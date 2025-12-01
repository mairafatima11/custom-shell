#define _GNU_SOURCE
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
#include <ctype.h>
#include <time.h>
#include <stdbool.h>

#define MAX_LINE 2048
#define MAX_ARGS 128
#define HISTORY_SIZE 200
#define MAX_JOBS 200
#define MAX_ALIASES 128
#define PATH_BUF 1024

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

#define MAX_PROCESSES 20
typedef struct Process {
    int pid;
    int arrival_time;
    int burst_time;
    int remaining_time;
    int completion_time;
    int turnaround_time;
    int waiting_time;
} Process;

static void simulate_fcfs(void) {
    printf("\n=== FCFS Scheduling Simulation ===\n");
    printf("Enter number of processes: ");
    int n;
    if (scanf("%d", &n) != 1) { while (getchar() != '\n' && getchar() != EOF); printf("Invalid\n"); return; }
    if (n <= 0 || n > MAX_PROCESSES) { printf("Invalid number (1..%d)\n", MAX_PROCESSES); return; }

    Process p[MAX_PROCESSES];
    memset(p, 0, sizeof(p));
    int time = 0;

    for (int i = 0; i < n; ++i) {
        p[i].pid = i + 1;
        printf("Process %d - Arrival Time: ", i+1);
        scanf("%d", &p[i].arrival_time);
        printf("Process %d - Burst Time: ", i+1);
        scanf("%d", &p[i].burst_time);
        p[i].remaining_time = p[i].burst_time;
    }
    while (getchar() != '\n' && !feof(stdin)) {}

    printf("\nGantt Chart: ");
    for (int i = 0; i < n; ++i) {
        while (time < p[i].arrival_time) ++time;
        printf("| P%d ", p[i].pid);
        time += p[i].burst_time;
        p[i].completion_time = time;
        p[i].turnaround_time = p[i].completion_time - p[i].arrival_time;
        p[i].waiting_time = p[i].turnaround_time - p[i].burst_time;
    }
    printf("|\n\n");

    printf("%-8s %-12s %-10s %-10s %-10s\n", "PID","Arrival","Burst","Turnaround","Waiting");
    float avg_tat = 0, avg_wt = 0;
    for (int i = 0; i < n; ++i) {
        printf("%-8d %-12d %-10d %-10d %-10d\n",
               p[i].pid, p[i].arrival_time, p[i].burst_time,
               p[i].turnaround_time, p[i].waiting_time);
        avg_tat += p[i].turnaround_time;
        avg_wt += p[i].waiting_time;
    }
    printf("\nAverage Turnaround Time: %.2f\n", avg_tat / n);
    printf("Average Waiting Time: %.2f\n\n", avg_wt / n);
}

static void simulate_rr(int quantum) {
    if (quantum <= 0) { printf("Quantum must be > 0\n"); return; }
    printf("\n=== Round Robin (Quantum = %d) Scheduling Simulation ===\n", quantum);
    printf("Enter number of processes: ");
    int n;
    if (scanf("%d", &n) != 1) { while (getchar() != '\n' && getchar() != EOF); printf("Invalid\n"); return; }
    if (n <= 0 || n > MAX_PROCESSES) { printf("Invalid number (1..%d)\n", MAX_PROCESSES); return; }

    Process p[MAX_PROCESSES];
    memset(p, 0, sizeof(p));
    int time = 0, completed = 0;
    int queue[MAX_PROCESSES];
    int front = 0, rear = 0;

    for (int i = 0; i < n; ++i) {
        p[i].pid = i + 1;
        printf("Process %d - Arrival Time: ", i+1); scanf("%d", &p[i].arrival_time);
        printf("Process %d - Burst Time: ", i+1); scanf("%d", &p[i].burst_time);
        p[i].remaining_time = p[i].burst_time;
    }
    while (getchar() != '\n' && !feof(stdin)) {}

    printf("\nGantt Chart: ");
    while (completed < n) {
        for (int i = 0; i < n; ++i) {
            if (p[i].arrival_time <= time && p[i].remaining_time > 0) {
                int found = 0;
                for (int j = front; j < rear; ++j) if (queue[j % MAX_PROCESSES] == i) { found = 1; break; }
                if (!found) { queue[rear++ % MAX_PROCESSES] = i; }
            }
        }

        if (front < rear) {
            int idx = queue[front++ % MAX_PROCESSES];
            int exec = (p[idx].remaining_time > quantum) ? quantum : p[idx].remaining_time;
            for (int t = 0; t < exec; ++t) printf(" P%d ", p[idx].pid);
            time += exec;
            p[idx].remaining_time -= exec;
            if (p[idx].remaining_time <= 0) {
                p[idx].completion_time = time;
                p[idx].turnaround_time = time - p[idx].arrival_time;
                p[idx].waiting_time = p[idx].turnaround_time - p[idx].burst_time;
                ++completed;
            } else {
                queue[rear++ % MAX_PROCESSES] = idx;
            }
        } else {
            printf(" idle ");
            ++time;
        }
    }
    printf("|\n\n");

    printf("%-8s %-12s %-10s %-10s %-10s\n", "PID","Arrival","Burst","Turnaround","Waiting");
    float avg_tat = 0, avg_wt = 0;
    for (int i = 0; i < n; ++i) {
        printf("%-8d %-12d %-10d %-10d %-10d\n",
               p[i].pid, p[i].arrival_time, p[i].burst_time,
               p[i].turnaround_time, p[i].waiting_time);
        avg_tat += p[i].turnaround_time;
        avg_wt += p[i].waiting_time;
    }
    printf("\nAverage Turnaround Time: %.2f\n", avg_tat / n);
    printf("Average Waiting Time: %.2f\n\n", avg_wt / n);
}

#define VFS_MAX_FILES 32
#define VFS_BLOCK_SIZE 128
#define VFS_NAME_LEN 32

typedef struct VFS_File {
    char name[VFS_NAME_LEN];
    int inode;
    int size;
    time_t created;
    time_t modified;
    char data[VFS_BLOCK_SIZE * 4];
} VFS_File;

static VFS_File vfs_files[VFS_MAX_FILES];
static int vfs_file_count = 0;
static int vfs_initialized = 0;

static void vfs_init(void) {
    if (vfs_initialized) return;
    memset(vfs_files, 0, sizeof(vfs_files));
    vfs_file_count = 0;
    vfs_initialized = 1;
}

static void vfs_create(const char *filename) {
    vfs_init();
    if (!filename) { printf("vfs: no filename\n"); return; }
    if (vfs_file_count >= VFS_MAX_FILES) { printf("vfs: filesystem full\n"); return; }
    for (int i = 0; i < vfs_file_count; ++i) {
        if (strcmp(vfs_files[i].name, filename) == 0) { printf("vfs: file '%s' already exists\n", filename); return; }
    }
    VFS_File *f = &vfs_files[vfs_file_count++];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, filename, VFS_NAME_LEN - 1);
    f->name[VFS_NAME_LEN - 1] = '\0';
    f->inode = vfs_file_count;
    f->size = 0;
    f->created = f->modified = time(NULL);
    printf("vfs: created file '%s'\n", filename);
}

static void vfs_write(const char *filename, const char *data) {
    vfs_init();
    if (!filename) { printf("vfs: no filename\n"); return; }
    for (int i = 0; i < vfs_file_count; ++i) {
        if (strcmp(vfs_files[i].name, filename) == 0) {
            if (!data) data = "";
            snprintf(vfs_files[i].data, sizeof(vfs_files[i].data), "%s", data);
            vfs_files[i].size = (int)strlen(vfs_files[i].data);
            vfs_files[i].modified = time(NULL);
            printf("vfs: wrote to '%s' (%d bytes)\n", filename, vfs_files[i].size);
            return;
        }
    }
    printf("vfs: no such file '%s'\n", filename);
}

static void vfs_cat(const char *filename) {
    vfs_init();
    if (!filename) { printf("vfs: no filename\n"); return; }
    for (int i = 0; i < vfs_file_count; ++i) {
        if (strcmp(vfs_files[i].name, filename) == 0) {
            printf("%s\n", vfs_files[i].data);
            return;
        }
    }
    printf("vfs: no such file '%s'\n", filename);
}

static void vfs_ls(void) {
    vfs_init();
    if (vfs_file_count == 0) { printf("(empty)\n"); return; }
    printf("%-20s %-8s %-12s %s\n", "Name", "Size", "Modified", "Created");
    for (int i = 0; i < vfs_file_count; ++i) {
        char mbuf[64], cbuf[64];
        struct tm mtm, ctm;
        localtime_r(&vfs_files[i].modified, &mtm);
        localtime_r(&vfs_files[i].created, &ctm);
        strftime(mbuf, sizeof(mbuf), "%b %d %H:%M", &mtm);
        strftime(cbuf, sizeof(cbuf), "%b %d %H:%M", &ctm);
        printf("%-20s %-8d %-12s %s\n", vfs_files[i].name, vfs_files[i].size, mbuf, cbuf);
    }
}

static void vfs_rm(const char *filename) {
    vfs_init();
    if (!filename) { printf("vfs: no filename\n"); return; }
    for (int i = 0; i < vfs_file_count; ++i) {
        if (strcmp(vfs_files[i].name, filename) == 0) {
            if (i + 1 < vfs_file_count) {
                memmove(&vfs_files[i], &vfs_files[i+1], sizeof(VFS_File) * (vfs_file_count - i - 1));
            }
            --vfs_file_count;
            printf("vfs: removed '%s'\n", filename);
            return;
        }
    }
    printf("vfs: no such file '%s'\n", filename);
}

static int handle_vfs(Command *cmd) {
    if (!cmd || !cmd->name) return 0;
    if (strcmp(cmd->name, "vfs") != 0) return 0;
    if (!cmd->args[1]) { printf("vfs: missing subcommand (create/write/ls/cat/rm)\n"); return 1; }

    if (strcmp(cmd->args[1], "create") == 0 && cmd->args[2]) {
        vfs_create(cmd->args[2]);
    } else if (strcmp(cmd->args[1], "write") == 0 && cmd->args[2]) {
        char combined[2048] = {0};
        for (int i = 3; cmd->args[i]; ++i) {
            if (strlen(combined) + strlen(cmd->args[i]) + 2 < sizeof(combined)) {
                if (combined[0]) strcat(combined, " ");
                strcat(combined, cmd->args[i]);
            }
        }
        vfs_write(cmd->args[2], combined);
    } else if (strcmp(cmd->args[1], "ls") == 0) {
        vfs_ls();
    } else if (strcmp(cmd->args[1], "cat") == 0 && cmd->args[2]) {
        vfs_cat(cmd->args[2]);
    } else if (strcmp(cmd->args[1], "rm") == 0 && cmd->args[2]) {
        vfs_rm(cmd->args[2]);
    } else {
        printf("vfs: unknown command. Use: create/write/ls/cat/rm\n");
    }
    return 1;
}

static int handle_schedule(Command *cmd) {
    if (!cmd || !cmd->name) return 0;
    if (strcmp(cmd->name, "schedule") != 0) return 0;
    if (!cmd->args[1]) { printf("Usage: schedule fcfs | schedule rr <quantum>\n"); return 1; }

    if (strcmp(cmd->args[1], "fcfs") == 0) {
        simulate_fcfs();
    } else if (strcmp(cmd->args[1], "rr") == 0 && cmd->args[2]) {
        int q = atoi(cmd->args[2]);
        if (q <= 0) { printf("Invalid quantum\n"); return 1; }
        simulate_rr(q);
    } else {
        printf("Usage: schedule fcfs | schedule rr <quantum>\n");
    }
    return 1;
}

static Job jobs[MAX_JOBS];
static int job_count = 0;
static int next_job_id = 1;

static void add_job(pid_t pid, const char *command, JobState state) {
    if (job_count >= MAX_JOBS) return;
    jobs[job_count].job_id = next_job_id++;
    jobs[job_count].pid = pid;
    strncpy(jobs[job_count].command, command ? command : "", sizeof(jobs[job_count].command)-1);
    jobs[job_count].command[sizeof(jobs[job_count].command)-1] = '\0';
    jobs[job_count].state = state;
    ++job_count;
}

static void remove_job(pid_t pid) {
    for (int i = 0; i < job_count; ++i) {
        if (jobs[i].pid == pid) {
            for (int j = i; j + 1 < job_count; ++j) jobs[j] = jobs[j+1];
            --job_count;
            return;
        }
    }
}

static Job* find_job(int job_id) {
    for (int i = 0; i < job_count; ++i) if (jobs[i].job_id == job_id) return &jobs[i];
    return NULL;
}

static void list_jobs(void) {
    for (int i = 0; i < job_count; ++i) {
        printf("[%d] %s %s\n", jobs[i].job_id,
               jobs[i].state==RUNNING ? "Running" : "Stopped",
               jobs[i].command);
    }
}

static char *history[HISTORY_SIZE];
static int history_count = 0;

static void add_history(const char *line) {
    if (!line) return;
    char *copy = strdup(line);
    if (!copy) return;
    if (history_count < HISTORY_SIZE) {
        history[history_count++] = copy;
    } else {
        free(history[0]);
        memmove(history, history + 1, (HISTORY_SIZE - 1) * sizeof(char*));
        history[HISTORY_SIZE - 1] = copy;
    }
}

static void print_history(void) {
    for (int i = 0; i < history_count; ++i) {
        printf("%4d  %s\n", i+1, history[i]);
    }
}

typedef struct Alias { char name[64]; char command[MAX_LINE]; } Alias;
static Alias aliases[MAX_ALIASES];
static int alias_count = 0;

static void add_alias(const char *name, const char *command) {
    if (!name || !command) return;
    for (int i = 0; i < alias_count; ++i) {
        if (strcmp(aliases[i].name, name) == 0) {
            strncpy(aliases[i].command, command, sizeof(aliases[i].command)-1);
            aliases[i].command[sizeof(aliases[i].command)-1] = '\0';
            return;
        }
    }
    if (alias_count < MAX_ALIASES) {
        strncpy(aliases[alias_count].name, name, sizeof(aliases[alias_count].name)-1);
        aliases[alias_count].name[sizeof(aliases[alias_count].name)-1] = '\0';
        strncpy(aliases[alias_count].command, command, sizeof(aliases[alias_count].command)-1);
        aliases[alias_count].command[sizeof(aliases[alias_count].command)-1] = '\0';
        ++alias_count;
    }
}

static const char* check_alias(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < alias_count; ++i) if (strcmp(aliases[i].name, name) == 0) return aliases[i].command;
    return NULL;
}

static char* find_command_in_path(const char *cmd) {
    if (!cmd || *cmd == '\0') return NULL;
    if (cmd[0] == '/' || cmd[0] == '.') {
        if (access(cmd, X_OK) == 0) return strdup(cmd);
        return NULL;
    }
    const char *pathenv = getenv("PATH");
    if (!pathenv) return NULL;
    char *pathdup = strdup(pathenv);
    if (!pathdup) return NULL;
    char full[PATH_BUF];
    char *saveptr = NULL;
    char *dir = strtok_r(pathdup, ":", &saveptr);
    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, cmd);
        if (access(full, X_OK) == 0) { free(pathdup); return strdup(full); }
        dir = strtok_r(NULL, ":", &saveptr);
    }
    free(pathdup);
    return NULL;
}

static void sigint_handler(int sig) { (void)sig; printf("\n"); fflush(stdout); }
static void sigtstp_handler(int sig) { (void)sig; printf("\n"); fflush(stdout); }

static void sigchld_handler(int sig) {
    (void)sig;
    int saved = errno;
    while (1) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED);
        if (pid <= 0) break;
        for (int i = 0; i < job_count; ++i) {
            if (jobs[i].pid == pid) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    remove_job(pid);
                } else if (WIFSTOPPED(status)) {
                    jobs[i].state = STOPPED;
                } else if (WIFCONTINUED(status)) {
                    jobs[i].state = RUNNING;
                }
                break;
            }
        }
    }
    errno = saved;
}

static void display_prompt(void) {
    char cwd[PATH_BUF];
    if (getcwd(cwd, sizeof(cwd))) printf("[my_shell:%s]$ ", cwd);
    else printf("[my_shell]$ ");
    fflush(stdout);
}

static char* read_input(void) {
    char *line = NULL;
    size_t n = 0;
    ssize_t r = getline(&line, &n, stdin);
    if (r <= 0) { free(line); return NULL; }
    while (r > 0 && (line[r-1] == '\n' || line[r-1] == '\r')) { line[--r] = '\0'; }
    return line;
}

static void free_tokens(char **tokens) {
    if (!tokens) return;
    for (int i = 0; tokens[i]; ++i) free(tokens[i]);
    free(tokens);
}

static char **tokenize_line(char *line, int *tok_count_out) {
    if (!line) { if (tok_count_out) *tok_count_out = 0; return NULL; }
    size_t cap = 64;
    char **tokens = malloc(cap * sizeof(char*));
    if (!tokens) return NULL;
    int tcount = 0;
    char *p = line;
    while (*p) {
        while (isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        if (tcount + 4 >= (int)cap) {
            cap *= 2;
            char **tmp = realloc(tokens, cap * sizeof(char*));
            if (!tmp) { free_tokens(tokens); return NULL; }
            tokens = tmp;
        }
        if (*p == '\"' || *p == '\'') {
            char q = *p++;
            char *start = p;
            while (*p && *p != q) ++p;
            size_t len = (size_t)(p - start);
            char *tok = malloc(len + 1);
            if (!tok) { free_tokens(tokens); return NULL; }
            memcpy(tok, start, len);
            tok[len] = '\0';
            tokens[tcount++] = tok;
            if (*p == q) ++p;
        } else if (*p == '>' || *p == '<' || *p == '|') {
            if (*p == '>' && *(p+1) == '>') {
                tokens[tcount++] = strdup(">>");
                p += 2;
            } else {
                char tmp[2] = {*p, '\0'};
                tokens[tcount++] = strdup(tmp);
                ++p;
            }
        } else {
            char *start = p;
            while (*p && !isspace((unsigned char)*p) && *p != '>' && *p != '<' && *p != '|') ++p;
            size_t len = (size_t)(p - start);
            char *tok = malloc(len + 1);
            if (!tok) { free_tokens(tokens); return NULL; }
            memcpy(tok, start, len);
            tok[len] = '\0';
            tokens[tcount++] = tok;
        }
    }
    tokens[tcount] = NULL;
    if (tok_count_out) *tok_count_out = tcount;
    return tokens;
}

static void free_command(Command *cmd) {
    while (cmd) {
        Command *n = cmd->next;
        if (cmd->name) free(cmd->name);
        for (int i = 0; i < MAX_ARGS && cmd->args[i]; ++i) free(cmd->args[i]);
        if (cmd->input_file) free(cmd->input_file);
        if (cmd->output_file) free(cmd->output_file);
        free(cmd);
        cmd = n;
    }
}

static Command* parse_input(const char *rawline) {
    if (!rawline) return NULL;
    char *line = strdup(rawline);
    if (!line) return NULL;
    int tcount = 0;
    char **tokens = tokenize_line(line, &tcount);
    free(line);
    if (!tokens) return NULL;

    if (tcount > 0) {
        const char *alias_cmd = check_alias(tokens[0]);
        if (alias_cmd) {
            int acount = 0;
            char *alias_dup = strdup(alias_cmd);
            char **alias_tokens = tokenize_line(alias_dup, &acount);
            free(alias_dup);
            int newcap = acount + (tcount - 1) + 1;
            char **combined = malloc(newcap * sizeof(char*));
            if (!combined) { free_tokens(alias_tokens); free_tokens(tokens); return NULL; }
            int idx = 0;
            for (int i = 0; i < acount; ++i) combined[idx++] = strdup(alias_tokens[i]);
            for (int i = 1; i < tcount; ++i) combined[idx++] = strdup(tokens[i]);
            combined[idx] = NULL;
            free_tokens(alias_tokens);
            free_tokens(tokens);
            tokens = combined;
            tcount = idx;
        }
    }

    Command *head = NULL;
    Command *cur = NULL;
    int i = 0;
    while (i < tcount) {
        if (!cur) {
            cur = calloc(1, sizeof(Command));
            if (!cur) break;
            for (int k = 0; k < MAX_ARGS; ++k) cur->args[k] = NULL;
            cur->input_file = NULL; cur->output_file = NULL; cur->append = 0; cur->next = NULL;
            if (!head) head = cur;
            else {
                Command *tmp = head;
                while (tmp->next) tmp = tmp->next;
                tmp->next = cur;
            }
        }
        char *tok = tokens[i];
        if (strcmp(tok, "|") == 0) {
            cur = NULL; ++i; continue;
        } else if (strcmp(tok, "<") == 0) {
            ++i;
            if (i >= tcount) { fprintf(stderr,"syntax error: expected filename after '<'\n"); break; }
            free(cur->input_file);
            cur->input_file = strdup(tokens[i]);
            ++i; continue;
        } else if (strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0) {
            int is_append = (strcmp(tok, ">>") == 0);
            ++i;
            if (i >= tcount) { fprintf(stderr,"syntax error: expected filename after '>' or '>>'\n"); break; }
            free(cur->output_file);
            cur->output_file = strdup(tokens[i]);
            cur->append = is_append;
            ++i; continue;
        } else {
            char *arg = tokens[i];
            if (arg[0] == '$') {
                char *val = getenv(arg + 1);
                arg = val ? val : "";
            }
            int aidx = 0;
            while (aidx < MAX_ARGS - 1 && cur->args[aidx]) ++aidx;
            if (aidx >= MAX_ARGS - 1) { fprintf(stderr,"too many arguments\n"); ++i; continue; }
            cur->args[aidx] = strdup(arg);
            cur->args[aidx + 1] = NULL;
            if (!cur->name) cur->name = strdup(cur->args[0]);
            ++i; continue;
        }
    }

    free_tokens(tokens);
    return head;
}

static int handle_builtin(Command *cmd) {
    if (!cmd || !cmd->name) return 0;
    if (handle_vfs(cmd)) return 1;
    if (handle_schedule(cmd)) return 1;

    if (strcmp(cmd->name, "cd") == 0) {
        char *dir = cmd->args[1] ? cmd->args[1] : getenv("HOME");
        if (!dir) dir = "/";
        if (chdir(dir) != 0) perror("cd");
        return 1;
    }
    if (strcmp(cmd->name, "exit") == 0) exit(0);
    if (strcmp(cmd->name, "pwd") == 0) {
        char cwd[PATH_BUF];
        if (getcwd(cwd, sizeof(cwd))) printf("%s\n", cwd); else perror("pwd");
        return 1;
    }
    if (strcmp(cmd->name, "history") == 0) { print_history(); return 1; }
    if (strcmp(cmd->name, "jobs") == 0) { list_jobs(); return 1; }
    if (strcmp(cmd->name, "alias") == 0) {
        if (cmd->args[1]) {
            char *eq = strchr(cmd->args[1], '=');
            if (eq) {
                *eq = '\0';
                char *val = eq + 1;
                if (val[0] == '"' && val[strlen(val)-1] == '"') { val[strlen(val)-1] = '\0'; ++val; }
                add_alias(cmd->args[1], val);
            } else { printf("alias: bad format. Use alias name=\"command\"\n"); }
        } else {
            for (int i = 0; i < alias_count; ++i) printf("alias %s=\"%s\"\n", aliases[i].name, aliases[i].command);
        }
        return 1;
    }
    if (strncmp(cmd->name, "set", 3) == 0 && cmd->args[1]) {
        char *eq = strchr(cmd->args[1], '=');
        if (eq) { *eq = '\0'; setenv(cmd->args[1], eq+1, 1); }
        return 1;
    }
    if (strcmp(cmd->name, "fg") == 0 || strcmp(cmd->name, "bg") == 0) {
        if (!cmd->args[1]) { printf("%s: job id required (e.g. %%1)\n", cmd->name); return 1; }
        int jid = atoi(cmd->args[1] + 1);
        Job *job = find_job(jid);
        if (!job) { printf("%s: no such job\n", cmd->name); return 1; }
        kill(-job->pid, SIGCONT);
        job->state = RUNNING;
        if (strcmp(cmd->name, "fg") == 0) {
            tcsetpgrp(STDIN_FILENO, job->pid);
            int status;
            waitpid(job->pid, &status, WUNTRACED);
            tcsetpgrp(STDIN_FILENO, getpid());
            if (WIFSTOPPED(status)) job->state = STOPPED; else remove_job(job->pid);
        }
        return 1;
    }
    return 0;
}

static int execute_pipeline(Command *cmd_list, int background, const char *full_line) {
    if (!cmd_list) return 0;
    Command *cmd = cmd_list;
    int prev_fd = -1;
    int pipefd[2];
    pid_t last_pid = -1;
    pid_t pgid = 0;

    while (cmd) {
        int has_next = (cmd->next != NULL);
        if (has_next) {
            if (pipe(pipefd) < 0) { perror("pipe"); return -1; }
        } else { pipefd[0] = pipefd[1] = -1; }

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return -1; }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            if (pgid == 0) pgid = getpid();
            setpgid(0, pgid);

            if (prev_fd != -1) { dup2(prev_fd, STDIN_FILENO); close(prev_fd); }
            if (cmd->input_file) {
                int fd = open(cmd->input_file, O_RDONLY);
                if (fd < 0) { perror("open input"); _exit(127); }
                dup2(fd, STDIN_FILENO); close(fd);
            }
            if (has_next) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            }
            if (cmd->output_file) {
                int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
                int fd = open(cmd->output_file, flags, 0644);
                if (fd < 0) { perror("open output"); _exit(127); }
                dup2(fd, STDOUT_FILENO); close(fd);
            }
            char *full = find_command_in_path(cmd->name);
            if (!full) { fprintf(stderr, "%s: command not found\n", cmd->name); _exit(127); }
            execv(full, cmd->args);
            perror("execv"); free(full); _exit(127);
        } else {
            if (pgid == 0) pgid = pid;
            setpgid(pid, pgid);
            last_pid = pid;
            if (has_next) {
                close(pipefd[1]);
                if (prev_fd != -1) close(prev_fd);
                prev_fd = pipefd[0];
            } else if (prev_fd != -1) { close(prev_fd); prev_fd = -1; }
            cmd = cmd->next;
        }
    }

    if (!background) {
        tcsetpgrp(STDIN_FILENO, pgid);
        int status;
        waitpid(last_pid, &status, WUNTRACED);
        tcsetpgrp(STDIN_FILENO, getpid());
        if (WIFSTOPPED(status)) { add_job(last_pid, full_line, STOPPED); printf("\n[%d] Stopped\n", next_job_id - 1); }
    } else {
        add_job(last_pid, full_line, RUNNING);
        printf("[%d] %d\n", next_job_id - 1, last_pid);
    }
    return 1;
}

static int execute_command(Command *cmd_list, int background, const char *full_line) {
    if (!cmd_list) return 0;
    if (handle_builtin(cmd_list)) return 1;
    if (cmd_list->next) return execute_pipeline(cmd_list, background, full_line);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        setpgid(0, 0);
        if (cmd_list->input_file) {
            int fd = open(cmd_list->input_file, O_RDONLY);
            if (fd < 0) { perror("open input"); _exit(127); }
            dup2(fd, STDIN_FILENO); close(fd);
        }
        if (cmd_list->output_file) {
            int flags = O_WRONLY | O_CREAT | (cmd_list->append ? O_APPEND : O_TRUNC);
            int fd = open(cmd_list->output_file, flags, 0644);
            if (fd < 0) { perror("open output"); _exit(127); }
            dup2(fd, STDOUT_FILENO); close(fd);
        }
        char *full = find_command_in_path(cmd_list->name);
        if (!full) { fprintf(stderr, "%s: command not found\n", cmd_list->name); _exit(127); }
        execv(full, cmd_list->args);
        perror("execv"); free(full); _exit(127);
    } else {
        setpgid(pid, pid);
        if (background) { add_job(pid, full_line, RUNNING); printf("[%d] %d\n", next_job_id - 1, pid); }
        else {
            tcsetpgrp(STDIN_FILENO, pid);
            int status;
            waitpid(pid, &status, WUNTRACED);
            tcsetpgrp(STDIN_FILENO, getpid());
            if (WIFSTOPPED(status)) { add_job(pid, full_line, STOPPED); printf("\n[%d] Stopped\n", next_job_id - 1); }
        }
    }
    return 1;
}

int main(void) {
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    pid_t shell_pgid = getpid();
    setpgid(shell_pgid, shell_pgid);
    tcsetpgrp(STDIN_FILENO, shell_pgid);
    setenv("SHELL", "my_shell", 1);

    while (1) {
        display_prompt();
        char *line = read_input();
        if (!line) break;
        if (line[0] == '\0') { free(line); continue; }

        int background = 0;
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '&') {
            background = 1;
            line[len-1] = '\0';
            while (len > 0 && isspace((unsigned char)line[len-1])) { line[--len] = '\0'; }
        }

        add_history(line);
        Command *cmd = parse_input(line);
        if (cmd) {
            execute_command(cmd, background, line);
            free_command(cmd);
        }
        free(line);
    }

    printf("\nExiting my_shell.\n");
    for (int i = 0; i < history_count; ++i) free(history[i]);
    return 0;
}
