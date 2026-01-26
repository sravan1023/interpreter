#ifndef _INTERPRETER_H_
#define _INTERPRETER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef XINU_KERNEL
#include "../include/types.h"
#else
/* Standalone type definitions */
typedef int32_t     pid32;
typedef int32_t     status;
#define OK          1 
#define SYSERR      -1
#endif


#define SHELL_MAX_LINE      256 
#define SHELL_MAX_ARGS      32 
#define SHELL_MAX_CMD       64
#define SHELL_MAX_PATH      256 
#define SHELL_HISTORY_SIZE  50 
#define SHELL_MAX_ALIAS     32


#define SHELL_PROMPT        "xinu$ "
#define SHELL_ROOT_PROMPT   "xinu# "



#define SHELL_OK            0
#define SHELL_ERROR         1
#define SHELL_EXIT          -1
#define SHELL_NOT_FOUND     127


typedef int (*shell_cmd_func)(int argc, char **argv);

typedef struct shell_command {
    char            name[SHELL_MAX_CMD];
    char            description[128];
    shell_cmd_func  func;
    bool            builtin;
} shell_command_t;


typedef struct shell_alias {
    char    name[SHELL_MAX_CMD];
    char    value[SHELL_MAX_LINE]; 
    bool    defined; 
} shell_alias_t;


typedef struct history_entry {
    char        command[SHELL_MAX_LINE];
    int32_t     number;
    uint32_t    timestamp;
} history_entry_t;


typedef struct shell_state {
    char            cwd[SHELL_MAX_PATH];
    int32_t         last_exit;
    pid32           pid;                    
    bool            interactive;            
    bool            running;                
    /* History */
    history_entry_t history[SHELL_HISTORY_SIZE];
    int32_t         history_count;
    int32_t         history_index;
    
    /* Aliases */
    shell_alias_t   aliases[SHELL_MAX_ALIAS];
    int32_t         alias_count;
    
    /* Environment */
    char            **env;
    int32_t         env_count;
} shell_state_t;


#define SCRIPT_MAX_VARS     128
#define SCRIPT_MAX_FUNCS    64
#define SCRIPT_MAX_STACK    256
#define SCRIPT_VAR_NAME_LEN 64
#define SCRIPT_VAR_VAL_LEN  256
#define SCRIPT_MAX_LINE     512
#define SCRIPT_MAX_LABELS   64

typedef enum {
    VAR_TYPE_INT,
    VAR_TYPE_STRING,
    VAR_TYPE_FLOAT,
    VAR_TYPE_ARRAY,
    VAR_TYPE_UNDEFINED
} var_type_t;

typedef struct script_var {
    char        name[SCRIPT_VAR_NAME_LEN];
    var_type_t  type;
    union {
        int32_t     int_val;
        double      float_val;
        char        str_val[SCRIPT_VAR_VAL_LEN];
        void        *array_val;
    } value;
    bool        readonly;
    bool        exported;
    bool        defined;
} script_var_t;


typedef struct script_func {
    char        name[SCRIPT_VAR_NAME_LEN];
    char        *body;
    int32_t     body_len;
    int32_t     num_params;
    bool        defined;
} script_func_t;


typedef struct script_label {
    char        name[SCRIPT_VAR_NAME_LEN];
    int32_t     line_num;
    bool        defined;
} script_label_t;

typedef struct script_context {
    script_var_t    vars[SCRIPT_MAX_VARS];
    script_func_t   funcs[SCRIPT_MAX_FUNCS];
    script_label_t  labels[SCRIPT_MAX_LABELS];
    
    int32_t         var_count;
    int32_t         func_count;
    int32_t         label_count;
    
    /* Execution state */
    int32_t         line_num;
    bool            running;
    int32_t         exit_code;
    int32_t         loop_stack[SCRIPT_MAX_STACK];
    int32_t         loop_sp;
    int32_t         call_stack[SCRIPT_MAX_STACK];
    int32_t         call_sp;
    int32_t         stdin_fd;
    int32_t         stdout_fd;
    int32_t         stderr_fd;
} script_context_t;

/* Shell lifecycle */
extern void     shell_init(void);
extern void     shell_run(void);
extern void     shell_exit(int status);

/* Command execution */
extern int      shell_execute(const char *line);
extern int      shell_execute_file(const char *filename);
extern int      shell_parse_line(char *line, char **argv, int max_args);

/* Built-in commands */
extern int      shell_register_command(const char *name, const char *desc, shell_cmd_func func);
extern int      shell_unregister_command(const char *name);
extern shell_command_t* shell_find_command(const char *name);

/* History */
extern void     shell_history_add(const char *cmd);
extern char*    shell_history_get(int index);
extern void     shell_history_clear(void);
extern void     shell_history_list(void);

/* Aliases */
extern int      shell_alias_set(const char *name, const char *value);
extern char*    shell_alias_get(const char *name);
extern int      shell_alias_remove(const char *name);
extern void     shell_alias_list(void);

/* Environment */
extern char*    shell_getenv(const char *name);
extern int      shell_setenv(const char *name, const char *value);
extern int      shell_unsetenv(const char *name);

/* I/O redirection */
extern int      shell_redirect_input(const char *filename);
extern int      shell_redirect_output(const char *filename, bool append);

/* Piping */
extern int      shell_pipe(const char *cmd1, const char *cmd2);

/* Job control */
extern int      shell_bg(pid32 pid);
extern int      shell_fg(pid32 pid);
extern void     shell_jobs_list(void);


/* Interpreter lifecycle */
extern script_context_t* script_create_context(void);
extern void     script_destroy_context(script_context_t *ctx);
extern void     script_reset_context(script_context_t *ctx);

/* Execution */
extern int      script_execute(script_context_t *ctx, const char *script);
extern int      script_execute_file(script_context_t *ctx, const char *filename);
extern int      script_execute_line(script_context_t *ctx, const char *line);

/* Variables */
extern int      script_set_var(script_context_t *ctx, const char *name, var_type_t type, void *value);
extern int      script_get_var(script_context_t *ctx, const char *name, var_type_t *type, void *value);
extern int      script_unset_var(script_context_t *ctx, const char *name);
extern bool     script_var_exists(script_context_t *ctx, const char *name);

/* Functions */
extern int      script_define_func(script_context_t *ctx, const char *name, const char *body, int num_params);
extern int      script_call_func(script_context_t *ctx, const char *name, int argc, char **argv);
extern int32_t  script_eval_int(script_context_t *ctx, const char *expr);
extern double   script_eval_float(script_context_t *ctx, const char *expr);
extern char*    script_eval_string(script_context_t *ctx, const char *expr);
extern bool     script_eval_bool(script_context_t *ctx, const char *expr);
extern int      script_goto_label(script_context_t *ctx, const char *label);
extern int      script_break(script_context_t *ctx);
extern int      script_continue(script_context_t *ctx);
extern int      script_return(script_context_t *ctx, int value);
extern int32_t  expr_eval_arithmetic(const char *expr);
extern double   expr_eval_float(const char *expr);
extern char*    expr_eval_string_expr(const char *expr);
extern bool     expr_eval_condition(const char *expr);
extern bool     expr_match_glob(const char *pattern, const char *string);
extern bool     expr_match_regex(const char *pattern, const char *string);

#endif
