#ifndef _SHELL_H_
#define _SHELL_H_

#include "interpreter.h"

/* Shell Configuration */

/* Shell options */
#define SHELL_OPT_ECHO      0x01    /* Echo commands */
#define SHELL_OPT_VERBOSE   0x02    /* Verbose output */
#define SHELL_OPT_ERREXIT   0x04    /* Exit on error */
#define SHELL_OPT_NOGLOB    0x08    /* Disable globbing */

/* Shell Token Types */

typedef enum {
    TOK_WORD,           /* Regular word/argument */
    TOK_PIPE,           /* | */
    TOK_REDIR_IN,       /* < */
    TOK_REDIR_OUT,      /* > */
    TOK_REDIR_APPEND,   /* >> */
    TOK_REDIR_ERR,      /* 2> */
    TOK_BACKGROUND,     /* & */
    TOK_SEMICOLON,      /* ; */
    TOK_AND,            /* && */
    TOK_OR,             /* || */
    TOK_LPAREN,         /* ( */
    TOK_RPAREN,         /* ) */
    TOK_NEWLINE,        /* End of line */
    TOK_EOF             /* End of input */
} shell_token_type_t;

/* Shell Token */

typedef struct shell_token {
    shell_token_type_t  type;
    char                value[SHELL_MAX_LINE];
    int                 position;
} shell_token_t;

/* Command Pipeline */

typedef struct shell_pipeline {
    char    **commands;     /* Array of command strings */
    int     num_commands;   /* Number of commands in pipeline */
    char    *input_file;    /* Input redirection file */
    char    *output_file;   /* Output redirection file */
    bool    append_output;  /* Append to output file */
    bool    background;     /* Run in background */
} shell_pipeline_t;

/* Job State */

typedef enum {
    JOB_RUNNING,        /* Job is running */
    JOB_STOPPED,        /* Job is stopped */
    JOB_DONE,           /* Job completed */
    JOB_KILLED          /* Job was killed */
} job_state_t;

/* Job Structure */

typedef struct shell_job {
    int         id;             /* Job ID */
    pid32       pid;            /* Process ID */
    pid32       pgid;           /* Process group ID */
    job_state_t state;          /* Job state */
    char        command[SHELL_MAX_LINE];  /* Command string */
    bool        foreground;     /* Foreground job */
} shell_job_t;

/* Shell Main Functions */
extern void shell_start(void);
extern void shell_process(void);
extern int shell_batch(const char *filename);

/* Command Processing */
extern char* shell_readline(char *buffer, int size);
extern int shell_tokenize(const char *line, shell_token_t *tokens, int max_tokens);
extern int shell_expand(const char *input, char *output, int size);
extern int shell_parse_pipeline(const char *line, shell_pipeline_t *pipeline);
extern int shell_execute_pipeline(shell_pipeline_t *pipeline);

/* Built-in Command Registration */
extern void shell_builtin_init(void);
extern bool shell_is_builtin(const char *name);

/* Job Control */
extern int shell_job_create(pid32 pid, const char *command, bool foreground);
extern void shell_job_update(int id, job_state_t state);
extern shell_job_t* shell_job_find(int id);
extern shell_job_t* shell_job_find_by_pid(pid32 pid);
extern int shell_wait_job(int id);

#endif
