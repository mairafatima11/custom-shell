//  merged environment variable support, alias support, and tab-completion
//  baqi ye bhi chal raha hai (jobs, history, piping, redirection, background/foreground)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>
#include <termios.h>

#define MAX_LINE 1024
#define MAX_ARGS 64
#define HISTORY_SIZE 50
#define MAX_JOBS 100
#define MAX_ALIASES 50

typedef struct Command {
    char *name;
    char *args[MAX_ARGS];
    char *input_file;
    char *output_file;
    int append;
    struct Command *next;
} Command;

typedef enum { RUNNING, STOPPED } JobState;
typedef struct Job {
    int job_id;
    pid_t pid;
    char command[MAX_LINE];
    JobState state;
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;
int next_job_id = 1;

void add_job(pid_t pid, char *command, JobState state) {
    if(job_count >= MAX_JOBS) return;
    jobs[job_count].job_id = next_job_id++;
    jobs[job_count].pid = pid;
    strncpy(jobs[job_count].command, command, MAX_LINE);
    jobs[job_count].state = state;
    job_count++;
}

void remove_job(pid_t pid) {
    for(int i=0;i<job_count;i++){
        if(jobs[i].pid==pid){
            for(int j=i;j<job_count-1;j++) jobs[j]=jobs[j+1];
            job_count--; return;
        }
    }
}

Job* find_job(int job_id) {
    for(int i=0;i<job_count;i++) if(jobs[i].job_id==job_id) return &jobs[i];
    return NULL;
}

void list_jobs() {
    for(int i=0;i<job_count;i++){
        printf("[%d] %s %s\n", jobs[i].job_id,
               jobs[i].state==RUNNING?"Running":"Stopped",
               jobs[i].command);
    }
}

char *history[HISTORY_SIZE];
int history_count = 0;

void add_history(char *line) {
    if (history_count < HISTORY_SIZE) history[history_count++] = strdup(line);
    else { free(history[0]); memmove(history, history+1, (HISTORY_SIZE-1)*sizeof(char*)); history[HISTORY_SIZE-1]=strdup(line); }
}

void print_history() { for(int i=0;i<history_count;i++) printf("%d: %s\n", i+1, history[i]); }

typedef struct Alias { char name[64]; char command[MAX_LINE]; } Alias;
Alias aliases[MAX_ALIASES];
int alias_count=0;

void add_alias(char *name, char *command){
    for(int i=0;i<alias_count;i++)
        if(strcmp(aliases[i].name,name)==0){ strcpy(aliases[i].command,command); return; }
    if(alias_count<MAX_ALIASES){ strcpy(aliases[alias_count].name,name); strcpy(aliases[alias_count].command,command); alias_count++; }
}

char* check_alias(char *name){
    for(int i=0;i<alias_count;i++) if(strcmp(aliases[i].name,name)==0) return aliases[i].command;
    return NULL;
}

void sigint_handler(int sig);
void sigtstp_handler(int sig);
void display_prompt();
char* read_input();
Command* parse_input(char *line);
void free_command(Command *cmd);
int execute_command(Command *cmd, int background, char *full_command_line);
int handle_builtin(Command *cmd);
int execute_pipeline(Command *cmd,int background,char *full_command_line);
char* next_token(char **input);
void tab_complete(char *buf, size_t *len);

void sigint_handler(int sig){ printf("\n"); display_prompt(); fflush(stdout); }
void sigtstp_handler(int sig){ printf("\n"); display_prompt(); fflush(stdout); }

void display_prompt() {
    char cwd[1024];
    getcwd(cwd,sizeof(cwd));
    printf("[my_shell:%s]$ ",cwd);
    fflush(stdout);
}

char* read_input() {
    char *line = malloc(MAX_LINE);
    if (!line) { perror("malloc failed"); exit(1); }
    size_t len = 0;
    int c;
    while((c=getchar())!=EOF){
        if(c=='\t'){ tab_complete(line,&len); continue; }
        if(c=='\n'){ line[len]='\0'; 
             break; }
        line[len++]=c;
    }
    return line;
}

void tab_complete(char *buf, size_t *len){
    buf[*len]='\0';
    DIR *d = opendir(".");
    if(!d) return;
    struct dirent *entry;
    int matches=0;
    char match[256];
    while((entry=readdir(d))){
        if(strncmp(entry->d_name, buf, strlen(buf))==0){
            matches++;
            strcpy(match, entry->d_name);
        }
    }
    closedir(d);
    if(matches==1){
        size_t l=strlen(match);
        strcpy(buf, match);
        *len=l;
        printf("\r%s", buf);
        fflush(stdout);
    }
}

char* next_token(char **input){
    char *start=*input,*token;
    if(!start) return NULL;
    while(*start==' ') start++;
    if(*start=='\0') return NULL;
    if(*start=='"'||*start=='\''){ char q=*start; start++; token=start; while(*start && *start!=q) start++; if(*start)*start++='\0'; *input=start; return token; }
    token=start;
    while(*start && *start!=' ' && *start!='<' && *start!='>' && *start!='|') start++;
    if(*start){ if(*start=='<'||*start=='>'||*start=='|'){ if(start==token){ if(*start=='>'&&*(start+1)=='>'){*start='\0';*input=start+2; return ">>";} char c=*start;*start++='\0';*input=start; char *sym=malloc(2); sym[0]=c;sym[1]='\0';return sym;} } else *start++='\0'; }
    *input=start;
    return token;
}

Command* parse_input(char *line){
    Command *head=NULL,*current=NULL;
    char *token, *rest = line;

    while((token = next_token(&rest)) != NULL){
       
        char* alias_exp = check_alias(token);
        if(alias_exp){
            char new_rest[MAX_LINE];
            snprintf(new_rest, MAX_LINE, "%s %s", alias_exp, rest ? rest : "");
            rest = new_rest;
            token = next_token(&rest);
        }

        Command *cmd = malloc(sizeof(Command));
        cmd->input_file = NULL;
        cmd->output_file = NULL;
        cmd->append = 0;
        cmd->next = NULL;

        int arg_index = 0;
        while(token != NULL){
            if(strcmp(token,"<") == 0){ token = next_token(&rest); cmd->input_file = strdup(token); }
            else if(strcmp(token,">") == 0){ token = next_token(&rest); cmd->output_file = strdup(token); cmd->append = 0; }
            else if(strcmp(token,">>") == 0){ token = next_token(&rest); cmd->output_file = strdup(token); cmd->append = 1; }
            else if(strcmp(token,"|") == 0) break;
            else {
                if(token[0]=='$'){ char* val=getenv(token+1); if(val) token = val; }
                cmd->args[arg_index++] = strdup(token);
            }
            token = next_token(&rest);
        }
        cmd->args[arg_index] = NULL;
        cmd->name = (arg_index>0) ? strdup(cmd->args[0]) : NULL;

        if(!head) head = cmd;
        if(current) current->next = cmd;
        current = cmd;
    }
    return head;
}

void free_command(Command *cmd){ while(cmd){ Command *next=cmd->next; for(int i=0;cmd->args[i];i++) free(cmd->args[i]); if(cmd->input_file) free(cmd->input_file); if(cmd->output_file) free(cmd->output_file); if(cmd->name) free(cmd->name); free(cmd); cmd=next;} }

int handle_builtin(Command *cmd){
    if(!cmd->name) return 0;
    if(strcmp(cmd->name,"cd")==0){ if(cmd->args[1]) chdir(cmd->args[1]); else chdir(getenv("HOME")); return 1; }
    else if(strcmp(cmd->name,"exit")==0) exit(0);
    else if(strcmp(cmd->name,"pwd")==0){ char cwd[1024]; getcwd(cwd,sizeof(cwd)); printf("%s\n",cwd); return 1; }
    else if(strcmp(cmd->name,"mkdir")==0){ if(cmd->args[1]) mkdir(cmd->args[1],0755); else printf("mkdir: missing argument\n"); return 1; }
    else if(strcmp(cmd->name,"touch")==0){ if(cmd->args[1]){ int fd=open(cmd->args[1],O_CREAT|O_WRONLY,0644); if(fd>=0) close(fd); } else printf("touch: missing argument\n"); return 1; }
    else if(strcmp(cmd->name,"history")==0){ print_history(); return 1; }
    else if(strcmp(cmd->name,"jobs")==0){ list_jobs(); return 1; }
    else if(strcmp(cmd->name,"fg")==0){
        if(!cmd->args[1]){ printf("fg: missing argument\n"); return 1; }
        int jid=atoi(cmd->args[1]+1);
        Job* job=find_job(jid);
        if(!job){ printf("fg: no such job\n"); return 1; }
        kill(-job->pid,SIGCONT);
        job->state=RUNNING;
        int status;
        tcsetpgrp(STDIN_FILENO,job->pid);
        waitpid(job->pid,&status,WUNTRACED); 
        tcsetpgrp(STDIN_FILENO,getpid());
        if(WIFSTOPPED(status)) job->state=STOPPED;
        else remove_job(job->pid);
        return 1;
    }
    else if(strcmp(cmd->name,"bg")==0){
        if(!cmd->args[1]){ printf("bg: missing argument\n"); return 1; }
        int jid=atoi(cmd->args[1]+1);
        Job* job=find_job(jid);
        if(!job){ printf("bg: no such job\n"); return 1; }
        kill(-job->pid,SIGCONT);
        job->state=RUNNING;
        return 1;
    }
    else if(strncmp(cmd->name,"set",3)==0 && cmd->args[1]){
        char* eq=strchr(cmd->args[1],'=');
        if(eq){ *eq='\0'; setenv(cmd->args[1], eq+1, 1); }
        return 1;
    }
    else if(strcmp(cmd->name,"alias")==0 && cmd->args[1]){
        char* eq=strchr(cmd->args[1],'=');
        if(eq){ *eq='\0'; char* val=eq+1; if(val[0]=='"') val++; if(val[strlen(val)-1]=='"') val[strlen(val)-1]='\0'; add_alias(cmd->args[1],val); }
        return 1;
    }
    return 0;
}

int execute_pipeline(Command *cmd, int background, char *full_command_line) {
    int fd[2];
    pid_t pid;
    int in_fd = 0;
    Command *current = cmd;

    while (current) {
        pipe(fd);
        pid = fork();
        if (pid < 0) { perror("fork failed"); exit(1); }

        if (pid == 0) { // child
            setpgid(0,0);
            signal(SIGINT,SIG_DFL);
            signal(SIGTSTP,SIG_DFL);

            dup2(in_fd, STDIN_FILENO);
            if (current->next) dup2(fd[1], STDOUT_FILENO);

            if (current->input_file) {
                int f=open(current->input_file,O_RDONLY);
                if(f<0){perror("input");exit(1);}
                dup2(f,STDIN_FILENO); close(f);
            }
            if (current->output_file) {
                int f;
                if(current->append) f=open(current->output_file,O_WRONLY|O_CREAT|O_APPEND,0644);
                else f=open(current->output_file,O_WRONLY|O_CREAT|O_TRUNC,0644);
                if(f<0){perror("output");exit(1);}
                dup2(f,STDOUT_FILENO); close(f);
            }
            close(fd[0]);
            execvp(current->name,current->args);
            perror("exec failed"); exit(1);
        } else {
            setpgid(pid,pid);
            if(!background) waitpid(pid,NULL,0);
            else add_job(pid, full_command_line, RUNNING);

            close(fd[1]);
            in_fd = fd[0];
            current=current->next;
        }
    }
    return 1;
}

int execute_command(Command *cmd, int background, char *full_command_line) {
    if(!cmd) return 0;
    if(handle_builtin(cmd)) return 1;
    if(cmd->next) return execute_pipeline(cmd, background, full_command_line);

    pid_t pid=fork();
    if(pid<0){perror("fork failed");return -1;}
    if(pid==0){ 
        setpgid(0,0);
        signal(SIGINT,SIG_DFL);
        signal(SIGTSTP,SIG_DFL);

        if(cmd->input_file){int fd=open(cmd->input_file,O_RDONLY);if(fd<0){perror("input");exit(1);}dup2(fd,STDIN_FILENO);close(fd);}
        if(cmd->output_file){int fd; if(cmd->append) fd=open(cmd->output_file,O_WRONLY|O_CREAT|O_APPEND,0644); else fd=open(cmd->output_file,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd<0){perror("output");exit(1);}dup2(fd,STDOUT_FILENO);close(fd);}
        execvp(cmd->name,cmd->args);
        perror("exec failed"); exit(1);
    } else {
        setpgid(pid,pid);
        if(background){
            add_job(pid, full_command_line, RUNNING);
            printf("[Background pid %d]\n",pid);
        } else {
            int status;
            tcsetpgrp(STDIN_FILENO,pid);
            waitpid(pid,&status,WUNTRACED);
            if(WIFSTOPPED(status)){
                add_job(pid, full_command_line, STOPPED);
                printf("\n[%d] Stopped\n", next_job_id-1);
            }
            tcsetpgrp(STDIN_FILENO,getpid());
        }
    }
    return 1;
}


int main() {
signal(SIGTTOU, SIG_IGN);
signal(SIGTTIN, SIG_IGN);
signal(SIGTSTP, SIG_IGN);

pid_t shell_pgid = getpid();
setpgid(shell_pgid, shell_pgid);
tcsetpgrp(STDIN_FILENO, shell_pgid);

    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);

    while(1){
        display_prompt();
        char *line=read_input();
        if(!line) break;

        int background=0;
        int n=strlen(line);
        if(n>0 && line[n-1]=='&'){background=1;line[n-1]='\0';}

        add_history(line);
        Command *cmd=parse_input(line);
        if(cmd){ execute_command(cmd, background, line); free_command(cmd);}
        free(line);
    }
    printf("\nExiting my_shell.\n");
    return 0;
}
