/*
 * Public interface for the Xinu command shell.
 */

#ifndef _SHELL_H_
#define _SHELL_H_

#include "interpreter.h"

/*------------------------------------------------------------------------
 * Shell Configuration
 *------------------------------------------------------------------------*/

/* Shell options */
#define SHELL_OPT_ECHO      0x01    /* Echo commands */
#define SHELL_OPT_VERBOSE   0x02    /* Verbose output */
#define SHELL_OPT_ERREXIT   0x04    /* Exit on error */
#define SHELL_OPT_NOGLOB    0x08    /* Disable globbing */

/*------------------------------------------------------------------------
 * Shell Token Types
 *------------------------------------------------------------------------*/

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

/*------------------------------------------------------------------------
 * Shell Token
 *------------------------------------------------------------------------*/

typedef struct shell_token {
    shell_token_type_t  type;
    char                value[SHELL_MAX_LINE];
    int                 position;
} shell_token_t;

/*------------------------------------------------------------------------
 * Command Pipeline
 *------------------------------------------------------------------------*/

typedef struct shell_pipeline {
    char    **commands;     /* Array of command strings */
    int     num_commands;   /* Number of commands in pipeline */
    char    *input_file;    /* Input redirection file */
    char    *output_file;   /* Output redirection file */
    bool    append_output;  /* Append to output file */
    bool    background;     /* Run in background */
} shell_pipeline_t;

/*------------------------------------------------------------------------
 * Job State
 *------------------------------------------------------------------------*/

typedef enum {
    JOB_RUNNING,        /* Job is running */
    JOB_STOPPED,        /* Job is stopped */
    JOB_DONE,           /* Job completed */
    JOB_KILLED          /* Job was killed */
} job_state_t;

/*------------------------------------------------------------------------
 * Job Structure
 *------------------------------------------------------------------------*/

typedef struct shell_job {
    int         id;             /* Job ID */
    pid32       pid;            /* Process ID */
    pid32       pgid;           /* Process group ID */
    job_state_t state;          /* Job state */
    char        command[SHELL_MAX_LINE];  /* Command string */
    bool        foreground;     /* Foreground job */
} shell_job_t;

/*------------------------------------------------------------------------
 * Shell Main Functions
 *------------------------------------------------------------------------*/

/**
 * shell_start - Start the shell
 * 
 * Initializes and runs the interactive shell.
 * Does not return until shell exits.
 */
extern void shell_start(void);

/**
 * shell_process - Main shell process entry point
 * 
 * Entry point for shell when run as a process.
 */
extern void shell_process(void);

/**
 * shell_batch - Run shell in batch mode
 * 
 * @param filename: Script file to execute
 * 
 * Returns: Exit status
 */
extern int shell_batch(const char *filename);

/*------------------------------------------------------------------------
 * Command Processing
 *------------------------------------------------------------------------*/

/**
 * shell_readline - Read a line from input
 * 
 * @param buffer: Buffer to store line
 * @param size: Buffer size
 * 
 * Returns: Pointer to buffer, or NULL on EOF
 */
extern char* shell_readline(char *buffer, int size);

/**
 * shell_tokenize - Tokenize command line
 * 
 * @param line: Command line to tokenize
 * @param tokens: Array to store tokens
 * @param max_tokens: Maximum tokens
 * 
 * Returns: Number of tokens
 */
extern int shell_tokenize(const char *line, shell_token_t *tokens, 
                          int max_tokens);

/**
 * shell_expand - Expand variables and wildcards
 * 
 * @param input: Input string
 * @param output: Output buffer
 * @param size: Output buffer size
 * 
 * Returns: OK on success, SYSERR on error
 */
extern int shell_expand(const char *input, char *output, int size);

/**
 * shell_parse_pipeline - Parse command pipeline
 * 
 * @param line: Command line
 * @param pipeline: Pipeline structure to fill
 * 
 * Returns: OK on success, SYSERR on error
 */
extern int shell_parse_pipeline(const char *line, shell_pipeline_t *pipeline);

/**
 * shell_execute_pipeline - Execute command pipeline
 * 
 * @param pipeline: Pipeline to execute
 * 
 * Returns: Exit status of last command
 */
extern int shell_execute_pipeline(shell_pipeline_t *pipeline);

/*------------------------------------------------------------------------
 * Built-in Command Registration
 *------------------------------------------------------------------------*/

/**
 * shell_builtin_init - Initialize built-in commands
 * 
 * Registers all standard shell built-in commands.
 */
extern void shell_builtin_init(void);

/**
 * shell_is_builtin - Check if command is built-in
 * 
 * @param name: Command name
 * 
 * Returns: TRUE if built-in, FALSE otherwise
 */
extern bool shell_is_builtin(const char *name);

/*------------------------------------------------------------------------
 * Job Control
 *------------------------------------------------------------------------*/

/**
 * shell_job_create - Create new job
 * 
 * @param pid: Process ID
 * @param command: Command string
 * @param foreground: Is foreground job
 * 
 * Returns: Job ID
 */
extern int shell_job_create(pid32 pid, const char *command, bool foreground);

/**
 * shell_job_update - Update job state
 * 
 * @param id: Job ID
 * @param state: New state
 */
extern void shell_job_update(int id, job_state_t state);

/**
 * shell_job_find - Find job by ID
 * 
 * @param id: Job ID
 * 
 * Returns: Pointer to job, or NULL if not found
 */
extern shell_job_t* shell_job_find(int id);

/**
 * shell_job_find_by_pid - Find job by process ID
 * 
 * @param pid: Process ID
 * 
 * Returns: Pointer to job, or NULL if not found
 */
extern shell_job_t* shell_job_find_by_pid(pid32 pid);

/**
 * shell_wait_job - Wait for job to complete
 * 
 * @param id: Job ID
 * 
 * Returns: Job exit status
 */
extern int shell_wait_job(int id);

#endif
