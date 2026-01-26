#if !defined(_POSIX_C_SOURCE) && !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "interpreter.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

#ifdef XINU_KERNEL
#include "../include/kernel.h"
#include "../include/memory.h"
#include "../include/types.h"


#else

#define getmem(size)        malloc(size)
#define freemem(ptr, size)  free(ptr)
#endif

#ifdef _WIN32
#ifndef strtok_r
#define strtok_r(str, delim, saveptr) strtok_s(str, delim, saveptr)
#endif
#endif


script_context_t* script_create_context(void) {
    script_context_t *ctx = (script_context_t*)getmem(sizeof(script_context_t));
    
    if (ctx == NULL) {
        return NULL;
    }
    
    script_reset_context(ctx);
    
    return ctx;
}

void script_destroy_context(script_context_t *ctx) {
    int i;
    
    if (ctx == NULL) {
        return;
    }
    
    /* Free function bodies */
    for (i = 0; i < ctx->func_count; i++) {
        if (ctx->funcs[i].body != NULL) {
            freemem(ctx->funcs[i].body, ctx->funcs[i].body_len);
        }
    }
    
    freemem(ctx, sizeof(script_context_t));
}

void script_reset_context(script_context_t *ctx) {
    int i;
    
    if (ctx == NULL) {
        return;
    }
    
    /* Reset variables */
    for (i = 0; i < SCRIPT_MAX_VARS; i++) {
        ctx->vars[i].defined = false;
        ctx->vars[i].type = VAR_TYPE_UNDEFINED;
        ctx->vars[i].readonly = false;
        ctx->vars[i].exported = false;
    }
    ctx->var_count = 0;
    
    /* Reset functions */
    for (i = 0; i < SCRIPT_MAX_FUNCS; i++) {
        if (ctx->funcs[i].body != NULL) {
            freemem(ctx->funcs[i].body, ctx->funcs[i].body_len);
        }
        ctx->funcs[i].defined = false;
        ctx->funcs[i].body = NULL;
    }
    ctx->func_count = 0;
    
    /* Reset labels */
    for (i = 0; i < SCRIPT_MAX_LABELS; i++) {
        ctx->labels[i].defined = false;
    }
    ctx->label_count = 0;
    
    /* Reset execution state */
    ctx->line_num = 0;
    ctx->running = false;
    ctx->exit_code = 0;
    ctx->loop_sp = 0;
    ctx->call_sp = 0;
    
    /* Set default I/O */
    ctx->stdin_fd = 0;
    ctx->stdout_fd = 1;
    ctx->stderr_fd = 2;
}


static script_var_t* find_var(script_context_t *ctx, const char *name) {
    int i;
    
    for (i = 0; i < SCRIPT_MAX_VARS; i++) {
        if (ctx->vars[i].defined && 
            strcmp(ctx->vars[i].name, name) == 0) {
            return &ctx->vars[i];
        }
    }
    
    return NULL;
}

static script_var_t* create_var(script_context_t *ctx, const char *name) {
    int i;
    
    /* Check if already exists */
    script_var_t *var = find_var(ctx, name);
    if (var != NULL) {
        return var;
    }
    
    /* Find empty slot */
    for (i = 0; i < SCRIPT_MAX_VARS; i++) {
        if (!ctx->vars[i].defined) {
            ctx->vars[i].defined = true;
            strncpy(ctx->vars[i].name, name, SCRIPT_VAR_NAME_LEN - 1);
            ctx->vars[i].name[SCRIPT_VAR_NAME_LEN - 1] = '\0';
            ctx->vars[i].type = VAR_TYPE_UNDEFINED;
            ctx->vars[i].readonly = false;
            ctx->vars[i].exported = false;
            ctx->var_count++;
            return &ctx->vars[i];
        }
    }
    
    return NULL;
}

int script_set_var(script_context_t *ctx, const char *name,
                   var_type_t type, void *value) {
    script_var_t *var;
    
    if (ctx == NULL || name == NULL || value == NULL) {
        return SYSERR;
    }
    
    var = create_var(ctx, name);
    if (var == NULL) {
        return SYSERR;
    }
    
    if (var->readonly) {
        return SYSERR;  /* Cannot modify readonly variable */
    }
    
    var->type = type;
    
    switch (type) {
        case VAR_TYPE_INT:
            var->value.int_val = *(int32_t*)value;
            break;
            
        case VAR_TYPE_FLOAT:
            var->value.float_val = *(double*)value;
            break;
            
        case VAR_TYPE_STRING:
            strncpy(var->value.str_val, (char*)value, SCRIPT_VAR_VAL_LEN - 1);
            var->value.str_val[SCRIPT_VAR_VAL_LEN - 1] = '\0';
            break;
            
        case VAR_TYPE_ARRAY:
            var->value.array_val = value;
            break;
            
        default:
            return SYSERR;
    }
    
    return OK;
}

int script_get_var(script_context_t *ctx, const char *name,
                   var_type_t *type, void *value) {
    script_var_t *var;
    
    if (ctx == NULL || name == NULL) {
        return SYSERR;
    }
    
    var = find_var(ctx, name);
    if (var == NULL) {
        return SYSERR;
    }
    
    if (type != NULL) {
        *type = var->type;
    }
    
    if (value != NULL) {
        switch (var->type) {
            case VAR_TYPE_INT:
                *(int32_t*)value = var->value.int_val;
                break;
                
            case VAR_TYPE_FLOAT:
                *(double*)value = var->value.float_val;
                break;
                
            case VAR_TYPE_STRING:
                strcpy((char*)value, var->value.str_val);
                break;
                
            case VAR_TYPE_ARRAY:
                *(void**)value = var->value.array_val;
                break;
                
            default:
                return SYSERR;
        }
    }
    
    return OK;
}

int script_unset_var(script_context_t *ctx, const char *name) {
    script_var_t *var;
    
    if (ctx == NULL || name == NULL) {
        return SYSERR;
    }
    
    var = find_var(ctx, name);
    if (var == NULL) {
        return SYSERR;
    }
    
    if (var->readonly) {
        return SYSERR;
    }
    
    var->defined = false;
    ctx->var_count--;
    
    return OK;
}

bool script_var_exists(script_context_t *ctx, const char *name) {
    return find_var(ctx, name) != NULL;
}


static script_func_t* find_func(script_context_t *ctx, const char *name) {
    int i;
    
    for (i = 0; i < SCRIPT_MAX_FUNCS; i++) {
        if (ctx->funcs[i].defined && 
            strcmp(ctx->funcs[i].name, name) == 0) {
            return &ctx->funcs[i];
        }
    }
    
    return NULL;
}

int script_define_func(script_context_t *ctx, const char *name,
                       const char *body, int num_params) {
    int i;
    script_func_t *func;
    
    if (ctx == NULL || name == NULL || body == NULL) {
        return SYSERR;
    }
    
    /* Check if already exists */
    func = find_func(ctx, name);
    if (func != NULL) {
        /* Free old body */
        if (func->body != NULL) {
            freemem(func->body, func->body_len);
        }
    } else {
        /* Find empty slot */
        for (i = 0; i < SCRIPT_MAX_FUNCS; i++) {
            if (!ctx->funcs[i].defined) {
                func = &ctx->funcs[i];
                func->defined = true;
                strncpy(func->name, name, SCRIPT_VAR_NAME_LEN - 1);
                func->name[SCRIPT_VAR_NAME_LEN - 1] = '\0';
                ctx->func_count++;
                break;
            }
        }
        
        if (func == NULL) {
            return SYSERR;
        }
    }
    
    /* Allocate and copy body */
    func->body_len = strlen(body) + 1;
    func->body = (char*)getmem(func->body_len);
    if (func->body == NULL) {
        func->defined = false;
        return SYSERR;
    }
    
    strcpy(func->body, body);
    func->num_params = num_params;
    
    return OK;
}

int script_call_func(script_context_t *ctx, const char *name,
                     int argc, char **argv) {
    script_func_t *func;
    int i;
    char param_name[32];
    
    if (ctx == NULL || name == NULL) {
        return SYSERR;
    }
    
    func = find_func(ctx, name);
    if (func == NULL) {
        return SYSERR;
    }
    
    /* Push call frame */
    if (ctx->call_sp >= SCRIPT_MAX_STACK) {
        return SYSERR;  /* Stack overflow */
    }
    ctx->call_stack[ctx->call_sp++] = ctx->line_num;
    
    /* Set parameters as variables */
    for (i = 0; i < func->num_params && i < argc; i++) {
        snprintf(param_name, sizeof(param_name), "arg%d", i);
        script_set_var(ctx, param_name, VAR_TYPE_STRING, argv[i]);
    }
    
    /* Execute function body */
    int result = script_execute(ctx, func->body);
    
    /* Pop call frame */
    if (ctx->call_sp > 0) {
        ctx->line_num = ctx->call_stack[--ctx->call_sp];
    }
    
    return result;
}


/* Simple integer expression evaluator */
int32_t script_eval_int(script_context_t *ctx, const char *expr) {
    const char *p = expr;
    int32_t result = 0;
    bool negative = false;
    
    /* Skip whitespace */
    while (*p == ' ' || *p == '\t') p++;
    
    /* Check for negative */
    if (*p == '-') {
        negative = true;
        p++;
    } else if (*p == '+') {
        p++;
    }
    
    /* Check for variable reference */
    if (*p == '$') {
        p++;
        char var_name[64];
        int i = 0;
        
        while (*p != '\0' && (isalnum(*p) || *p == '_') && i < 63) {
            var_name[i++] = *p++;
        }
        var_name[i] = '\0';
        
        var_type_t type;
        if (script_get_var(ctx, var_name, &type, &result) == OK) {
            if (type == VAR_TYPE_INT) {
                return negative ? -result : result;
            }
        }
        return 0;
    }
    
    /* Parse number */
    if (*p == '0' && (*(p + 1) == 'x' || *(p + 1) == 'X')) {
        /* Hexadecimal */
        p += 2;
        while (isxdigit(*p)) {
            result = result * 16;
            if (*p >= '0' && *p <= '9') {
                result += *p - '0';
            } else if (*p >= 'a' && *p <= 'f') {
                result += *p - 'a' + 10;
            } else if (*p >= 'A' && *p <= 'F') {
                result += *p - 'A' + 10;
            }
            p++;
        }
    } else if (*p == '0' && *(p + 1) >= '0' && *(p + 1) <= '7') {
        /* Octal */
        p++;
        while (*p >= '0' && *p <= '7') {
            result = result * 8 + (*p - '0');
            p++;
        }
    } else {
        /* Decimal */
        while (*p >= '0' && *p <= '9') {
            result = result * 10 + (*p - '0');
            p++;
        }
    }
    
    return negative ? -result : result;
}

double script_eval_float(script_context_t *ctx, const char *expr) {
    return (double)atof(expr);
}

char* script_eval_string(script_context_t *ctx, const char *expr) {
    static char result[SCRIPT_VAR_VAL_LEN];
    
    /* Simple string evaluation - would need expansion for variables */
    strncpy(result, expr, SCRIPT_VAR_VAL_LEN - 1);
    result[SCRIPT_VAR_VAL_LEN - 1] = '\0';
    
    return result;
}

bool script_eval_bool(script_context_t *ctx, const char *expr) {
    const char *p = expr;
    
    /* Skip whitespace */
    while (*p == ' ' || *p == '\t') p++;
    
    /* Empty string is false */
    if (*p == '\0') {
        return false;
    }
    
    /* Check for boolean keywords */
    if (strcmp(p, "true") == 0 || strcmp(p, "TRUE") == 0 || 
        strcmp(p, "1") == 0) {
        return true;
    }
    
    if (strcmp(p, "false") == 0 || strcmp(p, "FALSE") == 0 || 
        strcmp(p, "0") == 0) {
        return false;
    }
    
    /* Numeric comparison */
    int32_t val = script_eval_int(ctx, expr);
    return val != 0;
}


static script_label_t* find_label(script_context_t *ctx, const char *name) {
    int i;
    
    for (i = 0; i < SCRIPT_MAX_LABELS; i++) {
        if (ctx->labels[i].defined && 
            strcmp(ctx->labels[i].name, name) == 0) {
            return &ctx->labels[i];
        }
    }
    
    return NULL;
}

static int create_label(script_context_t *ctx, const char *name, int line_num) {
    int i;
    
    /* Check if already exists */
    script_label_t *label = find_label(ctx, name);
    if (label != NULL) {
        label->line_num = line_num;
        return OK;
    }
    
    /* Find empty slot */
    for (i = 0; i < SCRIPT_MAX_LABELS; i++) {
        if (!ctx->labels[i].defined) {
            ctx->labels[i].defined = true;
            strncpy(ctx->labels[i].name, name, SCRIPT_VAR_NAME_LEN - 1);
            ctx->labels[i].name[SCRIPT_VAR_NAME_LEN - 1] = '\0';
            ctx->labels[i].line_num = line_num;
            ctx->label_count++;
            return OK;
        }
    }
    
    return SYSERR;
}

int script_goto_label(script_context_t *ctx, const char *label) {
    script_label_t *lbl;
    
    if (ctx == NULL || label == NULL) {
        return SYSERR;
    }
    
    lbl = find_label(ctx, label);
    if (lbl == NULL) {
        return SYSERR;
    }
    
    ctx->line_num = lbl->line_num;
    return OK;
}

int script_break(script_context_t *ctx) {
    if (ctx == NULL || ctx->loop_sp == 0) {
        return SYSERR;
    }
    
    /* Jump to end of current loop */
    ctx->line_num = ctx->loop_stack[ctx->loop_sp - 1];
    
    return OK;
}

int script_continue(script_context_t *ctx) {
    if (ctx == NULL || ctx->loop_sp == 0) {
        return SYSERR;
    }
    
    /* Jump to beginning of current loop */
    ctx->line_num = ctx->loop_stack[ctx->loop_sp - 1] - 1;
    
    return OK;
}

int script_return(script_context_t *ctx, int value) {
    if (ctx == NULL) {
        return SYSERR;
    }
    
    ctx->exit_code = value;
    ctx->running = false;
    
    return OK;
}


static int execute_line(script_context_t *ctx, const char *line) {
    char *p = (char*)line;
    char *token;
    
    while (*p == ' ' || *p == '\t') p++;
    
    /* Skip empty lines and comments */
    if (*p == '\0' || *p == '#') {
        return OK;
    }
    
    /* Check for label definition */
    char *colon = strchr(p, ':');
    if (colon != NULL && colon > p) {
        char label[SCRIPT_VAR_NAME_LEN];
        int len = colon - p;
        if (len < SCRIPT_VAR_NAME_LEN) {
            strncpy(label, p, len);
            label[len] = '\0';
            create_label(ctx, label, ctx->line_num);
        }
        p = colon + 1;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') {
            return OK;
        }
    }
    
    /* Check for assignment */
    char *equals = strchr(p, '=');
    if (equals != NULL && equals > p && *(equals - 1) != '!' && 
        *(equals - 1) != '<' && *(equals - 1) != '>') {
        char var_name[SCRIPT_VAR_NAME_LEN];
        int len = equals - p;
        
        /* Get variable name */
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
        if (len < SCRIPT_VAR_NAME_LEN) {
            strncpy(var_name, p, len);
            var_name[len] = '\0';
            
            /* Get value */
            p = equals + 1;
            while (*p == ' ' || *p == '\t') p++;
            
            /* Determine type and set variable */
            if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+') {
                /* Numeric */
                int32_t val = script_eval_int(ctx, p);
                script_set_var(ctx, var_name, VAR_TYPE_INT, &val);
            } else {
                /* String */
                script_set_var(ctx, var_name, VAR_TYPE_STRING, p);
            }
            
            return OK;
        }
    }
    
    /* Check for control flow keywords */
    if (strncmp(p, "if ", 3) == 0) {
        /* if statement - simplified */
        p += 3;
        bool condition = script_eval_bool(ctx, p);
        /* Would need to track if/else blocks */
        return OK;
    } else if (strncmp(p, "while ", 6) == 0) {
        /* while loop - simplified */
        p += 6;
        bool condition = script_eval_bool(ctx, p);
        /* Would need to track loop */
        return OK;
    } else if (strncmp(p, "for ", 4) == 0) {
        /* for loop - simplified */
        return OK;
    } else if (strcmp(p, "break") == 0) {
        return script_break(ctx);
    } else if (strcmp(p, "continue") == 0) {
        return script_continue(ctx);
    } else if (strncmp(p, "return", 6) == 0) {
        p += 6;
        while (*p == ' ' || *p == '\t') p++;
        int val = *p != '\0' ? script_eval_int(ctx, p) : 0;
        return script_return(ctx, val);
    } else if (strncmp(p, "goto ", 5) == 0) {
        p += 5;
        while (*p == ' ' || *p == '\t') p++;
        return script_goto_label(ctx, p);
    }
    
    /* Otherwise treat as expression evaluation */
    script_eval_int(ctx, p);
    
    return OK;
}

int script_execute_line(script_context_t *ctx, const char *line) {
    if (ctx == NULL || line == NULL) {
        return SYSERR;
    }
    
    return execute_line(ctx, line);
}

int script_execute(script_context_t *ctx, const char *script) {
    char *line;
    char *script_copy;
    char *saveptr;
    int result = OK;
    
    if (ctx == NULL || script == NULL) {
        return SYSERR;
    }
    
    /* Make a copy of the script */
    size_t len = strlen(script) + 1;
    script_copy = (char*)getmem((uint32_t)len);
    if (script_copy == NULL) {
        return SYSERR;
    }
    strcpy(script_copy, script);
    
    ctx->running = true;
    ctx->line_num = 0;
    
    /* Execute line by line */
    line = strtok_r(script_copy, "\n", &saveptr);
    while (line != NULL && ctx->running) {
        ctx->line_num++;
        result = execute_line(ctx, line);
        
        if (result != OK) {
            break;
        }
        
        line = strtok_r(NULL, "\n", &saveptr);
    }
    
    freemem(script_copy, (uint32_t)len);
    ctx->running = false;
    
    return ctx->exit_code;
}

int script_execute_file(script_context_t *ctx, const char *filename) {

    return SYSERR;
}


int32_t expr_eval_arithmetic(const char *expr) {
    /* Simple arithmetic evaluator */
    return atoi(expr);
}

double expr_eval_float(const char *expr) {
    return atof(expr);
}

char* expr_eval_string_expr(const char *expr) {
    static char result[256];
    strncpy(result, expr, 255);
    result[255] = '\0';
    return result;
}

bool expr_eval_condition(const char *expr) {
    /* Simple condition evaluator */
    return expr != NULL && *expr != '\0' && strcmp(expr, "0") != 0;
}

bool expr_match_glob(const char *pattern, const char *string) {
    /* Simple glob pattern matching */
    const char *p = pattern;
    const char *s = string;
    
    while (*p != '\0' && *s != '\0') {
        if (*p == '*') {
            /* Match zero or more characters */
            p++;
            if (*p == '\0') {
                return true;  /* * at end matches rest */
            }
            while (*s != '\0') {
                if (expr_match_glob(p, s)) {
                    return true;
                }
                s++;
            }
            return false;
        } else if (*p == '?') {
            /* Match any single character */
            p++;
            s++;
        } else if (*p == *s) {
            p++;
            s++;
        } else {
            return false;
        }
    }
    
    while (*p == '*') p++;
    
    return *p == '\0' && *s == '\0';
}

bool expr_match_regex(const char *pattern, const char *string) {
    return expr_match_glob(pattern, string);
}

