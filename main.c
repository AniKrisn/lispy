#include <stdio.h>
#include <stdlib.h>
#include "mpc.h"

#ifdef _WIN32
#include <string.h>


static char buffer[2048];

char* readline(char* prompt) {
    fputs(prompt, stdout);
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return NULL;  // Handle EOF or error
    }
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len-1] == '\n') {
        buffer[len-1] = '\0';  // Remove newline in-place
    }
    return strdup(buffer);  // Create a new copy and return it
}


void add_history(char* unused) {}


#else 
#include <editline/readline.h>
#include <editline/history.h>
#endif

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

// possible lval types
enum { LVAL_ERR, LVAL_NUM, LVAL_SYM,
       LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR };

typedef lval*(*lbuiltin)(lenv*, lval*);

// def "lisp value" -- 
struct lval {
    int type;
    long num;
    // err and symbol types have string data
    char* err;
    char* sym;
    lbuiltin builtin;   
    lenv* env;
    lval* formals;
    lval* body;
    // count and point to a list of "lval*"
    int count;
    lval** cell;
};
  

// init all types with constructor functions

lval* lval_num (long x) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

lval* lval_err(char* fmt, ...) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_ERR;
    
    va_list va;
    va_start(va, fmt);

    v->err = malloc(512);

    vsnprintf(v->err, 511, fmt, va);
    v->err = realloc(v->err, strlen(v->err) + 1);

    va_end(va);

    return v;
}

char* ltype_name(int t) {
    switch(t) {
        case LVAL_FUN: return "Function";
        case LVAL_NUM: return "Number";
        case LVAL_ERR: return "Error";
        case LVAL_SYM: return "Symbol";
        case LVAL_SEXPR: return "S-Expression";
        case LVAL_QEXPR: return "Q-Expression";
        default: return "Unknown";
    }
}

lval* lval_sym(char* s) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = malloc(strlen(s) + 1);
    strcpy(v->sym, s);
    return v;
}

lval* lval_fun(lbuiltin func) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;
    v->builtin = func;
    return v;
}

lval* lval_sexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
} 

lval* lval_qexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

lenv* lenv_new(void);

lval* lval_lambda(lval* formals, lval* body) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;

    v->builtin = NULL;

    v->env = lenv_new();

    v->formals = formals;
    v->body = body;
    return v;
}

// destructor for lval, frees the memory used by the lval after used
void lenv_del(lenv* e);
void lval_del(lval* v) {
    switch (v->type) {
        // for nums the type is long so nothing special
        case LVAL_NUM: break;
        // err and sym are strings so freeing is straightforward
        case LVAL_ERR: free(v->err); break;
        case LVAL_SYM: free(v->sym); break;
        // sexprs are lists so we need to free each element and then the mem used to store the pointers
        case LVAL_QEXPR:
        case LVAL_SEXPR:
            for (int i = 0; i < v->count; i++) {
                lval_del(v->cell[i]);
            }
            free(v->cell);
        break;

        case LVAL_FUN:
            if (!v->builtin) {
                lenv_del(v->env);
                lval_del(v->formals);
                lval_del(v->body);
            }
        break;
    }
    // free the mem used to store the lval struct
    free(v);
}


lval* lval_add(lval* v, lval* x) {
    v->count++;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    v->cell[v->count-1] = x;
    return v;
}

// pops out ith value and shifts rest upwards
lval* lval_pop(lval* v, int i) {
    lval* x = v->cell[i];

    // use memmove here instead of memcopy in case destination and source overlap. Remember- params: destination, source, size.
    memmove(&v->cell[i], &v->cell[i+1],
        sizeof(lval*) * (v->count-i-1));

    // reduce count and reallocate memory of popped value
    v->count--;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);

    // return the popped value
    return x;
}

// take is like pop but it deletes the rest of the array
lval* lval_take(lval* v, int i) {
    lval* x = lval_pop(v, i);
    lval_del(v);
    return x;
}

lenv* lenv_copy(lenv* e);
lval* lval_copy(lval* v) {
    lval* x = malloc(sizeof(lval));
    x->type = v->type;

    switch (v->type) {
        case LVAL_NUM: x->num = v->num; break;

        case LVAL_ERR:
            x->err = malloc(strlen(v->err)+1);
            strcpy(x->err, v->err); break;

        case LVAL_SYM:
            x->sym = malloc(strlen(v->sym)+1);
            strcpy(x->sym, v->sym); break;

        case LVAL_SEXPR:
        case LVAL_QEXPR:
            x->count = v->count;
            x->cell = malloc(sizeof(lval*) * x->count);
            for (int i=0; i < x->count; i++) {
                x->cell[i] = lval_copy(v->cell[i]);
            }
        break;

        case LVAL_FUN:
            if (v->builtin) {
                x->builtin = v->builtin;
            } else {
                x->builtin = NULL;
                x->env = lenv_copy(v->env);
                x->formals = lval_copy(v->formals);
                x->body = lval_copy(v->body);
            }
        break;
    } 
    return x;
}



/* Lisp Environment */

struct lenv {
    int count;
    char** syms;
    lval** vals;
};

lenv* lenv_new(void) {
    lenv* e = malloc(sizeof(lenv));
    e->count = 0;
    e->syms = NULL;
    e->vals = NULL;
    return e;
}

void lenv_del(lenv* e) {
    for (int i=0; i < e->count; i++) {
        free(e->syms[i]);
        lval_del(e->vals[i]); // del because vals is an lval struct. del frees for all cases; using free would lead to potential memory leaks
    }
    free(e->syms);
    free(e->vals);
    free(e);
}

lval* lenv_get(lenv* e, lval* k) {

    for (int i=0; i < e->count; i++) {
        if (strcmp(e->syms[i], k->sym) == 0) {
            return lval_copy(e->vals[i]);
        }
    }

    // if sym not found return error
    return lval_err("unbound symbol!");

}

void lenv_put(lenv* e, lval* k, lval* v) {

    // iterate through all elements in the env, see if the var already exists
    for (int i=0; i < e->count; i++) {

        // if the var exists, delete the val in that position and replace with new
        if (strcmp(e->syms[i], k->sym) == 0){
            lval_del(e->vals[i]);
            e->vals[i] = lval_copy(v);
            return;
        }
    }

    // else if var doesn't exist, add space for it
    e->count++;
    e->vals = realloc(e->vals, sizeof(lval*) * e->count);
    e->syms = realloc(e->syms, sizeof(char*) * e->count);

    // copy contents of lval and symbol string into new location
    e->vals[e->count-1] = lval_copy(v);
    e->syms[e->count-1] = malloc(strlen(k->sym)+1);
    strcpy(e->syms[e->count-1], k->sym);
}


// forward declarations
lval* lval_eval(lenv* e, lval* v);
lval* builtin_op(lenv* e, lval* a, char* op);
lval* builtin(lenv* e, lval* a, char* func);

/* Builtins */

#define LASSERT(args, cond, fmt, ...) \
    if (!(cond)) {lval* err = lval_err(fmt, ##__VA_ARGS__); \
    lval_del(args); return err;} 

#define LASSERT_TYPE(args, func, expected) \
    LASSERT(args, args->cell[0]->type == expected, \
    "Function '%s' passed incorrect type. Got %s, expected %s.", func, ltype_name(args->cell[0]->type), ltype_name(expected))

#define LASSERT_NUM(args, func, expected) \
    LASSERT(args, args->count == expected, \
    "Function '%s' passed incorrect type. Got %s, expected %s.", func, args->count, expected) 

#define LASSERT_NOT_EMPTY(args, func) \
    LASSERT(args, args->cell[0]->count != 0, "Function '%s' passed {}", func)


lval* builtin_head(lenv* e, lval* a) {
    LASSERT_NUM(a, "head", 1);
    LASSERT_TYPE(a, "head", LVAL_QEXPR);
    LASSERT_NOT_EMPTY(a, "head");

    lval *v = lval_take(a, 0);
    while (v->count > 1) { lval_del(lval_pop(v, 1)); }
    return v;
}

lval* builtin_tail(lenv* e, lval* a) {
    LASSERT_NUM(a, "tail", 1);
    LASSERT_NOT_EMPTY(a, "tail");
    LASSERT_TYPE(a, "tail", LVAL_QEXPR);

    lval* v = lval_take(a, 0);
    lval_del(lval_pop(v,0));
    return v;
}

lval* builtin_list(lenv* e, lval* a) {
    a->type = LVAL_QEXPR;
    return a;
}

lval* builtin_eval(lenv* e, lval* a) {
    LASSERT_NUM(a, "eval", 1);
    LASSERT_TYPE(a, "eval", LVAL_QEXPR);

    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(e, x);
}

lval* lval_join(lenv* e, lval* x, lval* y) {
    while (y->count) {
        x = lval_add(x, lval_pop(y, 0));
    }

    lval_del(y);
    return x;
}

lval* builtin_join(lenv* e, lval* a) {

    for (int i=0; i < a->count; i++) {
        LASSERT_TYPE(a, "join", LVAL_QEXPR);
    }

    lval* x = lval_pop(a, 0);

    while (a->count) {
        lval* y = lval_pop(a, 0);
        x = lval_join(e,x,y);
    }

    lval_del(a);
    return x;
}

lval* builtin_op(lenv* e, lval* a, char* op) {
    
    for (int i=0; i < a->count; i++) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_del(a);
            return lval_err("Cannot operate on non-number!");
        }
    }

    // pop the first element
    lval* x = lval_pop(a, 0);

    // handle cases like "(- 5)" which should evaluate to "-5"
    if ((strcmp(op, "-") == 0) && a->count == 0) {
        x->num = -x->num;
    }

    // instead, while there are still cases remaining
    // recursively pop the first of remaining args, evaluate, until no remaining args
    while (a->count > 0) {

        lval* y = lval_pop(a, 0);

        if (strcmp(op, "+") == 0) { x->num += y->num; }
        if (strcmp(op, "-") == 0) { x->num -= y->num; }
        if (strcmp(op, "*") == 0) { x->num *= y->num; }
        if (strcmp(op, "/") == 0) { 
            if (y->num == 0) {
                lval_del(x); lval_del(y);
                x = lval_err("Division by Zero!");
                break;
            }
            x->num /= y->num;   
        }

        lval_del(y);
    }

    lval_del(a);
    return x;
}

lval* builtin_add(lenv* e, lval* a) {
    return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a) {
    return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a) {
    return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a) {
    return builtin_op(e, a, "/");
}

lval* builtin_def(lenv* e, lval* a) {
    LASSERT_TYPE(a, "def", LVAL_QEXPR);

    // first arg is symbol list
    lval* syms = a->cell[0];

    // first in list is all sym
    for (int i = 0; i < syms->count; i++) {
        LASSERT(a, syms->cell[i]->type == LVAL_SYM, "Function 'def' cannot define non-symbol");
    }

    LASSERT(a, syms->count == a->count-1, "Function 'def' cannot define incorrect no. values to symbols");

    for (int i = 0; i < syms->count; i++) {
        lenv_put(e, syms->cell[i], a->cell[i+1]);
    }

    lval_del(a);
    // call lval_sexpr since creating new sexpr lval
    return lval_sexpr();
}

/*
int exit_flag = 0;
lval* builtin_exit(lenv* e, lval* a) {
    exit_flag = 1;
}
*/

// forward declaration, allows us to use lval_print before it is defined: sometimes lval_expr_print needs it
void lval_print(lval* v);

// for Sexprs
void lval_expr_print(lval* v, char open, char close) {
    putchar(open);
    for (int i = 0; i < v->count; i++) {
        lval_print(v->cell[i]);
        if (i != v->count - 1) {
            putchar(' ');
        }
    }
    putchar(close);
}

// print an lval
void lval_print(lval* v) {
    switch (v->type) {
        case LVAL_NUM: printf("%li", v->num); break;
        case LVAL_ERR: printf("Error: %s", v->err); break;
        case LVAL_SYM: printf("%s", v->sym); break;
        case LVAL_FUN: 
            if (v->builtin) {
                printf("<builtin>");
            } else {
                printf("(\\"); 
                lval_print(v->formals);
                putchar(' '); lval_print(v->body); putchar(')');
            }
            break;
        case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
        case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
    }
}

// print an lval followed by a newline
void lval_println(lval* v) { lval_print(v); putchar('\n'); }

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
    lval* k = lval_sym(name);
    lval* v = lval_fun(func);
    lenv_put(e, k, v);
    lval_del(k);
    lval_del(v);
}

void lenv_add_builtins(lenv* e) {
    lenv_add_builtin(e, "list", builtin_list);
    lenv_add_builtin(e, "head", builtin_head);
    lenv_add_builtin(e, "tail", builtin_tail);
    lenv_add_builtin(e, "eval", builtin_eval);
    lenv_add_builtin(e, "join", builtin_join);
    lenv_add_builtin(e, "def", builtin_def);
    // lenv_add_builtin(e, "exit", builtin_exit);

    lenv_add_builtin(e, "+", builtin_add);
    lenv_add_builtin(e, "-", builtin_sub);
    lenv_add_builtin(e, "*", builtin_mul);
    lenv_add_builtin(e, "/", builtin_div);
}


/* Evaluation */

lval* lval_expr_sexpr(lenv* e, lval* v) {

    // evaluate children
    for (int i=0; i < v->count; i++) {
        v->cell[i] = lval_eval(e, v->cell[i]);
    }   

    // error checking
    for (int i=0; i < v->count; i++) {
        if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
    }

    if (v->count == 0) { return v; }
    if (v->count == 1) { return lval_take(v,0); }

    // ensure first element is func after eval
    lval* f = lval_pop(v, 0);
    if (f->type != LVAL_FUN) {
        lval_del(f);
        lval_del(v);
        return lval_err("S-expression does not start with Symbol!");
    }

    lval* result = f->builtin(e, v);
    lval_del(f);
    return result;
}


lval* lval_eval(lenv* e, lval* v) {
    if (v->type == LVAL_SYM) {
        lval* x = lenv_get(e, v);
        lval_del(v);
        return x;
    }

    if (v->type == LVAL_SEXPR) { return lval_expr_sexpr(e, v); }
    return v;
}



/* Reading */

lval* lval_read_num(mpc_ast_t* t) {
    errno = 0;
    long x = strtol(t->contents, NULL, 10);
    return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
}


lval* lval_read(mpc_ast_t* t) {

    // if the tag is a number or sym return a pointer to a num lval
    if (strstr(t->tag, "number")) {return lval_read_num(t);}
    if (strstr(t->tag, "symbol")) {return lval_sym(t->contents);}

    lval* x = NULL;
    // ">" is the root of the expression in the AST. If root or sexpr, create empty list
    if (strcmp(t->tag, ">") == 0) {x = lval_sexpr();}
    if (strstr(t->tag, "sexpr"))  {x = lval_sexpr();}
    if (strstr(t->tag, "qexpr"))  {x = lval_qexpr();}


    if (x == NULL) {
        return lval_err("unknown node");
    }

    for (int i = 0; i < t->children_num; i++) {
        if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
        if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "}") == 0) { continue; }        
        if (strcmp(t->children[i]->tag,  "regex") == 0) { continue; }

        lval* child = lval_read(t->children[i]);
        x = lval_add(x, child);
    }

    return x;
}


int main (int argc, char** argv) {

    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Symbol = mpc_new("symbol");
    mpc_parser_t* Sexpr  = mpc_new("sexpr");
    mpc_parser_t* Qexpr  = mpc_new("qexpr");
    mpc_parser_t* Expr   = mpc_new("expr");
    mpc_parser_t* Lispy  = mpc_new("lispy");

    mpca_lang(MPCA_LANG_DEFAULT,
    "                                                     \
        number : /-?[0-9]+/ ;                               \
        symbol : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ;         \
        sexpr  : '(' <expr>* ')' ;                          \
        qexpr  : '{' <expr>* '}' ;                          \
        expr   : <number> | <symbol> | <sexpr> | <qexpr> ;  \
        lispy  : /^/ <expr>* /$/ ;                          \
    ",
    Number, Symbol, Sexpr, Qexpr, Expr, Lispy);


    lenv* e = lenv_new();
    lenv_add_builtins(e);
    
    while (1) {

        char* input = readline("lispy> ");

        // readline history is stored separately, primarily in the heap. Calling free() doesn't affect this; there are separate functions to read/write the readline history
        add_history(input);

        // pass user input
        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Lispy, &r)) {

            // lval result = eval(r.output);
            lval* x = lval_eval(e, lval_read(r.output));
            lval_println(x);
            lval_del(x);

            /*
            if (exit_flag) {
                puts("Exiting..");
                free(input);
                break;
            }
            */

        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }
        // if successful, eval & print, else error

        free(input);
    }

    lenv_del(e);
    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
    // undefine and delete the parsers

    return 0;
}