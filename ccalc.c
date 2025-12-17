//   Build: cc -std=c99 -Wall -Wextra -O2 ccalc.c -o ccalc
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { perror("malloc"); exit(1); }
    return p;
}
static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

typedef struct Env Env;
typedef struct AST AST;
typedef struct Value Value;

typedef enum {
    VAL_INT, VAL_DOUBLE, VAL_STRING, VAL_FUNC, VAL_LIST, VAL_NULL, VAL_BOOL
} ValueType;

typedef enum {
    CF_NONE,
    CF_RETURN,
    CF_BREAK,
    CF_CONTINUE
} ControlFlow;

typedef struct Function {
    char **params;
    size_t arity;
    AST *body;
    bool is_builtin;
    Value (*builtin)(Value *, size_t);
} Function;

typedef struct {
    Value *items;
    size_t size;
    size_t capacity;
} List;

struct Value {
    ValueType type;
    ControlFlow cf;
    union {
        long long i;
        double d;
        char *s;
        Function *fn;
        List *list;
        bool b;
    };
};

Value v_int(long long i) { Value v; memset(&v, 0, sizeof(v)); v.type=VAL_INT; v.i=i; return v; }
Value v_double(double d) { Value v; memset(&v, 0, sizeof(v)); v.type=VAL_DOUBLE; v.d=d; return v; }
Value v_str(const char*s) { Value v; memset(&v, 0, sizeof(v)); v.type=VAL_STRING; v.s=xstrdup(s); return v; }
Value v_null(void) { Value v; memset(&v, 0, sizeof(v)); v.type=VAL_NULL; return v; }
Value v_bool(bool b) { Value v; memset(&v, 0, sizeof(v)); v.type=VAL_BOOL; v.b=b; return v; }
Value v_func(Function*f) { Value v; memset(&v, 0, sizeof(v)); v.type=VAL_FUNC; v.fn=f; return v; }

Value v_return(Value v) { v.cf = CF_RETURN; return v;}
Value v_break(void) { Value v = v_null(); v.cf = CF_BREAK; return v;}
Value v_continue(void) { Value v = v_null(); v.cf = CF_CONTINUE; return v;}

Value v_list(void) {
    Value v;
    memset(&v, 0, sizeof(v));
    v.type = VAL_LIST;
    v.list = xmalloc(sizeof(List));
    v.list->capacity = 8;
    v.list->size = 0;
    v.list->items = xmalloc(sizeof(Value) * v.list->capacity);
    return v;
}

void list_append(List *l, Value v) {
    if (l->size >= l->capacity) {
        l->capacity *= 2;
        l->items = realloc(l->items, sizeof(Value) * l->capacity);
    }
    l->items[l->size++] = v;
}

bool value_is_truthy(Value v) {
    switch (v.type) {
        case VAL_NULL: return false;
        case VAL_BOOL: return v.b;
        case VAL_INT: return v.i != 0;
        case VAL_DOUBLE: return v.d != 0.0;
        case VAL_STRING: return v.s && v.s[0] != 0;
        case VAL_LIST: return v.list->size > 0;
        case VAL_FUNC: return true;
    }
    return false;
}

double value_to_double(Value v) {
    if (v.type == VAL_INT) return (double)v.i;
    if (v.type == VAL_DOUBLE) return v.d;
    return 0.0;
}

struct Env {
    char *name;
    Value value;
    bool is_const;
    Env *next;
};

Env *global_env = NULL;

Env *env_new(void) {
    Env *e = xmalloc(sizeof(Env));
    e->name = NULL;
    e->is_const = false;
    e->next = NULL;
    return e;
}

void env_set(Env *env, const char *name, Value v, bool is_const) {
    for (Env *e = env; e; e = e->next) {
        if (e->name && !strcmp(e->name, name)) {
            if (e->is_const) {
                fprintf(stderr, "Error: Cannot reassign const '%s'\n", name);
                return;
            }
            e->value = v;
            e->is_const = is_const;
            return;
        }
    }
    Env *n = xmalloc(sizeof(Env));
    n->name = xstrdup(name);
    n->value = v;
    n->is_const = is_const;
    n->next = env->next;
    env->next = n;
}

Value env_get(Env *env, const char *name) {
    for (Env *e = env; e; e = e->next)
        if (e->name && !strcmp(e->name, name))
            return e->value;
    return v_null();
}

typedef enum {
    T_INT, T_DOUBLE, T_STRING, T_IDENT,
    T_LP, T_RP, T_COMMA, T_LB, T_RB, T_DOT,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_MOD, T_POW,
    T_ASSIGN, T_COLON, T_SEMI,
    T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE,
    T_LAMBDA, T_IF, T_ELSE, T_WHILE, T_TRUE, T_FALSE,
    T_CONST, T_IMPORT,  T_LC, T_RC,
    T_RETURN, T_BREAK,T_CONTINUE,
    T_EOF
} TokType;

typedef struct {
    TokType type;
    char text[256];
    double dval;
} Token;

const char *src;
Token tok;

void skip_ws(void) {
    while (*src && (isspace(*src) || *src == '#')) {
        if (*src == '#') {
            while (*src && *src != '\n') src++;
        } else {
            src++;
        }
    }
}

bool is_ident_start(char c) { return isalpha(c) || c == '_'; }
bool is_ident(char c) { return isalnum(c) || c == '_'; }

void next_token(void) {
    skip_ws();
    if (!*src) { tok.type=T_EOF; return; }

    if (isdigit(*src) || (*src == '.' && isdigit(*(src+1)))) {
        bool has_dot = false;
        char *start = (char*)src;
        if (*src == '.') has_dot = true;
        while (isdigit(*src) || (*src == '.' && !has_dot)) {
            if (*src == '.') has_dot = true;
            src++;
        }
        if (has_dot) {
            tok.type = T_DOUBLE;
            tok.dval = atof(start);
            snprintf(tok.text, sizeof(tok.text), "%g", tok.dval);
        } else {
            tok.type = T_INT;
            long long v = atoll(start);
            sprintf(tok.text, "%lld", v);
        }
        return;
    }

    if (*src=='"' || *src=='\'') {
        char quote = *src++;
        char *p=tok.text;
        while (*src && *src!=quote) {
            if (*src == '\\' && *(src+1)) {
                src++;
                switch (*src) {
                    case 'n': *p++ = '\n'; break;
                    case 't': *p++ = '\t'; break;
                    case '\\': *p++ = '\\'; break;
                    default: *p++ = *src; break;
                }
                src++;
            } else {
                *p++=*src++;
            }
        }
        *p=0;
        if (*src==quote) src++;
        tok.type=T_STRING;
        return;
    }

    if (is_ident_start(*src)) {
        char *p=tok.text;
        while (is_ident(*src)) *p++=*src++;
        *p=0;
        if (!strcmp(tok.text,"lambda")) tok.type=T_LAMBDA;
        else if (!strcmp(tok.text,"if")) tok.type=T_IF;
        else if (!strcmp(tok.text,"else")) tok.type=T_ELSE;
        else if (!strcmp(tok.text,"while")) tok.type=T_WHILE;
        else if (!strcmp(tok.text,"True")) tok.type=T_TRUE;
        else if (!strcmp(tok.text,"False")) tok.type=T_FALSE;
        else if (!strcmp(tok.text,"const")) tok.type=T_CONST;
        else if (!strcmp(tok.text,"import")) tok.type=T_IMPORT;
        else if (!strcmp(tok.text,"return")) tok.type = T_RETURN;
        else if (!strcmp(tok.text,"break")) tok.type = T_BREAK;
        else if (!strcmp(tok.text,"continue")) tok.type = T_CONTINUE;
        else tok.type=T_IDENT;
        return;
    }

    if (*src == '=' && *(src+1) == '=') { src += 2; tok.type = T_EQ; return; }
    if (*src == '!' && *(src+1) == '=') { src += 2; tok.type = T_NE; return; }
    if (*src == '<' && *(src+1) == '=') { src += 2; tok.type = T_LE; return; }
    if (*src == '>' && *(src+1) == '=') { src += 2; tok.type = T_GE; return; }
    if (*src == '*' && *(src+1) == '*') { src += 2; tok.type = T_POW; return; }

    switch (*src++) {
        case '+': tok.type=T_PLUS; return;
        case '-': tok.type=T_MINUS; return;
        case '*': tok.type=T_STAR; return;
        case '/': tok.type=T_SLASH; return;
        case '%': tok.type=T_MOD; return;
        case '(': tok.type=T_LP; return;
        case ')': tok.type=T_RP; return;
        case '[': tok.type=T_LB; return;
        case ']': tok.type=T_RB; return;
        case ',': tok.type=T_COMMA; return;
        case '=': tok.type=T_ASSIGN; return;
        case ':': tok.type=T_COLON; return;
        case ';': tok.type=T_SEMI; return;
        case '.': tok.type=T_DOT; return;
        case '<': tok.type=T_LT; return;
        case '>': tok.type=T_GT; return;
        case '{': tok.type = T_LC; return;
        case '}': tok.type = T_RC; return;
    }
    fprintf(stderr,"Unknown character: %c\n", *(src-1));
    exit(1);
}

typedef enum {
    A_INT, A_DOUBLE, A_STRING, A_VAR, A_BOOL,
    A_BINOP, A_CALL, A_LAMBDA, A_ASSIGN, A_IF, A_WHILE,
    A_LIST, A_INDEX, A_METHOD, A_BLOCK, A_RETURN, A_BREAK, A_CONTINUE
} ASTType;

struct AST {
    ASTType type;
    union {
        long long i;
        double d;
        char *s;
        char *name;
        bool b;
        struct { char op; AST *l,*r; } bin;
        struct { AST *fn; AST **args; size_t argc; } call;
        struct { char **params; size_t arity; AST *body; } lambda;
        struct { char *name; AST *value; } assign;
        struct { AST *cond; AST *then_block; AST *else_block; } ifelse;
        struct { AST *cond; AST *body; } whileloop;
        struct { AST **items; size_t count; } list;
        struct { AST *obj; AST *idx; } index;
        struct { AST *obj; char *method; AST **args; size_t argc; } method;
        struct { AST **stmts; size_t count; } block;
        struct { AST *value; } ret;
    };
};

AST *parse_expr(void);
AST *parse_stmt(void);

AST *ast_new(ASTType t) {
    AST *a=xmalloc(sizeof(AST));
    memset(a,0,sizeof(AST));
    a->type=t;
    return a;
}

AST *parse_block(void) {
    if (tok.type != T_LC) {
        printf("Expected {\n");
        exit(1);
    }
    next_token();

    AST **stmts = xmalloc(sizeof(AST*) * 64);
    size_t n = 0;

    while (tok.type != T_RC && tok.type != T_EOF) {
        stmts[n++] = parse_stmt();
        // Consume optional semicolon between statements
        if (tok.type == T_SEMI) {
            next_token();
        }
    }

    if (tok.type != T_RC) {
        printf("Expected }\n");
        exit(1);
    }
    next_token();

    AST *b = ast_new(A_BLOCK);
    b->block.stmts = stmts;
    b->block.count = n;
    return b;
}

AST *parse_primary(void) {
    AST *a;

    if (tok.type == T_LC)
        return parse_block();

    // List literal support: [1, 2, 3]
    if (tok.type == T_LB) {
        next_token();
        AST **items = xmalloc(sizeof(AST*) * 64);
        size_t n = 0;
        
        if (tok.type != T_RB) {
            while (1) {
                items[n++] = parse_expr();
                if (tok.type == T_COMMA) {
                    next_token();
                } else {
                    break;
                }
            }
        }
        
        if (tok.type != T_RB) {
            printf("Expected ]\n");
            exit(1);
        }
        next_token();
        
        AST *list = ast_new(A_LIST);
        list->list.items = items;
        list->list.count = n;
        return list;
    }

    if (tok.type == T_RETURN) {
        next_token();
        AST *ret = ast_new(A_RETURN);
        if (tok.type != T_RC && tok.type != T_SEMI && tok.type != T_EOF) {
            ret->ret.value = parse_expr();
        } else {
            ret->ret.value = NULL;
        }
        return ret;
    }

    if (tok.type == T_BREAK) {
        next_token();
        return ast_new(A_BREAK);
    }

    if (tok.type == T_CONTINUE) {
        next_token();
        return ast_new(A_CONTINUE);
    }

    if (tok.type == T_MINUS) {
        next_token();
        AST *operand = parse_primary();
        AST *zero = ast_new(A_INT);
        zero->i = 0;
        AST *neg = ast_new(A_BINOP);
        neg->bin.op = '-';
        neg->bin.l = zero;
        neg->bin.r = operand;
        return neg;
    }

    if (tok.type == T_INT) {
        a = ast_new(A_INT);
        a->i = atoll(tok.text);
        next_token();
        return a;
    }

    if (tok.type == T_DOUBLE) {
        a = ast_new(A_DOUBLE);
        a->d = tok.dval;
        next_token();
        return a;
    }

    if (tok.type == T_STRING) {
        a = ast_new(A_STRING);
        a->s = xstrdup(tok.text);
        next_token();
        return a;
    }

    if (tok.type == T_TRUE || tok.type == T_FALSE) {
        a = ast_new(A_BOOL);
        a->b = (tok.type == T_TRUE);
        next_token();
        return a;
    }

    if (tok.type == T_IDENT) {
        a = ast_new(A_VAR);
        a->name = xstrdup(tok.text);
        next_token();
        return a;
    }

    if (tok.type == T_LP) {
        next_token();
        a = parse_expr();
        if (tok.type != T_RP) exit(1);
        next_token();
        return a;
    }

    printf("Invalid expression\n");
    exit(1);
}

AST *parse_postfix(void) {
    AST *a = parse_primary();
    while (1) {
        if (tok.type == T_LP) {
            next_token();
            AST **args = xmalloc(sizeof(AST*) * 16);
            size_t n = 0;
            if (tok.type != T_RP) {
                while (1) {
                    args[n++] = parse_expr();
                    if (tok.type == T_COMMA) next_token();
                    else break;
                }
            }
            if (tok.type != T_RP) exit(1);
            next_token();
            AST *c = ast_new(A_CALL);
            c->call.fn = a;
            c->call.args = args;
            c->call.argc = n;
            a = c;
        } else if (tok.type == T_LB) {
            next_token();
            AST *idx = parse_expr();
            if (tok.type != T_RB) { printf("Expected ]\n"); exit(1); }
            next_token();
            AST *c = ast_new(A_INDEX);
            c->index.obj = a;
            c->index.idx = idx;
            a = c;
        } else if (tok.type == T_DOT) {
            next_token();
            if (tok.type != T_IDENT) { printf("Expected method\n"); exit(1); }
            char *method = xstrdup(tok.text);
            next_token();
            if (tok.type == T_LP) {
                next_token();
                AST **args = xmalloc(sizeof(AST*) * 16);
                size_t n = 0;
                if (tok.type != T_RP) {
                    while (1) {
                        args[n++] = parse_expr();
                        if (tok.type == T_COMMA) next_token();
                        else break;
                    }
                }
                if (tok.type != T_RP) exit(1);
                next_token();
                AST *c = ast_new(A_METHOD);
                c->method.obj = a;
                c->method.method = method;
                c->method.args = args;
                c->method.argc = n;
                a = c;
            } else {
                AST *c = ast_new(A_METHOD);
                c->method.obj = a;
                c->method.method = method;
                c->method.args = NULL;
                c->method.argc = 0;
                a = c;
            }
        } else {
            break;
        }
    }
    return a;
}

AST *parse_power(void) {
    AST *a = parse_postfix();
    if (tok.type == T_POW) {
        next_token();
        AST *b = parse_power();
        AST *n = ast_new(A_BINOP);
        n->bin.op = '^';
        n->bin.l = a;
        n->bin.r = b;
        return n;
    }
    return a;
}

AST *parse_term(void) {
    AST *a=parse_power();
    while (tok.type==T_STAR||tok.type==T_SLASH||tok.type==T_MOD) {
        char op;
        if (tok.type==T_STAR) op='*';
        else if (tok.type==T_SLASH) op='/';
        else op='%';
        next_token();
        AST *b=parse_power();
        AST *n=ast_new(A_BINOP);
        n->bin.op=op;
        n->bin.l=a;
        n->bin.r=b;
        a=n;
    }
    return a;
}

AST *parse_arith(void) {
    AST *a=parse_term();
    while (tok.type==T_PLUS||tok.type==T_MINUS) {
        char op=(tok.type==T_PLUS?'+':'-');
        next_token();
        AST *b=parse_term();
        AST *n=ast_new(A_BINOP);
        n->bin.op=op;
        n->bin.l=a;
        n->bin.r=b;
        a=n;
    }
    return a;
}

AST *parse_comparison(void) {
    AST *a = parse_arith();
    while (tok.type==T_EQ||tok.type==T_NE||tok.type==T_LT||
           tok.type==T_GT||tok.type==T_LE||tok.type==T_GE) {
        char op;
        switch (tok.type) {
            case T_EQ: op='E'; break;
            case T_NE: op='N'; break;
            case T_LT: op='<'; break;
            case T_GT: op='>'; break;
            case T_LE: op='L'; break;
            case T_GE: op='G'; break;
            default: op='E'; break;
        }
        next_token();
        AST *b = parse_arith();
        AST *n = ast_new(A_BINOP);
        n->bin.op = op;
        n->bin.l = a;
        n->bin.r = b;
        a = n;
    }
    return a;
}

AST *parse_expr(void) {
    if (tok.type == T_IF) {
        next_token();
        AST *cond = parse_comparison();

        AST *then_block;
        AST *else_block = NULL;

        if (tok.type == T_LC)
            then_block = parse_block();
        else {
            if (tok.type != T_COLON) exit(1);
            next_token();
            then_block = parse_expr();
        }

        if (tok.type == T_ELSE) {
            next_token();
            if (tok.type == T_LC)
                else_block = parse_block();
            else {
                if (tok.type != T_COLON) exit(1);
                next_token();
                else_block = parse_expr();
            }
        }

        AST *a = ast_new(A_IF);
        a->ifelse.cond = cond;
        a->ifelse.then_block = then_block;
        a->ifelse.else_block = else_block;
        return a;
    }

    if (tok.type == T_WHILE) {
        next_token();
        AST *cond = parse_comparison();
        AST *body;

        if (tok.type == T_LC)
            body = parse_block();
        else {
            if (tok.type != T_COLON) exit(1);
            next_token();
            body = parse_expr();
        }

        AST *a = ast_new(A_WHILE);
        a->whileloop.cond = cond;
        a->whileloop.body = body;
        return a;
    }

    return parse_comparison();
}

AST *parse_stmt(void) {
    return parse_expr();
}

Value builtin_print(Value *args, size_t argc) {
    for (size_t i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        switch (args[i].type) {
            case VAL_INT: printf("%lld", args[i].i); break;
            case VAL_DOUBLE: printf("%g", args[i].d); break;
            case VAL_STRING: printf("%s", args[i].s); break;
            case VAL_BOOL: printf("%s", args[i].b ? "True" : "False"); break;
            case VAL_NULL: printf("None"); break;
            case VAL_FUNC: printf("<function>"); break;
            case VAL_LIST:
                printf("[");
                for (size_t j = 0; j < args[i].list->size; j++) {
                    if (j > 0) printf(", ");
                    Value v = args[i].list->items[j];
                    if (v.type == VAL_INT) printf("%lld", v.i);
                    else if (v.type == VAL_DOUBLE) printf("%g", v.d);
                    else if (v.type == VAL_STRING) printf("\"%s\"", v.s);
                    else printf("?");
                }
                printf("]");
                break;
        }
    }
    printf("\n");
    return v_null();
}

Value builtin_assert(Value *args, size_t argc) {
    if (argc < 1 || argc > 2) {
        fprintf(stderr, "assert() takes 1 or 2 arguments\n");
        exit(1);
    }

    if (!value_is_truthy(args[0])) {
        if (argc == 2 && args[1].type == VAL_STRING) {
            fprintf(stderr, "Assertion failed: %s\n", args[1].s);
        } else {
            fprintf(stderr, "Assertion failed\n");
        }
        exit(1);
    }
    return v_null();
}

Value builtin_len(Value *args, size_t argc) {
    if (argc != 1) return v_null();
    if (args[0].type == VAL_LIST) return v_int(args[0].list->size);
    if (args[0].type == VAL_STRING) return v_int(strlen(args[0].s));
    return v_null();
}

Value builtin_range(Value *args, size_t argc) {
    if (argc < 1 || argc > 3) return v_null();
    long long start = 0, stop, step = 1;
    if (argc == 1) {
        if (args[0].type != VAL_INT) return v_null();
        stop = args[0].i;
    } else if (argc == 2) {
        if (args[0].type != VAL_INT || args[1].type != VAL_INT) return v_null();
        start = args[0].i;
        stop = args[1].i;
    } else {
        if (args[0].type != VAL_INT || args[1].type != VAL_INT || args[2].type != VAL_INT)
            return v_null();
        start = args[0].i;
        stop = args[1].i;
        step = args[2].i;
    }
    Value result = v_list();
    if (step > 0) {
        for (long long i = start; i < stop; i += step)
            list_append(result.list, v_int(i));
    } else if (step < 0) {
        for (long long i = start; i > stop; i += step)
            list_append(result.list, v_int(i));
    }
    return result;
}

Value builtin_help(Value *args, size_t argc) {
    (void)args; (void)argc;
    printf("\n=== Built-in Functions ===\n");
    printf("print(...)  - Print values\n");
    printf("assert(...) - Asserts two expressions\n");
    printf("len(obj)    - Get length\n");
    printf("range(...)  - Create range list\n");
    printf("help()      - This message\n");
    printf("\n=== Syntax ===\n");
    printf("x = 10                   - Variable\n");
    printf("const pi = 3.14          - Constant variable\n");
    printf("f(x) = x * 2             - Function\n");
    printf("const square(x) = x * x  - Constant function\n");
    printf("f(g, x) = g(x) + 1       - Composition\n");
    printf("import \"file.calc\"       - Import file\n");
    printf("nums = [1, 2, 3]         - List literal\n");
    printf("\n=== Features ===\n");
    printf("- Late binding: redefine g, f auto-updates\n");
    printf("- const: prevents reassignment\n");
    printf("- import: load .calc files\n");
    printf("- List literals: [1, 2, 3, 4]\n");
    printf("\n");
    return v_null();
}

Value eval(AST *a, Env *env);

Value call(Function *fn, AST **args, size_t argc, Env *caller) {
    if (fn->is_builtin) {
        Value *vals = xmalloc(sizeof(Value) * argc);
        for (size_t i = 0; i < argc; i++)
            vals[i] = eval(args[i], caller);
        Value result = fn->builtin(vals, argc);
        free(vals);
        return result;
    }

    if (argc < fn->arity) {
        Function *nf=xmalloc(sizeof(Function));
        *nf=*fn;
        nf->params+=argc;
        nf->arity-=argc;
        return v_func(nf);
    }

    Env *local = env_new();
    local->next = global_env;
    for (size_t i=0;i<fn->arity;i++)
        env_set(local,fn->params[i],eval(args[i],caller), false);
    Value result = eval(fn->body, local);
    if (result.cf == CF_RETURN) {
        result.cf = CF_NONE;
    }
    return result;
}

Value call_method(Value obj, const char *method, Value *args, size_t argc) {
    if (obj.type == VAL_INT) {
        if (!strcmp(method, "bin")) {
            char buf[128];
            long long n = obj.i;
            if (n == 0) return v_str("0b0");
            char bits[128];
            int i = 0;
            bool neg = n < 0;
            if (neg) n = -n;
            while (n > 0) {
                bits[i++] = (n & 1) ? '1' : '0';
                n >>= 1;
            }
            char *p = buf;
            if (neg) *p++ = '-';
            *p++ = '0'; *p++ = 'b';
            for (int j = i - 1; j >= 0; j--)
                *p++ = bits[j];
            *p = 0;
            return v_str(buf);
        }
        else if (!strcmp(method, "hex")) {
            char buf[64];
            sprintf(buf, "0x%llx", obj.i);
            return v_str(buf);
        }
    }
    if (obj.type == VAL_STRING) {
        if (!strcmp(method, "upper")) {
            char *s = xstrdup(obj.s);
            for (char *p = s; *p; p++) *p = toupper(*p);
            return v_str(s);
        }
        else if (!strcmp(method, "lower")) {
            char *s = xstrdup(obj.s);
            for (char *p = s; *p; p++) *p = tolower(*p);
            return v_str(s);
        }
    }
    if (obj.type == VAL_LIST) {
        if (!strcmp(method, "append") && argc == 1) {
            list_append(obj.list, args[0]);
            return v_null();
        }
        else if (!strcmp(method, "pop") && obj.list->size > 0) {
            return obj.list->items[--obj.list->size];
        }
    }
    return v_null();
}

Value eval(AST *a, Env *env) {
    switch (a->type) {
        case A_INT: return v_int(a->i);
        case A_DOUBLE: return v_double(a->d);
        case A_STRING: return v_str(a->s);
        case A_BOOL: return v_bool(a->b);
        case A_VAR: return env_get(env,a->name);
        case A_LIST: {
            Value v = v_list();
            for (size_t i = 0; i < a->list.count; i++)
                list_append(v.list, eval(a->list.items[i], env));
            return v;
        }
        case A_INDEX: {
            Value obj = eval(a->index.obj, env);
            Value idx = eval(a->index.idx, env);
            if (obj.type == VAL_LIST && idx.type == VAL_INT) {
                if (idx.i >= 0 && (size_t)idx.i < obj.list->size)
                    return obj.list->items[idx.i];
            }
            return v_null();
        }
        case A_METHOD: {
            Value obj = eval(a->method.obj, env);
            Value *args = NULL;
            if (a->method.argc > 0) {
                args = xmalloc(sizeof(Value) * a->method.argc);
                for (size_t i = 0; i < a->method.argc; i++)
                    args[i] = eval(a->method.args[i], env);
            }
            Value result = call_method(obj, a->method.method, args, a->method.argc);
            if (args) free(args);
            return result;
        }
        case A_BINOP: {
            Value l=eval(a->bin.l,env);
            Value r=eval(a->bin.r,env);
            if (a->bin.op == 'E') {
                if (l.type == VAL_INT && r.type == VAL_INT) return v_bool(l.i == r.i);
                if (l.type == VAL_BOOL && r.type == VAL_BOOL) return v_bool(l.b == r.b);
                return v_bool(false);
            }
            if (a->bin.op == 'N') {
                if (l.type == VAL_INT && r.type == VAL_INT) return v_bool(l.i != r.i);
                if (l.type == VAL_BOOL && r.type == VAL_BOOL) return v_bool(l.b != r.b);
                return v_bool(true);
            }
            if (a->bin.op == '<') {
                if (l.type == VAL_INT && r.type == VAL_INT) return v_bool(l.i < r.i);
                if (l.type == VAL_DOUBLE || r.type == VAL_DOUBLE)
                    return v_bool(value_to_double(l) < value_to_double(r));
                return v_bool(false);
            }
            if (a->bin.op == '>') {
                if (l.type == VAL_INT && r.type == VAL_INT) return v_bool(l.i > r.i);
                if (l.type == VAL_DOUBLE || r.type == VAL_DOUBLE)
                    return v_bool(value_to_double(l) > value_to_double(r));
                return v_bool(false);
            }
            if (a->bin.op == 'L') {
                if (l.type == VAL_INT && r.type == VAL_INT) return v_bool(l.i <= r.i);
                if (l.type == VAL_DOUBLE || r.type == VAL_DOUBLE)
                    return v_bool(value_to_double(l) <= value_to_double(r));
                return v_bool(false);
            }
            if (a->bin.op == 'G') {
                if (l.type == VAL_INT && r.type == VAL_INT) return v_bool(l.i >= r.i);
                if (l.type == VAL_DOUBLE || r.type == VAL_DOUBLE)
                    return v_bool(value_to_double(l) >= value_to_double(r));
                return v_bool(false);
            }
            if (l.type == VAL_DOUBLE || r.type == VAL_DOUBLE) {
                double ld = value_to_double(l);
                double rd = value_to_double(r);
                if (a->bin.op=='+') return v_double(ld + rd);
                if (a->bin.op=='-') return v_double(ld - rd);
                if (a->bin.op=='*') return v_double(ld * rd);
                if (a->bin.op=='/') return v_double(ld / rd);
                if (a->bin.op=='^') {
                    if ((long long)rd == rd) {
                        long long exp = (long long)rd;
                        if (exp < 0) return v_double(0);
                        double result = 1.0;
                        double base = ld;
                        while (exp > 0) {
                            if (exp & 1) result *= base;
                            base *= base;
                            exp >>= 1;
                        }
                        return v_double(result);
                    }
                    return v_double(0);
                }
            }
            if (l.type==VAL_INT && r.type==VAL_INT) {
                if (a->bin.op=='+') return v_int(l.i+r.i);
                if (a->bin.op=='-') return v_int(l.i-r.i);
                if (a->bin.op=='*') return v_int(l.i*r.i);
                if (a->bin.op=='/') return v_int(l.i/r.i);
                if (a->bin.op=='%') return v_int(l.i%r.i);
                if (a->bin.op=='^') {
                    if (r.i < 0) return v_int(0);
                    long long result = 1;
                    long long base = l.i;
                    long long exp = r.i;
                    while (exp > 0) {
                        if (exp & 1) result *= base;
                        base *= base;
                        exp >>= 1;
                    }
                    return v_int(result);
                }
            }
            if (a->bin.op=='+' && l.type==VAL_STRING && r.type==VAL_STRING) {
                char *s = xmalloc(strlen(l.s) + strlen(r.s) + 1);
                strcpy(s, l.s);
                strcat(s, r.s);
                Value v;
                memset(&v, 0, sizeof(v));
                v.type = VAL_STRING;
                v.s = s;
                return v;
            }
            return v_null();
        }
        case A_CALL: {
            Value f=eval(a->call.fn,env);
            if (f.type!=VAL_FUNC) return v_null();
            return call(f.fn,a->call.args,a->call.argc,env);
        }
        case A_LAMBDA: {
            Function *f=xmalloc(sizeof(Function));
            f->params=a->lambda.params;
            f->arity=a->lambda.arity;
            f->body=a->lambda.body;
            f->is_builtin=false;
            return v_func(f);
        }
        case A_ASSIGN: {
            Value v=eval(a->assign.value,env);
            env_set(env,a->assign.name,v, false);
            return v;
        }
        case A_IF: {
            Value cond = eval(a->ifelse.cond, env);
            if (value_is_truthy(cond)) {
                return eval(a->ifelse.then_block, env);
            } else if (a->ifelse.else_block) {
                return eval(a->ifelse.else_block, env);
            }
            return v_null();
        }
        case A_WHILE: {
            Value result = v_null();
            while (value_is_truthy(eval(a->whileloop.cond, env))) {
                result = eval(a->whileloop.body, env);
                if (result.cf == CF_BREAK) {
                    result.cf = CF_NONE;
                    break;
                }
                if (result.cf == CF_CONTINUE) {
                    result.cf = CF_NONE;
                    continue;
                }
                if (result.cf == CF_RETURN) {
                    return result;
                }
            }
            return result;
        }
        case A_BLOCK: {
            Value result = v_null();
            for (size_t i = 0; i < a->block.count; i++) {
                result = eval(a->block.stmts[i], env);
                if (result.cf != CF_NONE) {
                    return result;
                }
            }
            return result;
        }
        case A_RETURN: {
            Value v = a->ret.value ? eval(a->ret.value, env) : v_null();
            return v_return(v);
        }
        case A_BREAK: {
            return v_break();
        }
        case A_CONTINUE: {
            return v_continue();
        }
    }
    return v_null();
}

Function *make_builtin(Value (*fn)(Value*, size_t)) {
    Function *f = xmalloc(sizeof(Function));
    f->is_builtin = true;
    f->builtin = fn;
    f->arity = 0;
    return f;
}

void run_file(const char *filename);

void run_repl(void) {
    char line[2048];
    while (1) {
        printf("λ ");
        if (!fgets(line,sizeof(line),stdin)) break;
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = 0;
        char *trimmed = line;
        while (*trimmed && isspace(*trimmed)) trimmed++;
        if (strlen(trimmed) == 0 || trimmed[0] == '#') continue;
        if (!strcmp(trimmed, "quit") || !strcmp(trimmed, "exit")) {
            printf("\n");
            break;
        }

        src=line;
        next_token();

        if (tok.type == T_IMPORT) {
            next_token();
            if (tok.type != T_STRING) {
                printf("Error: import requires a filename string\n");
                continue;
            }
            char filename[256];
            strcpy(filename, tok.text);
            next_token();
            run_file(filename);
            continue;
        }

        bool is_const = false;
        if (tok.type == T_CONST) {
            is_const = true;
            next_token();
        }

        if (tok.type==T_IDENT) {
            char name[256];
            strcpy(name,tok.text);
            next_token();
            if (tok.type==T_LP) {
                next_token();
                char **params = xmalloc(sizeof(char*) * 16);
                size_t n = 0;
                while (tok.type == T_IDENT) {
                    params[n++] = xstrdup(tok.text);
                    next_token();
                    if (tok.type == T_LP) {
                        int depth = 1;
                        next_token();
                        while (depth > 0 && tok.type != T_EOF) {
                            if (tok.type == T_IDENT && depth == 1) {
                                params[n++] = xstrdup(tok.text);
                            }
                            next_token();
                            if (tok.type == T_LP) depth++;
                            else if (tok.type == T_RP) depth--;
                        }
                    }
                    if (tok.type == T_COMMA) {
                        next_token();
                    } else if (tok.type == T_RP) {
                        break;
                    }
                }
                if (tok.type == T_RP) {
                    next_token();
                    if (tok.type == T_ASSIGN) {
                        next_token();
                        AST *body = parse_expr();
                        AST *lambda = ast_new(A_LAMBDA);
                        lambda->lambda.params = params;
                        lambda->lambda.arity = n;
                        lambda->lambda.body = body;
                        Value fn = eval(lambda, global_env);
                        env_set(global_env, name, fn, is_const);
                        continue;
                    }
                }
                src = line;
                next_token();
            }
            else if (tok.type==T_ASSIGN) {
                next_token();
                AST *expr=parse_expr();
                Value v=eval(expr,global_env);
                env_set(global_env,name,v, is_const);
                continue;
            }
        }

        src = line;
        next_token();
        AST *e=parse_stmt();
        Value v=eval(e,global_env);
        if (v.type==VAL_INT) printf("%lld\n",v.i);
        else if (v.type==VAL_DOUBLE) printf("%g\n",v.d);
        else if (v.type==VAL_STRING) printf("\"%s\"\n",v.s);
        else if (v.type==VAL_BOOL) printf("%s\n", v.b ? "True" : "False");
        else if (v.type==VAL_LIST) {
            printf("[");
            for (size_t i = 0; i < v.list->size; i++) {
                if (i > 0) printf(", ");
                if (v.list->items[i].type == VAL_INT)
                    printf("%lld", v.list->items[i].i);
                else if (v.list->items[i].type == VAL_DOUBLE)
                    printf("%g", v.list->items[i].d);
                else if (v.list->items[i].type == VAL_STRING)
                    printf("\"%s\"", v.list->items[i].s);
            }
            printf("]\n");
        }
    }
}

void run_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = xmalloc(fsize + 1);
    size_t bytes_read = fread(content, 1, fsize, f);
    content[bytes_read] = 0;
    fclose(f);

    src = content;
    next_token();

    while (tok.type != T_EOF) {
        if (tok.type == T_IMPORT) {
            next_token();
            if (tok.type != T_STRING) {
                fprintf(stderr, "Error: import requires a filename string\n");
                break;
            }
            char subfile[256];
            strcpy(subfile, tok.text);
            next_token();
            run_file(subfile);
            continue;
        }

        bool is_const = false;
        if (tok.type == T_CONST) {
            is_const = true;
            next_token();
        }

        AST *stmt = parse_stmt();
        
        if (stmt->type == A_VAR && tok.type == T_ASSIGN) {
            char *name = stmt->name;
            next_token();
            AST *expr = parse_expr();
            Value v = eval(expr, global_env);
            env_set(global_env, name, v, is_const);
        }
        else if (stmt->type == A_CALL && stmt->call.fn->type == A_VAR && tok.type == T_ASSIGN) {
            char *name = stmt->call.fn->name;
            AST **args = stmt->call.args;
            size_t argc = stmt->call.argc;
            
            char **params = xmalloc(sizeof(char*) * argc);
            for (size_t i = 0; i < argc; i++) {
                if (args[i]->type == A_VAR) {
                    params[i] = args[i]->name;
                } else {
                    fprintf(stderr, "Error: Function parameters must be identifiers\n");
                    free(params);
                    goto skip_stmt;
                }
            }
            
            next_token();
            AST *body = parse_expr();
            AST *lambda = ast_new(A_LAMBDA);
            lambda->lambda.params = params;
            lambda->lambda.arity = argc;
            lambda->lambda.body = body;
            Value fn = eval(lambda, global_env);
            env_set(global_env, name, fn, is_const);
            free(params);
        }
        else {
            eval(stmt, global_env);
        }
        
        skip_stmt:;
    }
    
    free(content);
}

int main(int argc, char **argv) {
    global_env = env_new();
    env_set(global_env, "print", v_func(make_builtin(builtin_print)), true);
    env_set(global_env, "len", v_func(make_builtin(builtin_len)), true);
    env_set(global_env, "range", v_func(make_builtin(builtin_range)), true);
    env_set(global_env, "help", v_func(make_builtin(builtin_help)), true);
    env_set(global_env, "assert", v_func(make_builtin(builtin_assert)), true);

    if (argc > 1) {
        run_file(argv[1]);
    } else {
        printf("λ-calculus REPL with Composition\n");
        printf("Type 'help()' for syntax\n\n");
        run_repl();
    }
    return 0;
}
