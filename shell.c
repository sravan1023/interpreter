#include "shell.h"
#include "interpreter.h"

#ifdef XINU_KERNEL
#include "../include/kernel.h"
#include "../include/process.h"
#include "../include/memory.h"
#include "../include/types.h"
#else

/* Standalone mode */
#define NPROC       64
#define PR_FREE     0
#define PR_CURR     1
#define PR_READY    2
#define PR_SLEEP    4
#define PR_SUSP     5
#define PR_WAIT     6
#define PR_RECV     3

typedef struct {
    uint32_t pstate;
    uint32_t pprio;
    char pname[16];
} proc_t;

static proc_t proctab[NPROC];
static pid32 currpid = 0;

static pid32 getpid(void) { return currpid; }
static int kill(pid32 pid) { (void)pid; return OK; }
static void resume(pid32 pid) { (void)pid; }
static void yield(void) { }
static void sleep(uint32_t ms) { (void)ms; }
#endif

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>


static shell_state_t shell_state;
static shell_command_t shell_commands[128];
static int shell_command_count = 0;
static shell_job_t shell_jobs[32];
static int shell_job_count = 0;


static int cmd_help(int argc, char **argv);
static int cmd_exit(int argc, char **argv);
static int cmd_cd(int argc, char **argv);
static int cmd_pwd(int argc, char **argv);
static int cmd_echo(int argc, char **argv);
static int cmd_set(int argc, char **argv);
static int cmd_unset(int argc, char **argv);
static int cmd_export(int argc, char **argv);
static int cmd_env(int argc, char **argv);
static int cmd_alias(int argc, char **argv);
static int cmd_unalias(int argc, char **argv);
static int cmd_history(int argc, char **argv);
static int cmd_ps(int argc, char **argv);
static int cmd_kill(int argc, char **argv);
static int cmd_jobs(int argc, char **argv);
static int cmd_fg(int argc, char **argv);
static int cmd_bg(int argc, char **argv);
static int cmd_mem(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_sleep(int argc, char **argv);
static int cmd_time(int argc, char **argv);
static int cmd_true(int argc, char **argv);
static int cmd_false(int argc, char **argv);
static int cmd_test(int argc, char **argv);

static void shell_printf(const char *fmt, ...) {
    va_list args;
    
    va_start(args, fmt);
#ifdef XINU_KERNEL
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    printf("%s", buffer);
#else
    vprintf(fmt, args);
#endif
    va_end(args);
}

static void shell_error(const char *fmt, ...) {
    va_list args;
    
    va_start(args, fmt);
#ifdef XINU_KERNEL
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    fprintf(stderr, "%s", buffer);
#else
    vfprintf(stderr, fmt, args);
#endif
    va_end(args);
}

void shell_init(void) {
    int i;
    
    /* Initialize shell state */
    memset(&shell_state, 0, sizeof(shell_state));
    strcpy(shell_state.cwd, "/");
    shell_state.interactive = true;
    shell_state.running = true;
    shell_state.pid = getpid();
    
    shell_state.history_count = 0;
    shell_state.history_index = 0;
    
    shell_state.alias_count = 0;
    for (i = 0; i < SHELL_MAX_ALIAS; i++) {
        shell_state.aliases[i].defined = false;
    }
    
    shell_command_count = 0;
    memset(shell_commands, 0, sizeof(shell_commands));
    
    shell_job_count = 0;
    memset(shell_jobs, 0, sizeof(shell_jobs));
    shell_builtin_init();
}


void shell_builtin_init(void) {
    shell_register_command("help", "Display help information", cmd_help);
    shell_register_command("exit", "Exit the shell", cmd_exit);
    shell_register_command("quit", "Exit the shell", cmd_exit);
    shell_register_command("cd", "Change directory", cmd_cd);
    shell_register_command("pwd", "Print working directory", cmd_pwd);
    shell_register_command("echo", "Display text", cmd_echo);
    shell_register_command("clear", "Clear screen", cmd_clear);
    shell_register_command("set", "Set shell variable", cmd_set);
    shell_register_command("unset", "Unset shell variable", cmd_unset);
    shell_register_command("export", "Export variable", cmd_export);
    shell_register_command("env", "Display environment", cmd_env);
    shell_register_command("alias", "Create alias", cmd_alias);
    shell_register_command("unalias", "Remove alias", cmd_unalias);
    shell_register_command("history", "Show command history", cmd_history);
    shell_register_command("ps", "List processes", cmd_ps);
    shell_register_command("kill", "Kill process", cmd_kill);
    shell_register_command("jobs", "List background jobs", cmd_jobs);
    shell_register_command("fg", "Bring job to foreground", cmd_fg);
    shell_register_command("bg", "Send job to background", cmd_bg);
    shell_register_command("mem", "Display memory statistics", cmd_mem);
    shell_register_command("sleep", "Sleep for seconds", cmd_sleep);
    shell_register_command("time", "Time a command", cmd_time);
    shell_register_command("test", "Evaluate expression", cmd_test);
    shell_register_command("[", "Test (alternate form)", cmd_test);
    shell_register_command("true", "Return success", cmd_true);
    shell_register_command("false", "Return failure", cmd_false);
}

int shell_register_command(const char *name, const char *desc, 
                           shell_cmd_func func) {
    if (shell_command_count >= 128) {
        return SYSERR;
    }
    
    strncpy(shell_commands[shell_command_count].name, name, SHELL_MAX_CMD - 1);
    strncpy(shell_commands[shell_command_count].description, desc, 127);
    shell_commands[shell_command_count].func = func;
    shell_commands[shell_command_count].builtin = true;
    shell_command_count++;
    
    return OK;
}

shell_command_t* shell_find_command(const char *name) {
    int i;
    
    for (i = 0; i < shell_command_count; i++) {
        if (strcmp(shell_commands[i].name, name) == 0) {
            return &shell_commands[i];
        }
    }
    
    return NULL;
}

bool shell_is_builtin(const char *name) {
    return shell_find_command(name) != NULL;
}

char* shell_readline(char *buffer, int size) {
    int i = 0;
    int ch;
    
    /* Read characters until newline or buffer full */
    while (i < size - 1) {
        ch = getchar();
        
        if (ch == EOF || ch == '\n' || ch == '\r') {
            break;
        }
        
        if (ch == '\b' || ch == 127) { 
            if (i > 0) {
                i--;
            }
            continue;
        }
        
        if (ch == 0x03) { 
            buffer[0] = '\0';
            return buffer;
        }
        
        if (ch == 0x04) { 
            if (i == 0) {
                return NULL;
            }
            break;
        }
        
        buffer[i++] = (char)ch;
    }
    
    buffer[i] = '\0';
    return buffer;
}

int shell_parse_line(char *line, char **argv, int max_args) {
    int argc = 0;
    char *p = line;
    bool in_quote = false;
    char quote_char = '\0';
    
    while (*p != '\0' && argc < max_args - 1) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        
        if (*p == '\0') {
            break;
        }

        if (*p == '#' && !in_quote) {
            break;
        }
        
        argv[argc++] = p;
        
        while (*p != '\0') {
            if (*p == '\\' && *(p + 1) != '\0') {

                memmove(p, p + 1, strlen(p));
                p++;
                continue;
            }
            
            if (*p == '"' || *p == '\'') {
                if (!in_quote) {
                    in_quote = true;
                    quote_char = *p;
                    memmove(p, p + 1, strlen(p));
                    continue;
                } else if (*p == quote_char) {
                    in_quote = false;
                    memmove(p, p + 1, strlen(p));
                    continue;
                }
            }
            
            if (!in_quote && (*p == ' ' || *p == '\t')) {
                break;
            }
            
            p++;
        }
        
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    
    argv[argc] = NULL;
    return argc;
}

int shell_expand(const char *input, char *output, int size) {
    const char *p = input;
    int i = 0;
    
    while (*p != '\0' && i < size - 1) {
        if (*p == '$') {
            p++;
            
            if (*p == '?') {
                char num[16];
                snprintf(num, sizeof(num), "%d", shell_state.last_exit);
                int len = strlen(num);
                if (i + len < size - 1) {
                    strcpy(output + i, num);
                    i += len;
                }
                p++;
                continue;
            } else if (*p == '$') {
                char num[16];
                snprintf(num, sizeof(num), "%d", shell_state.pid);
                int len = strlen(num);
                if (i + len < size - 1) {
                    strcpy(output + i, num);
                    i += len;
                }
                p++;
                continue;
            }
            
            char var_name[64];
            int j = 0;
            bool in_braces = false;
            
            if (*p == '{') {
                in_braces = true;
                p++;
            }
            
            while (*p != '\0' && j < 63) {
                if (in_braces && *p == '}') {
                    p++;
                    break;
                }
                if (!in_braces && !isalnum(*p) && *p != '_') {
                    break;
                }
                var_name[j++] = *p++;
            }
            var_name[j] = '\0';
            
            char *value = shell_getenv(var_name);
            if (value != NULL) {
                int len = strlen(value);
                if (i + len < size - 1) {
                    strcpy(output + i, value);
                    i += len;
                }
            }
        } else if (*p == '~' && (p == input || *(p - 1) == ' ' || *(p - 1) == ':')) {
            char *home = shell_getenv("HOME");
            if (home == NULL) {
                home = "/";
            }
            int len = strlen(home);
            if (i + len < size - 1) {
                strcpy(output + i, home);
                i += len;
            }
            p++;
        } else {
            output[i++] = *p++;
        }
    }
    
    output[i] = '\0';
    return OK;
}


int shell_execute(const char *line) {
    char expanded[SHELL_MAX_LINE];
    char *argv[SHELL_MAX_ARGS];
    int argc;
    char line_copy[SHELL_MAX_LINE];
    
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '#') {
        return SHELL_OK;
    }
    
    if (shell_state.interactive) {
        shell_history_add(line);
    }
    
    strncpy(line_copy, line, SHELL_MAX_LINE - 1);
    line_copy[SHELL_MAX_LINE - 1] = '\0';
    
    shell_expand(line_copy, expanded, SHELL_MAX_LINE);
    
    /* Parse command line */
    argc = shell_parse_line(expanded, argv, SHELL_MAX_ARGS);
    if (argc == 0) {
        return SHELL_OK;
    }
    
    /* Look for built-in command */
    shell_command_t *cmd = shell_find_command(argv[0]);
    if (cmd != NULL) {
        shell_state.last_exit = cmd->func(argc, argv);
        return shell_state.last_exit;
    }
    
    /* Try to execute as external program */
    shell_error("%s: command not found\n", argv[0]);
    shell_state.last_exit = SHELL_NOT_FOUND;
    return SHELL_NOT_FOUND;
}

int shell_execute_file(const char *filename) {
    /* Would read and execute script file */
    return SHELL_OK;
}

void shell_run(void) {
    char line[SHELL_MAX_LINE];
    
    shell_init();
    
    shell_printf("Xinu Shell\n");
    shell_printf("Type 'help' for commands\n\n");
    
    while (shell_state.running) {
        /* Print prompt */
        shell_printf("%s", SHELL_PROMPT);
        
        /* Read command line */
        if (shell_readline(line, SHELL_MAX_LINE) == NULL) {
            shell_printf("\n");
            break;
        }
        
        /* Execute command */
        shell_execute(line);
    }
}

void shell_start(void) {
    shell_run();
}

void shell_process(void) {
    shell_run();
}

void shell_exit(int status) {
    shell_state.running = false;
    shell_state.last_exit = status;
}


void shell_history_add(const char *cmd) {
    if (cmd == NULL || *cmd == '\0') {
        return;
    }
    
    if (shell_state.history_count > 0) {
        int last = (shell_state.history_index - 1 + SHELL_HISTORY_SIZE) % 
                   SHELL_HISTORY_SIZE;
        if (strcmp(shell_state.history[last].command, cmd) == 0) {
            return;
        }
    }
    
    strncpy(shell_state.history[shell_state.history_index].command, 
            cmd, SHELL_MAX_LINE - 1);
    shell_state.history[shell_state.history_index].number = 
        shell_state.history_count;
    
    shell_state.history_index = (shell_state.history_index + 1) % SHELL_HISTORY_SIZE;
    if (shell_state.history_count < SHELL_HISTORY_SIZE) {
        shell_state.history_count++;
    }
}

char* shell_history_get(int index) {
    if (index < 0 || index >= shell_state.history_count) {
        return NULL;
    }
    
    int actual = (shell_state.history_index - shell_state.history_count + 
                  index + SHELL_HISTORY_SIZE) % SHELL_HISTORY_SIZE;
    return shell_state.history[actual].command;
}

void shell_history_clear(void) {
    shell_state.history_count = 0;
    shell_state.history_index = 0;
}

void shell_history_list(void) {
    int i;
    for (i = 0; i < shell_state.history_count; i++) {
        char *cmd = shell_history_get(i);
        if (cmd != NULL) {
            shell_printf("%5d  %s\n", i + 1, cmd);
        }
    }
}


int shell_alias_set(const char *name, const char *value) {
    int i;
    
    for (i = 0; i < SHELL_MAX_ALIAS; i++) {
        if (shell_state.aliases[i].defined &&
            strcmp(shell_state.aliases[i].name, name) == 0) {
            strncpy(shell_state.aliases[i].value, value, SHELL_MAX_LINE - 1);
            return OK;
        }
    }
    
    for (i = 0; i < SHELL_MAX_ALIAS; i++) {
        if (!shell_state.aliases[i].defined) {
            shell_state.aliases[i].defined = true;
            strncpy(shell_state.aliases[i].name, name, SHELL_MAX_CMD - 1);
            strncpy(shell_state.aliases[i].value, value, SHELL_MAX_LINE - 1);
            shell_state.alias_count++;
            return OK;
        }
    }
    
    return SYSERR;
}

char* shell_alias_get(const char *name) {
    int i;
    
    for (i = 0; i < SHELL_MAX_ALIAS; i++) {
        if (shell_state.aliases[i].defined &&
            strcmp(shell_state.aliases[i].name, name) == 0) {
            return shell_state.aliases[i].value;
        }
    }
    
    return NULL;
}

int shell_alias_remove(const char *name) {
    int i;
    
    for (i = 0; i < SHELL_MAX_ALIAS; i++) {
        if (shell_state.aliases[i].defined &&
            strcmp(shell_state.aliases[i].name, name) == 0) {
            shell_state.aliases[i].defined = false;
            shell_state.alias_count--;
            return OK;
        }
    }
    
    return SYSERR;
}

void shell_alias_list(void) {
    int i;
    
    for (i = 0; i < SHELL_MAX_ALIAS; i++) {
        if (shell_state.aliases[i].defined) {
            shell_printf("alias %s='%s'\n", 
                        shell_state.aliases[i].name,
                        shell_state.aliases[i].value);
        }
    }
}


static struct {
    char name[64];
    char value[256];
    bool defined;
} shell_env[64];

char* shell_getenv(const char *name) {
    int i;
    
    for (i = 0; i < 64; i++) {
        if (shell_env[i].defined && strcmp(shell_env[i].name, name) == 0) {
            return shell_env[i].value;
        }
    }
    
    return NULL;
}

int shell_setenv(const char *name, const char *value) {
    int i;

    for (i = 0; i < 64; i++) {
        if (shell_env[i].defined && strcmp(shell_env[i].name, name) == 0) {
            strncpy(shell_env[i].value, value, 255);
            return OK;
        }
    }
    
    for (i = 0; i < 64; i++) {
        if (!shell_env[i].defined) {
            shell_env[i].defined = true;
            strncpy(shell_env[i].name, name, 63);
            strncpy(shell_env[i].value, value, 255);
            return OK;
        }
    }
    
    return SYSERR;
}

int shell_unsetenv(const char *name) {
    int i;
    
    for (i = 0; i < 64; i++) {
        if (shell_env[i].defined && strcmp(shell_env[i].name, name) == 0) {
            shell_env[i].defined = false;
            return OK;
        }
    }
    
    return SYSERR;
}

int shell_job_create(pid32 pid, const char *command, bool foreground) {
    int i;
    
    for (i = 0; i < 32; i++) {
        if (shell_jobs[i].state == JOB_DONE || shell_jobs[i].id == 0) {
            shell_jobs[i].id = i + 1;
            shell_jobs[i].pid = pid;
            shell_jobs[i].pgid = pid;
            shell_jobs[i].state = JOB_RUNNING;
            strncpy(shell_jobs[i].command, command, SHELL_MAX_LINE - 1);
            shell_jobs[i].foreground = foreground;
            shell_job_count++;
            return shell_jobs[i].id;
        }
    }
    
    return SYSERR;
}

void shell_job_update(int id, job_state_t state) {
    int i;
    
    for (i = 0; i < 32; i++) {
        if (shell_jobs[i].id == id) {
            shell_jobs[i].state = state;
            return;
        }
    }
}

shell_job_t* shell_job_find(int id) {
    int i;
    
    for (i = 0; i < 32; i++) {
        if (shell_jobs[i].id == id) {
            return &shell_jobs[i];
        }
    }
    
    return NULL;
}

shell_job_t* shell_job_find_by_pid(pid32 pid) {
    int i;
    
    for (i = 0; i < 32; i++) {
        if (shell_jobs[i].pid == pid) {
            return &shell_jobs[i];
        }
    }
    
    return NULL;
}

int shell_wait_job(int id) {
    shell_job_t *job = shell_job_find(id);
    if (job == NULL) {
        return SYSERR;
    }
    
    while (job->state == JOB_RUNNING) {
        yield();
    }
    
    return OK;
}

int shell_bg(pid32 pid) {
    shell_job_t *job = shell_job_find_by_pid(pid);
    if (job == NULL) {
        return SYSERR;
    }
    
    if (job->state == JOB_STOPPED) {
        resume(pid);
        job->state = JOB_RUNNING;
        job->foreground = false;
    }
    
    return OK;
}

int shell_fg(pid32 pid) {
    shell_job_t *job = shell_job_find_by_pid(pid);
    if (job == NULL) {
        return SYSERR;
    }
    
    if (job->state == JOB_STOPPED) {
        resume(pid);
    }
    
    job->state = JOB_RUNNING;
    job->foreground = true;
    
    shell_wait_job(job->id);
    
    return OK;
}

void shell_jobs_list(void) {
    int i;
    const char *state_str;
    
    for (i = 0; i < 32; i++) {
        if (shell_jobs[i].id > 0 && shell_jobs[i].state != JOB_DONE) {
            switch (shell_jobs[i].state) {
                case JOB_RUNNING: state_str = "Running"; break;
                case JOB_STOPPED: state_str = "Stopped"; break;
                case JOB_DONE:    state_str = "Done"; break;
                case JOB_KILLED:  state_str = "Killed"; break;
                default:          state_str = "Unknown"; break;
            }
            shell_printf("[%d]  %s\t\t%s\n", shell_jobs[i].id, 
                        state_str, shell_jobs[i].command);
        }
    }
}



static int cmd_help(int argc, char **argv) {
    int i;
    
    shell_printf("Xinu Shell - Built-in Commands:\n\n");
    
    for (i = 0; i < shell_command_count; i++) {
        shell_printf("  %-12s - %s\n", 
                    shell_commands[i].name,
                    shell_commands[i].description);
    }
    
    shell_printf("\nFor more information, see shell documentation.\n");
    return SHELL_OK;
}

static int cmd_exit(int argc, char **argv) {
    int status = SHELL_OK;
    
    if (argc > 1) {
        status = atoi(argv[1]);
    }
    
    shell_exit(status);
    return status;
}

static int cmd_cd(int argc, char **argv) {
    const char *dir;
    
    if (argc < 2) {
        dir = shell_getenv("HOME");
        if (dir == NULL) {
            dir = "/";
        }
    } else {
        dir = argv[1];
    }
    
    strncpy(shell_state.cwd, dir, SHELL_MAX_PATH - 1);
    shell_setenv("PWD", shell_state.cwd);
    
    return SHELL_OK;
}

static int cmd_pwd(int argc, char **argv) {
    shell_printf("%s\n", shell_state.cwd);
    return SHELL_OK;
}

static int cmd_echo(int argc, char **argv) {
    int i;
    bool newline = true;
    int start = 1;
    
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        newline = false;
        start = 2;
    }
    
    for (i = start; i < argc; i++) {
        if (i > start) {
            shell_printf(" ");
        }
        shell_printf("%s", argv[i]);
    }
    
    if (newline) {
        shell_printf("\n");
    }
    
    return SHELL_OK;
}

static int cmd_set(int argc, char **argv) {
    if (argc < 3) {
        int i;
        for (i = 0; i < 64; i++) {
            if (shell_env[i].defined) {
                shell_printf("%s=%s\n", shell_env[i].name, shell_env[i].value);
            }
        }
        return SHELL_OK;
    }
    
    return shell_setenv(argv[1], argv[2]);
}

static int cmd_unset(int argc, char **argv) {
    if (argc < 2) {
        shell_error("unset: missing variable name\n");
        return SHELL_ERROR;
    }
    
    return shell_unsetenv(argv[1]);
}

static int cmd_export(int argc, char **argv) {
    if (argc < 2) {
        return SHELL_OK;
    }
    
    char *eq = strchr(argv[1], '=');
    if (eq != NULL) {
        *eq = '\0';
        shell_setenv(argv[1], eq + 1);
    }
    
    return SHELL_OK;
}

static int cmd_env(int argc, char **argv) {
    int i;
    
    for (i = 0; i < 64; i++) {
        if (shell_env[i].defined) {
            shell_printf("%s=%s\n", shell_env[i].name, shell_env[i].value);
        }
    }
    
    return SHELL_OK;
}

static int cmd_alias(int argc, char **argv) {
    if (argc < 2) {
        shell_alias_list();
        return SHELL_OK;
    }
    
    if (argc < 3) {
        char *val = shell_alias_get(argv[1]);
        if (val != NULL) {
            shell_printf("alias %s='%s'\n", argv[1], val);
        }
        return SHELL_OK;
    }
    
    return shell_alias_set(argv[1], argv[2]);
}

static int cmd_unalias(int argc, char **argv) {
    if (argc < 2) {
        shell_error("unalias: missing alias name\n");
        return SHELL_ERROR;
    }
    
    return shell_alias_remove(argv[1]);
}

static int cmd_history(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        shell_history_clear();
        return SHELL_OK;
    }
    
    shell_history_list();
    return SHELL_OK;
}

static int cmd_ps(int argc, char **argv) {
    int i;
    
    shell_printf("PID\tSTATE\t\tPRI\tNAME\n");
    shell_printf("---\t-----\t\t---\t----\n");
    
    for (i = 0; i < NPROC; i++) {
        if (proctab[i].pstate != PR_FREE) {
            const char *state;
            switch (proctab[i].pstate) {
                case PR_CURR:  state = "Current"; break;
                case PR_READY: state = "Ready"; break;
                case PR_SLEEP: state = "Sleep"; break;
                case PR_SUSP:  state = "Suspended"; break;
                case PR_WAIT:  state = "Wait"; break;
                case PR_RECV:  state = "Receive"; break;
                default:       state = "Unknown"; break;
            }
            shell_printf("%d\t%s\t\t%d\t%s\n", 
                        i, state, proctab[i].pprio, proctab[i].pname);
        }
    }
    
    return SHELL_OK;
}

static int cmd_kill(int argc, char **argv) {
    pid32 pid;
    
    if (argc < 2) {
        shell_error("kill: missing process ID\n");
        return SHELL_ERROR;
    }
    
    pid = atoi(argv[1]);
    
    if (kill(pid) == SYSERR) {
        shell_error("kill: failed to kill process %d\n", pid);
        return SHELL_ERROR;
    }
    
    return SHELL_OK;
}

static int cmd_jobs(int argc, char **argv) {
    shell_jobs_list();
    return SHELL_OK;
}

static int cmd_fg(int argc, char **argv) {
    int job_id;
    
    if (argc < 2) {
        job_id = shell_job_count;
    } else {
        job_id = atoi(argv[1]);
    }
    
    shell_job_t *job = shell_job_find(job_id);
    if (job == NULL) {
        shell_error("fg: no such job\n");
        return SHELL_ERROR;
    }
    
    shell_fg(job->pid);
    return SHELL_OK;
}

static int cmd_bg(int argc, char **argv) {
    int job_id;
    
    if (argc < 2) {
        job_id = shell_job_count;
    } else {
        job_id = atoi(argv[1]);
    }
    
    shell_job_t *job = shell_job_find(job_id);
    if (job == NULL) {
        shell_error("bg: no such job\n");
        return SHELL_ERROR;
    }
    
    shell_bg(job->pid);
    shell_printf("[%d] %s &\n", job->id, job->command);
    return SHELL_OK;
}

static int cmd_mem(int argc, char **argv) {
    shell_printf("Memory Statistics:\n");
    shell_printf("  (memory statistics would go here)\n");
    return SHELL_OK;
}

static int cmd_clear(int argc, char **argv) {
    shell_printf("\033[2J\033[H");
    return SHELL_OK;
}

static int cmd_sleep(int argc, char **argv) {
    int seconds;
    
    if (argc < 2) {
        shell_error("sleep: missing operand\n");
        return SHELL_ERROR;
    }
    
    seconds = atoi(argv[1]);
    sleep(seconds * 1000);
    
    return SHELL_OK;
}

static int cmd_time(int argc, char **argv) {
    if (argc < 2) {
        shell_error("time: missing command\n");
        return SHELL_ERROR;
    }
    
    char cmd_line[SHELL_MAX_LINE] = "";
    int i;
    for (i = 1; i < argc; i++) {
        if (i > 1) strcat(cmd_line, " ");
        strcat(cmd_line, argv[i]);
    }
    
    int result = shell_execute(cmd_line);
    
    shell_printf("\n(time statistics would go here)\n");
    
    return result;
}

static int cmd_true(int argc, char **argv) {
    return SHELL_OK;
}

static int cmd_false(int argc, char **argv) {
    return SHELL_ERROR;
}

static int cmd_test(int argc, char **argv) {
    if (argc < 2) {
        return SHELL_ERROR;
    }
    
    /* Handle [ ] syntax */
    if (strcmp(argv[0], "[") == 0) {
        if (argc > 1 && strcmp(argv[argc - 1], "]") == 0) {
            argc--;
        }
    }
    
    /*  test implementation */
    if (argc == 2) {
        return strlen(argv[1]) > 0 ? SHELL_OK : SHELL_ERROR;
    }
    
    if (argc == 3) {
        if (strcmp(argv[1], "-n") == 0) {
            return strlen(argv[2]) > 0 ? SHELL_OK : SHELL_ERROR;
        } else if (strcmp(argv[1], "-z") == 0) {
            return strlen(argv[2]) == 0 ? SHELL_OK : SHELL_ERROR;
        }
    }
    
    if (argc == 4) {
        if (strcmp(argv[2], "=") == 0 || strcmp(argv[2], "==") == 0) {
            return strcmp(argv[1], argv[3]) == 0 ? SHELL_OK : SHELL_ERROR;
        } else if (strcmp(argv[2], "!=") == 0) {
            return strcmp(argv[1], argv[3]) != 0 ? SHELL_OK : SHELL_ERROR;
        } else if (strcmp(argv[2], "-eq") == 0) {
            return atoi(argv[1]) == atoi(argv[3]) ? SHELL_OK : SHELL_ERROR;
        } else if (strcmp(argv[2], "-ne") == 0) {
            return atoi(argv[1]) != atoi(argv[3]) ? SHELL_OK : SHELL_ERROR;
        } else if (strcmp(argv[2], "-lt") == 0) {
            return atoi(argv[1]) < atoi(argv[3]) ? SHELL_OK : SHELL_ERROR;
        } else if (strcmp(argv[2], "-le") == 0) {
            return atoi(argv[1]) <= atoi(argv[3]) ? SHELL_OK : SHELL_ERROR;
        } else if (strcmp(argv[2], "-gt") == 0) {
            return atoi(argv[1]) > atoi(argv[3]) ? SHELL_OK : SHELL_ERROR;
        } else if (strcmp(argv[2], "-ge") == 0) {
            return atoi(argv[1]) >= atoi(argv[3]) ? SHELL_OK : SHELL_ERROR;
        }
    }
    
    return SHELL_ERROR;
}
