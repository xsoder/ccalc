//   ccalc - Lambda Calculus Language with FFI, Closures, Error Handling,
//   Tuples, and Any Type Build: cc -std=c99 -Wall -Wextra -O2 ccalc.c -o ccalc
//   -ldl -lm

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dlfcn.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_WHITE "\033[37m"
#define COLOR_GRAY "\033[90m"

bool use_colors = false;
bool errors_occurred = false;
bool import_mode = false;

typedef struct {
  char** files;
  size_t count;
  size_t capacity;
} ImportTracker;

ImportTracker import_tracker = {NULL, 0, 0};

void init_import_tracker(void) {
  import_tracker.capacity = 16;
  import_tracker.count = 0;
  import_tracker.files = malloc(sizeof(char*) * import_tracker.capacity);
}

bool is_file_imported(const char* filename) {
  for (size_t i = 0; i < import_tracker.count; i++) {
    if (strcmp(import_tracker.files[i], filename) == 0) {
      return true;
    }
  }
  return false;
}

void mark_file_imported(const char* filename) {
  if (import_tracker.count >= import_tracker.capacity) {
    import_tracker.capacity *= 2;
    import_tracker.files =
        realloc(import_tracker.files, sizeof(char*) * import_tracker.capacity);
  }
  import_tracker.files[import_tracker.count++] = strdup(filename);
}

typedef struct {
  const char* filename;
  int line;
  int column;
} SourceLoc;

SourceLoc current_loc = {"<stdin>", 1, 1};

typedef struct ArenaBlock {
  struct ArenaBlock* next;
  size_t capacity;
  size_t used;
  char data[];
} ArenaBlock;

typedef struct {
  ArenaBlock* blocks;
  size_t block_size;
} Arena;

Arena* arena_new(size_t block_size) {
  Arena* a = malloc(sizeof(Arena));
  a->blocks = NULL;
  a->block_size = block_size;
  return a;
}

void* arena_alloc(Arena* arena, size_t size) {
  if (!arena->blocks || arena->blocks->used + size > arena->blocks->capacity) {
    size_t bs = arena->block_size > size ? arena->block_size : size * 2;
    ArenaBlock* b = malloc(sizeof(ArenaBlock) + bs);
    b->next = arena->blocks;
    b->capacity = bs;
    b->used = 0;
    arena->blocks = b;
  }
  void* ptr = arena->blocks->data + arena->blocks->used;
  arena->blocks->used += size;
  return ptr;
}

char* arena_strdup(Arena* arena, const char* s) {
  size_t n = strlen(s) + 1;
  char* p = arena_alloc(arena, n);
  memcpy(p, s, n);
  return p;
}

void arena_free(Arena* arena) {
  ArenaBlock* b = arena->blocks;
  while (b) {
    ArenaBlock* next = b->next;
    free(b);
    b = next;
  }
  free(arena);
}

Arena* global_arena;

#define xmalloc(sz) arena_alloc(global_arena, sz)
#define xstrdup(s) arena_strdup(global_arena, s)

void error_at(SourceLoc loc, const char* fmt, ...) {
  fprintf(stderr, "%s:%d:%d: error: ", loc.filename, loc.line, loc.column);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
  errors_occurred = true;
}

void warning_at(SourceLoc loc, const char* fmt, ...) {
  fprintf(stderr, "%s:%d:%d: warning: ", loc.filename, loc.line, loc.column);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

typedef enum {
  FFI_INT,
  FFI_DOUBLE,
  FFI_STRING,
  FFI_VOID,
  FFI_PTR,
  FFI_LONG,
  FFI_FLOAT,
  FFI_CHAR,
  FFI_BOOL,
  FFI_ANY
} FFIType;

typedef struct {
  char* name;
  void* handle;
} LoadedLib;

typedef struct {
  char* name;
  char* c_name;
  void* func_ptr;
  FFIType* param_types;
  size_t param_count;
  FFIType return_type;
} ExternFunc;

LoadedLib* loaded_libs = NULL;
size_t loaded_libs_count = 0;
size_t loaded_libs_capacity = 0;

ExternFunc* extern_funcs = NULL;
size_t extern_funcs_count = 0;
size_t extern_funcs_capacity = 0;

typedef struct Env Env;
typedef struct AST AST;
typedef struct Value Value;

typedef enum {
  VAL_INT,
  VAL_DOUBLE,
  VAL_STRING,
  VAL_FUNC,
  VAL_LIST,
  VAL_NULL,
  VAL_BOOL,
  VAL_ERROR,
  VAL_TUPLE,
  VAL_PTR,
  VAL_STRUCT_DEF,
  VAL_STRUCT,
  VAL_ANY
} ValueType;

typedef enum { CF_NONE, CF_RETURN, CF_BREAK, CF_CONTINUE } ControlFlow;

typedef struct Function {
  char** params;
  size_t arity;
  AST* body;
  bool is_builtin;
  Value (*builtin)(Value*, size_t);
  Env* closure_env;
} Function;

typedef struct {
  Value* items;
  size_t size;
  size_t capacity;
} List;

typedef struct {
  Value* items;
  size_t size;
} Tuple;

typedef struct {
  char* name;
  char** fields;
  size_t field_count;
  char** method_names;
  Function** methods;
  size_t method_count;
} StructDef;

typedef struct {
  StructDef* def;
  Value* values;
} StructVal;

struct Value {
  ValueType type;
  ControlFlow cf;
  union {
    long long i;
    double d;
    char* s;
    Function* fn;
    List* list;
    bool b;
    Tuple* tuple;
    void* ptr;
    StructDef* struct_def;
    StructVal* struct_val;
    Value* any_val;
  };
};

Value v_int(long long i) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.type = VAL_INT;
  v.i = i;
  return v;
}
Value v_double(double d) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.type = VAL_DOUBLE;
  v.d = d;
  return v;
}
Value v_str(const char* s) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.type = VAL_STRING;
  v.s = xstrdup(s);
  return v;
}
Value v_null(void) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.type = VAL_NULL;
  return v;
}
Value v_bool(bool b) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.type = VAL_BOOL;
  v.b = b;
  return v;
}
Value v_func(Function* f) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.type = VAL_FUNC;
  v.fn = f;
  return v;
}
Value v_error(const char* msg) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.type = VAL_ERROR;
  v.s = xstrdup(msg);
  return v;
}
Value v_ptr(void* p) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.type = VAL_PTR;
  v.ptr = p;
  return v;
}

Value v_any(Value inner) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.type = VAL_ANY;
  v.any_val = xmalloc(sizeof(Value));
  *v.any_val = inner;
  return v;
}

Value v_return(Value v) {
  v.cf = CF_RETURN;
  return v;
}
Value v_break(void) {
  Value v = v_null();
  v.cf = CF_BREAK;
  return v;
}
Value v_continue(void) {
  Value v = v_null();
  v.cf = CF_CONTINUE;
  return v;
}

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

Value v_tuple(Value* items, size_t size) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.type = VAL_TUPLE;
  v.tuple = xmalloc(sizeof(Tuple));
  v.tuple->size = size;
  v.tuple->items = xmalloc(sizeof(Value) * size);
  memcpy(v.tuple->items, items, sizeof(Value) * size);
  return v;
}

void list_append(List* l, Value v) {
  if (l->size >= l->capacity) {
    size_t new_capacity = l->capacity * 2;
    Value* new_items = xmalloc(sizeof(Value) * new_capacity);
    memcpy(new_items, l->items, sizeof(Value) * l->size);
    l->items = new_items;
    l->capacity = new_capacity;
  }
  l->items[l->size++] = v;
}

bool value_is_truthy(Value v) {
  if (v.type == VAL_ANY && v.any_val) {
    return value_is_truthy(*v.any_val);
  }
  switch (v.type) {
    case VAL_NULL:
      return false;
    case VAL_ERROR:
      return false;
    case VAL_BOOL:
      return v.b;
    case VAL_INT:
      return v.i != 0;
    case VAL_DOUBLE:
      return v.d != 0.0;
    case VAL_STRING:
      return v.s && v.s[0] != 0;
    case VAL_LIST:
      return v.list->size > 0;
    case VAL_TUPLE:
      return v.tuple->size > 0;
    case VAL_FUNC:
      return true;
    case VAL_PTR:
      return v.ptr != NULL;
    case VAL_STRUCT_DEF:
      return true;
    case VAL_STRUCT:
      return true;
    case VAL_ANY:
      return false;
  }
  return false;
}

double value_to_double(Value v) {
  if (v.type == VAL_ANY && v.any_val) {
    return value_to_double(*v.any_val);
  }
  if (v.type == VAL_INT) return (double)v.i;
  if (v.type == VAL_DOUBLE) return v.d;
  return 0.0;
}

const char* value_type_name(Value v) {
  switch (v.type) {
    case VAL_INT:
      return "int";
    case VAL_DOUBLE:
      return "double";
    case VAL_STRING:
      return "string";
    case VAL_FUNC:
      return "function";
    case VAL_LIST:
      return "list";
    case VAL_NULL:
      return "null";
    case VAL_BOOL:
      return "bool";
    case VAL_ERROR:
      return "error";
    case VAL_TUPLE:
      return "tuple";
    case VAL_PTR:
      return "ptr";
    case VAL_STRUCT_DEF:
      return "struct_def";
    case VAL_STRUCT:
      return "struct";
    case VAL_ANY:
      return "any";
  }
  return "unknown";
}

const char* value_type_color(Value v) {
  if (!use_colors) return "";
  switch (v.type) {
    case VAL_INT:
      return COLOR_CYAN;
    case VAL_DOUBLE:
      return COLOR_BLUE;
    case VAL_STRING:
      return COLOR_GREEN;
    case VAL_FUNC:
      return COLOR_MAGENTA;
    case VAL_LIST:
      return COLOR_YELLOW;
    case VAL_NULL:
      return COLOR_GRAY;
    case VAL_BOOL:
      return COLOR_RED;
    case VAL_ERROR:
      return COLOR_RED;
    case VAL_TUPLE:
      return COLOR_MAGENTA;
    case VAL_PTR:
      return COLOR_WHITE;
    case VAL_STRUCT_DEF:
      return COLOR_CYAN;
    case VAL_STRUCT:
      return COLOR_CYAN;
    case VAL_ANY:
      return COLOR_YELLOW;
  }
  return COLOR_RESET;
}

struct Env {
  char* name;
  Value value;
  bool is_const;
  Env* next;
};

Env* global_env = NULL;

Env* env_new(void) {
  Env* e = xmalloc(sizeof(Env));
  e->name = NULL;
  e->is_const = false;
  e->next = NULL;
  return e;
}

void env_set(Env* env, const char* name, Value v, bool is_const) {
  for (Env* e = env; e; e = e->next) {
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
  Env* n = xmalloc(sizeof(Env));
  n->name = xstrdup(name);
  n->value = v;
  n->is_const = is_const;
  n->next = env->next;
  env->next = n;
}

Value env_get(Env* env, const char* name) {
  for (Env* e = env; e; e = e->next) {
    if (e->name && !strcmp(e->name, name)) return e->value;
  }
  return v_null();
}

typedef enum {
  T_INT,
  T_DOUBLE,
  T_STRING,
  T_IDENT,
  T_LP,
  T_RP,
  T_COMMA,
  T_LB,
  T_RB,
  T_DOT,
  T_PLUS,
  T_MINUS,
  T_STAR,
  T_SLASH,
  T_MOD,
  T_POW,
  T_ASSIGN,
  T_COLON,
  T_SEMI,
  T_EQ,
  T_NE,
  T_LT,
  T_GT,
  T_LE,
  T_GE,
  T_LAMBDA,
  T_IF,
  T_ELSE,
  T_WHILE,
  T_TRUE,
  T_FALSE,
  T_CONST,
  T_IMPORT,
  T_LC,
  T_RC,
  T_RETURN,
  T_BREAK,
  T_CONTINUE,
  T_LINK,
  T_EXTERN,
  T_STRUCT,
  T_EOF,
  T_ERROR
} TokType;

typedef struct {
  TokType type;
  char text[256];
  double dval;
  SourceLoc loc;
} Token;

const char* src;
const char* src_start;
Token tok;

const char* token_name(TokType t) {
  switch (t) {
    case T_INT:
      return "integer";
    case T_DOUBLE:
      return "double";
    case T_STRING:
      return "string";
    case T_IDENT:
      return "identifier";
    case T_LP:
      return "'('";
    case T_RP:
      return "')'";
    case T_COMMA:
      return "','";
    case T_LB:
      return "'['";
    case T_RB:
      return "']'";
    case T_DOT:
      return "'.'";
    case T_PLUS:
      return "'+'";
    case T_MINUS:
      return "'-'";
    case T_STAR:
      return "'*'";
    case T_SLASH:
      return "'/'";
    case T_MOD:
      return "'%'";
    case T_POW:
      return "'**'";
    case T_ASSIGN:
      return "'='";
    case T_COLON:
      return "':'";
    case T_SEMI:
      return "';'";
    case T_EQ:
      return "'=='";
    case T_NE:
      return "'!='";
    case T_LT:
      return "'<'";
    case T_GT:
      return "'>'";
    case T_LE:
      return "'<='";
    case T_GE:
      return "'>='";
    case T_LAMBDA:
      return "'lambda'";
    case T_IF:
      return "'if'";
    case T_ELSE:
      return "'else'";
    case T_WHILE:
      return "'while'";
    case T_TRUE:
      return "'True'";
    case T_FALSE:
      return "'False'";
    case T_CONST:
      return "'const'";
    case T_IMPORT:
      return "'import'";
    case T_LC:
      return "'{'";
    case T_RC:
      return "'}'";
    case T_RETURN:
      return "'return'";
    case T_BREAK:
      return "'break'";
    case T_CONTINUE:
      return "'continue'";
    case T_LINK:
      return "'link'";
    case T_EXTERN:
      return "'extern'";
    case T_STRUCT:
      return "'struct'";
    case T_EOF:
      return "end of file";
    case T_ERROR:
      return "error";
  }
  return "unknown";
}

void skip_ws(void) {
  while (*src && (isspace(*src) || *src == '#')) {
    if (*src == '#') {
      while (*src && *src != '\n') src++;
    } else {
      if (*src == '\n') {
        current_loc.line++;
        current_loc.column = 1;
      } else {
        current_loc.column++;
      }
      src++;
    }
  }
}

bool is_ident_start(char c) { return isalpha(c) || c == '_'; }
bool is_ident(char c) { return isalnum(c) || c == '_'; }

void next_token(void) {
  skip_ws();
  tok.loc = current_loc;

  if (!*src) {
    tok.type = T_EOF;
    strcpy(tok.text, "");
    return;
  }

  if (isdigit(*src) || (*src == '.' && isdigit(*(src + 1)))) {
    bool has_dot = false;
    char* start = (char*)src;
    if (*src == '.') has_dot = true;
    while (isdigit(*src) || (*src == '.' && !has_dot)) {
      if (*src == '.') has_dot = true;
      src++;
      current_loc.column++;
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

  if (*src == '"' || *src == '\'') {
    char quote = *src++;
    current_loc.column++;
    char* p = tok.text;
    while (*src && *src != quote) {
      if (*src == '\\' && *(src + 1)) {
        src++;
        current_loc.column++;
        switch (*src) {
          case 'n':
            *p++ = '\n';
            break;
          case 't':
            *p++ = '\t';
            break;
          case '\\':
            *p++ = '\\';
            break;
          default:
            *p++ = *src;
            break;
        }
        src++;
        current_loc.column++;
      } else {
        if (*src == '\n') {
          current_loc.line++;
          current_loc.column = 1;
        } else {
          current_loc.column++;
        }
        *p++ = *src++;
      }
    }
    *p = 0;
    if (*src == quote) {
      src++;
      current_loc.column++;
    } else {
      error_at(tok.loc, "unterminated string literal");
    }
    tok.type = T_STRING;
    return;
  }

  if (is_ident_start(*src)) {
    char* p = tok.text;
    while (is_ident(*src)) {
      *p++ = *src++;
      current_loc.column++;
    }
    *p = 0;
    if (!strcmp(tok.text, "lambda"))
      tok.type = T_LAMBDA;
    else if (!strcmp(tok.text, "if"))
      tok.type = T_IF;
    else if (!strcmp(tok.text, "else"))
      tok.type = T_ELSE;
    else if (!strcmp(tok.text, "while"))
      tok.type = T_WHILE;
    else if (!strcmp(tok.text, "True"))
      tok.type = T_TRUE;
    else if (!strcmp(tok.text, "False"))
      tok.type = T_FALSE;
    else if (!strcmp(tok.text, "const"))
      tok.type = T_CONST;
    else if (!strcmp(tok.text, "import"))
      tok.type = T_IMPORT;
    else if (!strcmp(tok.text, "return"))
      tok.type = T_RETURN;
    else if (!strcmp(tok.text, "break"))
      tok.type = T_BREAK;
    else if (!strcmp(tok.text, "continue"))
      tok.type = T_CONTINUE;
    else if (!strcmp(tok.text, "link"))
      tok.type = T_LINK;
    else if (!strcmp(tok.text, "extern"))
      tok.type = T_EXTERN;
    else if (!strcmp(tok.text, "struct"))
      tok.type = T_STRUCT;
    else
      tok.type = T_IDENT;
    return;
  }

  if (*src == '=' && *(src + 1) == '=') {
    src += 2;
    current_loc.column += 2;
    tok.type = T_EQ;
    strcpy(tok.text, "==");
    return;
  }
  if (*src == '!' && *(src + 1) == '=') {
    src += 2;
    current_loc.column += 2;
    tok.type = T_NE;
    strcpy(tok.text, "!=");
    return;
  }
  if (*src == '<' && *(src + 1) == '=') {
    src += 2;
    current_loc.column += 2;
    tok.type = T_LE;
    strcpy(tok.text, "<=");
    return;
  }
  if (*src == '>' && *(src + 1) == '=') {
    src += 2;
    current_loc.column += 2;
    tok.type = T_GE;
    strcpy(tok.text, ">=");
    return;
  }
  if (*src == '*' && *(src + 1) == '*') {
    src += 2;
    current_loc.column += 2;
    tok.type = T_POW;
    strcpy(tok.text, "**");
    return;
  }

  char ch = *src++;
  current_loc.column++;

  switch (ch) {
    case '+':
      tok.type = T_PLUS;
      strcpy(tok.text, "+");
      return;
    case '-':
      tok.type = T_MINUS;
      strcpy(tok.text, "-");
      return;
    case '*':
      tok.type = T_STAR;
      strcpy(tok.text, "*");
      return;
    case '/':
      tok.type = T_SLASH;
      strcpy(tok.text, "/");
      return;
    case '%':
      tok.type = T_MOD;
      strcpy(tok.text, "%");
      return;
    case '(':
      tok.type = T_LP;
      strcpy(tok.text, "(");
      return;
    case ')':
      tok.type = T_RP;
      strcpy(tok.text, ")");
      return;
    case '[':
      tok.type = T_LB;
      strcpy(tok.text, "[");
      return;
    case ']':
      tok.type = T_RB;
      strcpy(tok.text, "]");
      return;
    case ',':
      tok.type = T_COMMA;
      strcpy(tok.text, ",");
      return;
    case '=':
      tok.type = T_ASSIGN;
      strcpy(tok.text, "=");
      return;
    case ':':
      tok.type = T_COLON;
      strcpy(tok.text, ":");
      return;
    case ';':
      tok.type = T_SEMI;
      strcpy(tok.text, ";");
      return;
    case '.':
      tok.type = T_DOT;
      strcpy(tok.text, ".");
      return;
    case '<':
      tok.type = T_LT;
      strcpy(tok.text, "<");
      return;
    case '>':
      tok.type = T_GT;
      strcpy(tok.text, ">");
      return;
    case '{':
      tok.type = T_LC;
      strcpy(tok.text, "{");
      return;
    case '}':
      tok.type = T_RC;
      strcpy(tok.text, "}");
      return;
  }

  snprintf(tok.text, sizeof(tok.text), "%c", ch);
  tok.type = T_ERROR;
  error_at(tok.loc, "unexpected character '%c' (0x%02x)", ch,
           (unsigned char)ch);
}

typedef enum {
  A_INT,
  A_DOUBLE,
  A_STRING,
  A_VAR,
  A_BOOL,
  A_BINOP,
  A_CALL,
  A_LAMBDA,
  A_ASSIGN,
  A_IF,
  A_WHILE,
  A_LIST,
  A_INDEX,
  A_METHOD,
  A_BLOCK,
  A_RETURN,
  A_BREAK,
  A_CONTINUE,
  A_TUPLE,
  A_STRING_INTERP,
  A_STRUCT_DEF,
  A_STRUCT_INIT,
  A_MEMBER,
  A_ASSIGN_UNPACK
} ASTType;

struct AST {
  ASTType type;
  SourceLoc loc;
  union {
    long long i;
    double d;
    char* s;
    char* name;
    bool b;
    struct {
      char op;
      AST *l, *r;
    } bin;
    struct {
      AST* fn;
      AST** args;
      size_t argc;
    } call;
    struct {
      char** params;
      size_t arity;
      AST* body;
    } lambda;
    struct {
      char* name;
      AST* value;
    } assign;
    struct {
      AST* cond;
      AST* then_block;
      AST* else_block;
    } ifelse;
    struct {
      AST* cond;
      AST* body;
    } whileloop;
    struct {
      AST** items;
      size_t count;
    } list;
    struct {
      AST* obj;
      AST* idx;
    } index;
    struct {
      AST* obj;
      char* method;
      AST** args;
      size_t argc;
    } method;
    struct {
      AST** stmts;
      size_t count;
    } block;
    struct {
      AST* value;
    } ret;
    struct {
      char** parts;
      AST** exprs;
      size_t count;
    } str_interp;
    struct {
      char* name;
      char** fields;
      size_t count;
      AST** methods;
      size_t method_count;
    } struct_def;
    struct {
      char* name;
      char** fields;
      AST** values;
      size_t count;
    } struct_init;
    struct {
      AST* obj;
      char* member;
    } member;
    struct {
      char** names;
      size_t count;
      AST* value;
    } assign_unpack;
  };
};

AST* parse_expr(void);
AST* parse_stmt(void);

AST* ast_new(ASTType t) {
  AST* a = xmalloc(sizeof(AST));
  memset(a, 0, sizeof(AST));
  a->type = t;
  a->loc = tok.loc;
  return a;
}

bool expect(TokType expected) {
  if (tok.type != expected) {
    error_at(tok.loc, "expected %s but got %s", token_name(expected),
             token_name(tok.type));
    return false;
  }
  return true;
}

AST* parse_block(void) {
  if (tok.type != T_LC) {
    error_at(tok.loc, "expected '{' but got %s", token_name(tok.type));
    return ast_new(A_BLOCK);
  }
  next_token();

  AST** stmts = xmalloc(sizeof(AST*) * 64);
  size_t n = 0;

  while (tok.type != T_RC && tok.type != T_EOF) {
    AST* stmt = parse_stmt();

    // Handle assignment (single or unpacking)
    if (stmt && (stmt->type == A_VAR || stmt->type == A_TUPLE) &&
        tok.type == T_ASSIGN) {
      if (stmt->type == A_TUPLE) {
        // Check validity for unpacking
        for (size_t i = 0; i < stmt->list.count; i++) {
          if (stmt->list.items[i]->type != A_VAR) {
            error_at(stmt->list.items[i]->loc, "cannot unpack to non-variable");
          }
        }
      }

      next_token();
      AST* value = parse_expr();

      // Handle comma-separated values on RHS
      if (tok.type == T_COMMA) {
        AST** items = xmalloc(sizeof(AST*) * 64);
        size_t count = 0;
        items[count++] = value;
        while (tok.type == T_COMMA) {
          next_token();
          items[count++] = parse_expr();
        }
        AST* tuple = ast_new(A_TUPLE);
        tuple->list.items = items;
        tuple->list.count = count;
        value = tuple;
      }

      if (stmt->type == A_VAR) {
        AST* assign = ast_new(A_ASSIGN);
        assign->assign.name = stmt->name;
        assign->assign.value = value;
        stmt = assign;
      } else {
        AST* unpack = ast_new(A_ASSIGN_UNPACK);
        unpack->assign_unpack.count = stmt->list.count;
        unpack->assign_unpack.names = xmalloc(sizeof(char*) * stmt->list.count);
        for (size_t i = 0; i < stmt->list.count; i++) {
          unpack->assign_unpack.names[i] = stmt->list.items[i]->name;
        }
        unpack->assign_unpack.value = value;
        stmt = unpack;
      }
    }

    if (stmt) stmts[n++] = stmt;

    if (tok.type == T_SEMI) {
      next_token();
    }
    if (tok.type == T_ERROR) {
      next_token();
    }
  }

  if (tok.type != T_RC) {
    error_at(tok.loc, "expected '}' but got %s", token_name(tok.type));
  } else {
    next_token();
  }

  AST* b = ast_new(A_BLOCK);
  b->block.stmts = stmts;
  b->block.count = n;
  return b;
}

AST* parse_string_interpolation(const char* str) {
  bool has_interp = false;
  for (const char* p = str; *p; p++) {
    if (*p == '{' && *(p + 1) != '{') {
      has_interp = true;
      break;
    }
  }

  if (!has_interp) {
    AST* a = ast_new(A_STRING);
    a->s = xstrdup(str);
    return a;
  }

  AST* interp = ast_new(A_STRING_INTERP);
  char** parts = xmalloc(sizeof(char*) * 32);
  AST** exprs = xmalloc(sizeof(AST*) * 32);
  size_t count = 0;

  char buffer[512];
  char* buf_ptr = buffer;
  const char* p = str;

  while (*p) {
    if (*p == '{' && *(p + 1) == '{') {
      *buf_ptr++ = '{';
      p += 2;
    } else if (*p == '}' && *(p + 1) == '}') {
      *buf_ptr++ = '}';
      p += 2;
    } else if (*p == '{') {
      *buf_ptr = '\0';
      parts[count] = xstrdup(buffer);
      buf_ptr = buffer;
      p++;

      char var_name[128];
      char* var_ptr = var_name;
      while (*p && *p != '}' && is_ident(*p)) {
        *var_ptr++ = *p++;
      }
      *var_ptr = '\0';

      if (var_name[0] == '\0') {
        error_at(tok.loc, "empty interpolation in string");
        while (*p && *p != '}') p++;
        if (*p == '}') p++;
        continue;
      }

      if (*p != '}') {
        error_at(tok.loc, "unclosed interpolation in string");
        AST* a = ast_new(A_STRING);
        a->s = xstrdup(str);
        return a;
      }
      p++;

      AST* var = ast_new(A_VAR);
      var->name = xstrdup(var_name);
      exprs[count] = var;
      count++;
    } else {
      if (buf_ptr - buffer < 511) {
        *buf_ptr++ = *p++;
      } else {
        p++;
      }
    }
  }

  *buf_ptr = '\0';
  parts[count] = xstrdup(buffer);

  interp->str_interp.parts = parts;
  interp->str_interp.exprs = exprs;
  interp->str_interp.count = count;

  return interp;
}

AST* parse_primary(void) {
  AST* a;

  if (tok.type == T_LC) return parse_block();

  if (tok.type == T_LAMBDA) {
    SourceLoc lambda_loc = tok.loc;
    next_token();
    char** params = xmalloc(sizeof(char*) * 16);
    size_t n = 0;

    if (tok.type == T_IDENT) {
      params[n++] = xstrdup(tok.text);
      next_token();

      while (tok.type == T_COMMA) {
        next_token();
        if (tok.type != T_IDENT) {
          error_at(tok.loc, "expected parameter name after comma");
          break;
        }
        params[n++] = xstrdup(tok.text);
        next_token();
      }
    }

    if (tok.type != T_COLON) {
      error_at(tok.loc, "expected ':' after lambda parameters");
      if (tok.type != T_EOF) next_token();
    } else {
      next_token();
    }

    AST* body = parse_expr();

    AST* lambda = ast_new(A_LAMBDA);
    lambda->loc = lambda_loc;
    lambda->lambda.params = params;
    lambda->lambda.arity = n;
    lambda->lambda.body = body;
    return lambda;
  }

  if (tok.type == T_LB) {
    next_token();
    AST** items = xmalloc(sizeof(AST*) * 64);
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
      error_at(tok.loc, "expected ']' but got %s", token_name(tok.type));
    } else {
      next_token();
    }

    AST* list = ast_new(A_LIST);
    list->list.items = items;
    list->list.count = n;
    return list;
  }

  if (tok.type == T_RETURN) {
    next_token();
    AST* ret = ast_new(A_RETURN);
    if (tok.type != T_RC && tok.type != T_SEMI && tok.type != T_EOF) {
      AST* val = parse_expr();
      if (tok.type == T_COMMA) {
        AST** items = xmalloc(sizeof(AST*) * 64);
        size_t count = 0;
        items[count++] = val;
        while (tok.type == T_COMMA) {
          next_token();
          items[count++] = parse_expr();
        }
        AST* tuple = ast_new(A_TUPLE);
        tuple->list.items = items;
        tuple->list.count = count;
        val = tuple;
      }
      ret->ret.value = val;
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
    AST* operand = parse_primary();
    AST* zero = ast_new(A_INT);
    zero->i = 0;
    AST* neg = ast_new(A_BINOP);
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
    a = parse_string_interpolation(tok.text);
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
    char* name = xstrdup(tok.text);
    next_token();

    // Check for Struct Instantiation: Ident { ... }
    if (tok.type == T_LC) {
      next_token();
      AST** values = xmalloc(sizeof(AST*) * 32);
      char** fields = xmalloc(sizeof(char*) * 32);
      size_t count = 0;
      while (tok.type != T_RC && tok.type != T_EOF) {
        if (tok.type != T_IDENT) {
          error_at(tok.loc, "expected field name in struct init");
          break;
        }
        fields[count] = xstrdup(tok.text);
        next_token();
        if (!expect(T_COLON)) break;
        next_token();
        values[count] = parse_expr();
        count++;
        if (tok.type == T_COMMA) next_token();
      }
      expect(T_RC);
      next_token();

      AST* init = ast_new(A_STRUCT_INIT);
      init->struct_init.name = name;
      init->struct_init.fields = fields;
      init->struct_init.values = values;
      init->struct_init.count = count;
      return init;
    }

    a = ast_new(A_VAR);
    a->name = name;
    return a;
  }

  if (tok.type == T_LP) {
    next_token();

    AST** items = xmalloc(sizeof(AST*) * 64);
    size_t n = 0;

    if (tok.type != T_RP) {
      items[n++] = parse_expr();

      if (tok.type == T_COMMA) {
        while (tok.type == T_COMMA) {
          next_token();
          if (tok.type == T_RP) break;
          items[n++] = parse_expr();
        }

        if (!expect(T_RP)) {
        } else {
          next_token();
        }

        AST* tuple = ast_new(A_TUPLE);
        tuple->list.items = items;
        tuple->list.count = n;
        return tuple;
      }
    }

    if (!expect(T_RP)) {
    } else {
      next_token();
    }

    if (n == 0) return ast_new(A_INT);
    return items[0];
  }

  error_at(tok.loc, "expected expression but got %s", token_name(tok.type));
  next_token();
  return ast_new(A_INT);
}

AST* parse_postfix(void) {
  AST* obj = parse_primary();
  while (1) {
    if (tok.type == T_LP) {
      next_token();
      AST* call = ast_new(A_CALL);
      call->call.fn = obj;
      call->call.args = xmalloc(sizeof(AST*) * 64);
      call->call.argc = 0;
      if (tok.type != T_RP) {
        call->call.args[call->call.argc++] = parse_expr();
        while (tok.type == T_COMMA) {
          next_token();
          call->call.args[call->call.argc++] = parse_expr();
        }
      }
      expect(T_RP);
      next_token();
      obj = call;
    } else if (tok.type == T_LB) {
      next_token();
      AST* idx = parse_expr();
      expect(T_RB);
      next_token();
      AST* c = ast_new(A_INDEX);
      c->index.obj = obj;
      c->index.idx = idx;
      obj = c;
    } else if (tok.type == T_DOT) {
      next_token();
      if (!expect(T_IDENT)) {
        next_token();
        break;
      }
      char* method = xstrdup(tok.text);
      next_token();
      if (tok.type == T_LP) {
        next_token();
        AST** args = xmalloc(sizeof(AST*) * 16);
        size_t n = 0;
        if (tok.type != T_RP) {
          while (1) {
            args[n++] = parse_expr();
            if (tok.type == T_COMMA)
              next_token();
            else
              break;
          }
        }
        if (!expect(T_RP)) {
        } else {
          next_token();
        }
        AST* c = ast_new(A_METHOD);
        c->method.obj = obj;
        c->method.method = method;
        c->method.args = args;
        c->method.argc = n;
        obj = c;
      } else {
        AST* c = ast_new(A_MEMBER);
        c->member.obj = obj;
        c->member.member = method;
        obj = c;
      }
    } else {
      break;
    }
  }
  return obj;
}

AST* parse_power(void) {
  AST* a = parse_postfix();
  if (tok.type == T_POW) {
    next_token();
    AST* b = parse_power();
    AST* n = ast_new(A_BINOP);
    n->bin.op = '^';
    n->bin.l = a;
    n->bin.r = b;
    return n;
  }
  return a;
}

AST* parse_term(void) {
  AST* a = parse_power();
  while (tok.type == T_STAR || tok.type == T_SLASH || tok.type == T_MOD) {
    char op;
    if (tok.type == T_STAR)
      op = '*';
    else if (tok.type == T_SLASH)
      op = '/';
    else
      op = '%';
    next_token();
    AST* b = parse_power();
    AST* n = ast_new(A_BINOP);
    n->bin.op = op;
    n->bin.l = a;
    n->bin.r = b;
    a = n;
  }
  return a;
}

AST* parse_arith(void) {
  AST* a = parse_term();
  while (tok.type == T_PLUS || tok.type == T_MINUS) {
    char op = (tok.type == T_PLUS ? '+' : '-');
    next_token();
    AST* b = parse_term();
    AST* n = ast_new(A_BINOP);
    n->bin.op = op;
    n->bin.l = a;
    n->bin.r = b;
    a = n;
  }
  return a;
}

AST* parse_comparison(void) {
  AST* a = parse_arith();
  while (tok.type == T_EQ || tok.type == T_NE || tok.type == T_LT ||
         tok.type == T_GT || tok.type == T_LE || tok.type == T_GE) {
    char op;
    switch (tok.type) {
      case T_EQ:
        op = 'E';
        break;
      case T_NE:
        op = 'N';
        break;
      case T_LT:
        op = '<';
        break;
      case T_GT:
        op = '>';
        break;
      case T_LE:
        op = 'L';
        break;
      case T_GE:
        op = 'G';
        break;
      default:
        op = 'E';
        break;
    }
    next_token();
    AST* b = parse_arith();
    AST* n = ast_new(A_BINOP);
    n->bin.op = op;
    n->bin.l = a;
    n->bin.r = b;
    a = n;
  }
  return a;
}

AST* parse_expr(void) {
  if (tok.type == T_IF) {
    next_token();
    AST* cond = parse_comparison();

    AST* then_block;
    AST* else_block = NULL;

    if (tok.type == T_LC)
      then_block = parse_block();
    else {
      if (!expect(T_COLON)) {
        return ast_new(A_INT);
      }
      next_token();
      then_block = parse_expr();
    }

    if (tok.type == T_ELSE) {
      next_token();
      if (tok.type == T_LC)
        else_block = parse_block();
      else {
        if (!expect(T_COLON)) {
          return ast_new(A_INT);
        }
        next_token();
        else_block = parse_expr();
      }
    }

    AST* a = ast_new(A_IF);
    a->ifelse.cond = cond;
    a->ifelse.then_block = then_block;
    a->ifelse.else_block = else_block;
    return a;
  }

  if (tok.type == T_WHILE) {
    next_token();
    AST* cond = parse_comparison();
    AST* body;

    if (tok.type == T_LC)
      body = parse_block();
    else {
      if (!expect(T_COLON)) {
        return ast_new(A_INT);
      }
      next_token();
      body = parse_expr();
    }

    AST* a = ast_new(A_WHILE);
    a->whileloop.cond = cond;
    a->whileloop.body = body;
    return a;
  }

  return parse_comparison();
}

AST* parse_stmt(void) {
  if (tok.type == T_STRUCT) {
    next_token();
    if (tok.type != T_IDENT) {
      error_at(tok.loc, "expected struct name");
      return ast_new(A_STRUCT_DEF);
    }
    char* name = xstrdup(tok.text);
    next_token();
    if (!expect(T_LC)) {
      return ast_new(A_STRUCT_DEF);
    }
    next_token();

    AST** methods = xmalloc(sizeof(AST*) * 32);
    size_t method_count = 0;

    char** fields = xmalloc(sizeof(char*) * 32);
    size_t count = 0;

    while (tok.type != T_RC && tok.type != T_EOF) {
      if (tok.type != T_IDENT) {
        error_at(tok.loc, "expected field or method name");
        break;
      }
      char* member_name = xstrdup(tok.text);
      next_token();

      if (tok.type == T_LP) {
        next_token();
        char** params = xmalloc(sizeof(char*) * 16);
        size_t n = 0;
        if (tok.type != T_RP) {
          while (tok.type == T_IDENT) {
            params[n++] = xstrdup(tok.text);
            next_token();
            if (tok.type == T_COMMA)
              next_token();
            else
              break;
          }
        }
        expect(T_RP);
        next_token();

        if (!expect(T_ASSIGN)) {
          // Error
        }
        next_token();

        AST* body = parse_expr();
        AST* lambda = ast_new(A_LAMBDA);
        lambda->lambda.params = params;
        lambda->lambda.arity = n;
        lambda->lambda.body = body;

        AST* assign = ast_new(A_ASSIGN);
        assign->assign.name = member_name;
        assign->assign.value = lambda;

        methods[method_count++] = assign;
      } else {
        fields[count++] = member_name;
        if (tok.type == T_COMMA) next_token();
      }
    }
    expect(T_RC);
    next_token();

    AST* a = ast_new(A_STRUCT_DEF);
    a->struct_def.name = name;
    a->struct_def.fields = fields;
    a->struct_def.count = count;
    a->struct_def.methods = methods;
    a->struct_def.method_count = method_count;
    return a;
  }

  AST* expr = parse_expr();
  if (tok.type == T_COMMA) {
    AST** items = xmalloc(sizeof(AST*) * 64);
    size_t count = 0;
    items[count++] = expr;
    while (tok.type == T_COMMA) {
      next_token();
      items[count++] = parse_expr();
    }
    AST* tuple = ast_new(A_TUPLE);
    tuple->list.items = items;
    tuple->list.count = count;
    expr = tuple;
  }

  if (tok.type == T_ASSIGN) {
    if (expr->type == A_TUPLE) {
      next_token();
      AST* rhs = parse_expr();

      AST* unpack = ast_new(A_ASSIGN_UNPACK);
      unpack->assign_unpack.names = xmalloc(sizeof(char*) * expr->list.count);
      unpack->assign_unpack.count = expr->list.count;
      unpack->assign_unpack.value = rhs;

      for (size_t i = 0; i < expr->list.count; i++) {
        if (expr->list.items[i]->type != A_VAR) {
          error_at(expr->loc, "cannot unpack to non-variable");
          return expr;
        }
        unpack->assign_unpack.names[i] = expr->list.items[i]->name;
      }
      return unpack;
    } else if (expr->type == A_VAR) {
      next_token();
      AST* rhs = parse_expr();
      AST* assign = ast_new(A_ASSIGN);
      assign->assign.name = expr->name;
      assign->assign.value = rhs;
      return assign;
    }
  }

  return expr;
}
void print_value(Value v);

void print_value(Value v) {
  const char* color = value_type_color(v);
  const char* reset = use_colors ? COLOR_RESET : "";

  if (v.type == VAL_ANY && v.any_val) {
    printf("%s<any:", color);
    print_value(*v.any_val);
    printf(">%s", reset);
    return;
  }

  switch (v.type) {
    case VAL_INT:
      printf("%s%lld%s", color, v.i, reset);
      break;
    case VAL_DOUBLE:
      printf("%s%g%s", color, v.d, reset);
      break;
    case VAL_STRING:
      printf("%s%s%s", color, v.s, reset);
      break;
    case VAL_BOOL:
      printf("%s%s%s", color, v.b ? "True" : "False", reset);
      break;
    case VAL_NULL:
      printf("%sNone%s", color, reset);
      break;
    case VAL_ERROR:
      printf("%sError: %s%s", color, v.s, reset);
      break;
    case VAL_FUNC:
      printf("%s<function>%s", color, reset);
      break;
    case VAL_PTR:
      printf("%s<ptr:%p>%s", color, v.ptr, reset);
      break;
    case VAL_ANY:
      printf("%s<any:null>%s", color, reset);
      break;
    case VAL_STRUCT_DEF:
      printf("%s<struct %s>%s", color, v.struct_def->name, reset);
      break;
    case VAL_STRUCT:
      printf("%s%s {", color, v.struct_val->def->name);
      for (size_t i = 0; i < v.struct_val->def->field_count; i++) {
        if (i > 0) printf(", ");
        printf(" %s: ", v.struct_val->def->fields[i]);
        print_value(v.struct_val->values[i]);
      }
      printf(" }%s", reset);
      break;
    case VAL_LIST:
      printf("%s[%s", color, reset);
      for (size_t j = 0; j < v.list->size; j++) {
        if (j > 0) printf(", ");
        Value item = v.list->items[j];
        if (item.type == VAL_INT)
          printf("%s%lld%s", value_type_color(item), item.i, reset);
        else if (item.type == VAL_DOUBLE)
          printf("%s%g%s", value_type_color(item), item.d, reset);
        else if (item.type == VAL_STRING)
          printf("%s\"%s\"%s", value_type_color(item), item.s, reset);
        else if (item.type == VAL_BOOL)
          printf("%s%s%s", value_type_color(item), item.b ? "True" : "False",
                 reset);
        else if (item.type == VAL_PTR)
          printf("%s<ptr:%p>%s", value_type_color(item), item.ptr, reset);
        else
          printf("?");
      }
      printf("%s]%s", color, reset);
      break;
    case VAL_TUPLE:
      printf("%s(%s", color, reset);
      for (size_t j = 0; j < v.tuple->size; j++) {
        if (j > 0) printf(", ");
        Value item = v.tuple->items[j];
        if (item.type == VAL_INT)
          printf("%s%lld%s", value_type_color(item), item.i, reset);
        else if (item.type == VAL_DOUBLE)
          printf("%s%g%s", value_type_color(item), item.d, reset);
        else if (item.type == VAL_STRING)
          printf("%s\"%s\"%s", value_type_color(item), item.s, reset);
        else if (item.type == VAL_BOOL)
          printf("%s%s%s", value_type_color(item), item.b ? "True" : "False",
                 reset);
        else if (item.type == VAL_PTR)
          printf("%s<ptr:%p>%s", value_type_color(item), item.ptr, reset);
        else if (v.type == VAL_FUNC)
          printf("%s<function>%s\n", color, reset);
        else if (v.type == VAL_NULL)
          printf("%sNone%s\n", color, reset);
        else if (v.type == VAL_STRUCT_DEF)
          printf("%s<struct %s>%s\n", color, v.struct_def->name, reset);
        else if (v.type == VAL_STRUCT) {
          printf("%s%s {", color, v.struct_val->def->name);
          for (size_t i = 0; i < v.struct_val->def->field_count; i++) {
            if (i > 0) printf(", ");
            printf(" %s: ", v.struct_val->def->fields[i]);
            print_value(v.struct_val->values[i]);
          }
          printf(" }%s\n", reset);
        } else
          printf("?");
      }
      printf("%s)%s", color, reset);
      break;
  }
}

Value builtin_print(Value* args, size_t argc) {
  if (argc == 0) {
    printf("\n");
    fflush(stdout);
    return v_null();
  }

  if (args[0].type == VAL_STRING && argc > 1) {
    const char* fmt = args[0].s;
    size_t arg_idx = 1;

    size_t placeholder_count = 0;
    for (const char* p = fmt; *p; p++) {
      if (*p == '%' && *(p + 1) != '%') {
        placeholder_count++;
      } else if (*p == '%' && *(p + 1) == '%') {
        p++;
      }
    }

    if (placeholder_count > 0 && placeholder_count <= argc - 1) {
      const char* p = fmt;
      while (*p) {
        if (*p == '%' && *(p + 1) == '%') {
          printf("%%");
          p += 2;
        } else if (*p == '%' && arg_idx < argc) {
          print_value(args[arg_idx++]);
          p++;
        } else {
          putchar(*p);
          p++;
        }
      }
      printf("\n");
      fflush(stdout);
      return v_null();
    }
  }

  for (size_t i = 0; i < argc; i++) {
    if (i > 0) printf(" ");
    print_value(args[i]);
  }
  printf("\n");
  fflush(stdout);
  return v_null();
}

Value builtin_type(Value* args, size_t argc) {
  if (argc != 1) {
    fprintf(stderr, "type() takes exactly 1 argument\n");
    return v_null();
  }
  return v_str(value_type_name(args[0]));
}

Value builtin_exit(Value* args, size_t argc) {
  if (argc > 1) {
    fprintf(stderr, "exit() takes 1 arguments\n");
    return v_int(1);
  }

  if (argc == 1) {
    if (args[0].type == VAL_INT)
      exit(args[0].i);
    else {
      fprintf(stderr, "exit() expecits int \n");
      return v_int(1);
    }
  }
  return v_int(0);
}

Value builtin_assert(Value* args, size_t argc) {
  if (argc < 1 || argc > 2) {
    fprintf(stderr, "assert() takes 1 or 2 arguments\n");
    return v_int(1);
  }

  if (argc == 1) {
    if (!value_is_truthy(args[0])) {
      fprintf(stderr, "Assertion failed\n");
      exit(1);
    }
  } else {
    bool equal = false;
    if (args[0].type == VAL_INT && args[1].type == VAL_INT)
      equal = (args[0].i == args[1].i);
    else if (args[0].type == VAL_DOUBLE && args[1].type == VAL_DOUBLE)
      equal = (args[0].d == args[1].d);
    else if (args[0].type == VAL_BOOL && args[1].type == VAL_BOOL)
      equal = (args[0].b == args[1].b);
    else if (args[0].type == VAL_STRING && args[1].type == VAL_STRING)
      equal = (strcmp(args[0].s, args[1].s) == 0);
    if (!equal) {
      fprintf(stderr, "Assertion failed\n");
      exit(1);
    }
  }
  return v_int(0);
}

Value builtin_test(Value* args, size_t argc) {
  if (argc != 2) {
    fprintf(stderr, "test() takes 2 arguments\n");
    return v_bool(false);
  }

  bool equal = false;
  if (args[0].type == VAL_INT && args[1].type == VAL_INT)
    equal = (args[0].i == args[1].i);
  else if (args[0].type == VAL_DOUBLE && args[1].type == VAL_DOUBLE)
    equal = (args[0].d == args[1].d);
  else if (args[0].type == VAL_BOOL && args[1].type == VAL_BOOL)
    equal = (args[0].b == args[1].b);
  else if (args[0].type == VAL_STRING && args[1].type == VAL_STRING)
    equal = (strcmp(args[0].s, args[1].s) == 0);
  if (equal) {
    printf("Ok\n");
  } else {
    printf("Fail\n");
  }
  fflush(stdout);
  return v_bool(equal);
}

Value builtin_len(Value* args, size_t argc) {
  if (argc != 1) return v_null();
  if (args[0].type == VAL_LIST) return v_int(args[0].list->size);
  if (args[0].type == VAL_TUPLE) return v_int(args[0].tuple->size);
  if (args[0].type == VAL_STRING) return v_int(strlen(args[0].s));
  return v_null();
}

Value builtin_range(Value* args, size_t argc) {
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
    if (args[0].type != VAL_INT || args[1].type != VAL_INT ||
        args[2].type != VAL_INT)
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

Value builtin_int(Value* args, size_t argc) {
  if (argc != 1) {
    return v_error("int() takes exactly 1 argument");
  }

  Value arg = args[0];

  if (arg.type == VAL_ANY && arg.any_val) {
    arg = *arg.any_val;
  }

  switch (arg.type) {
    case VAL_INT:
      return arg;
    case VAL_DOUBLE:
      return v_int((long long)arg.d);
    case VAL_BOOL:
      return v_int(arg.b ? 1 : 0);
    case VAL_PTR:
      return v_int((long long)arg.ptr);
    case VAL_STRING: {
      char* endptr;
      long long val = strtoll(arg.s, &endptr, 10);
      if (*endptr != '\0') {
        return v_error("cannot convert string to int: invalid format");
      }
      return v_int(val);
    }
    default:
      return v_error("cannot convert to int");
  }
}

Value builtin_double(Value* args, size_t argc) {
  if (argc != 1) {
    return v_error("double() takes exactly 1 argument");
  }

  Value arg = args[0];

  if (arg.type == VAL_ANY && arg.any_val) {
    arg = *arg.any_val;
  }

  switch (arg.type) {
    case VAL_DOUBLE:
      return arg;
    case VAL_INT:
      return v_double((double)arg.i);
    case VAL_BOOL:
      return v_double(arg.b ? 1.0 : 0.0);
    case VAL_STRING: {
      char* endptr;
      double val = strtod(arg.s, &endptr);
      if (*endptr != '\0') {
        return v_error("cannot convert string to double: invalid format");
      }
      return v_double(val);
    }
    default:
      return v_error("cannot convert to double");
  }
}

Value builtin_str(Value* args, size_t argc) {
  if (argc != 1) {
    return v_error("str() takes exactly 1 argument");
  }

  Value arg = args[0];

  if (arg.type == VAL_ANY && arg.any_val) {
    arg = *arg.any_val;
  }

  char buf[256];

  switch (arg.type) {
    case VAL_STRING:
      return arg;
    case VAL_INT:
      snprintf(buf, sizeof(buf), "%lld", arg.i);
      return v_str(buf);
    case VAL_DOUBLE:
      snprintf(buf, sizeof(buf), "%g", arg.d);
      return v_str(buf);
    case VAL_BOOL:
      return v_str(arg.b ? "True" : "False");
    case VAL_NULL:
      return v_str("None");
    case VAL_PTR:
      snprintf(buf, sizeof(buf), "<ptr:%p>", arg.ptr);
      return v_str(buf);
    case VAL_ERROR:
      snprintf(buf, sizeof(buf), "Error: %s", arg.s);
      return v_str(buf);
    default:
      return v_error("cannot convert to string");
  }
}

Value builtin_bool(Value* args, size_t argc) {
  if (argc != 1) {
    return v_error("bool() takes exactly 1 argument");
  }

  return v_bool(value_is_truthy(args[0]));
}

Value builtin_is_error(Value* args, size_t argc) {
  if (argc != 1) {
    return v_error("is_error() takes exactly 1 argument");
  }

  Value arg = args[0];
  if (arg.type == VAL_ANY && arg.any_val) {
    arg = *arg.any_val;
  }

  return v_bool(arg.type == VAL_ERROR);
}

Value builtin_is_null(Value* args, size_t argc) {
  if (argc != 1) {
    return v_error("is_null() takes exactly 1 argument");
  }

  Value arg = args[0];
  if (arg.type == VAL_ANY && arg.any_val) {
    arg = *arg.any_val;
  }

  return v_bool(arg.type == VAL_NULL ||
                (arg.type == VAL_PTR && arg.ptr == NULL));
}

Value builtin_ptr_to_int(Value* args, size_t argc) {
  if (argc != 1) {
    return v_error("ptr_to_int() takes exactly 1 argument");
  }

  Value arg = args[0];
  if (arg.type == VAL_ANY && arg.any_val) {
    arg = *arg.any_val;
  }

  if (arg.type == VAL_PTR) {
    return v_int((long long)arg.ptr);
  }

  return v_error("ptr_to_int() requires a pointer argument");
}

Value builtin_int_to_ptr(Value* args, size_t argc) {
  if (argc != 1) {
    return v_error("int_to_ptr() takes exactly 1 argument");
  }

  Value arg = args[0];
  if (arg.type == VAL_ANY && arg.any_val) {
    arg = *arg.any_val;
  }

  if (arg.type == VAL_INT) {
    return v_ptr((void*)arg.i);
  }

  return v_error("int_to_ptr() requires an integer argument");
}

Value builtin_tuple(Value* args, size_t argc) { return v_tuple(args, argc); }

Value builtin_any(Value* args, size_t argc) {
  if (argc != 1) {
    return v_error("any() takes exactly 1 argument");
  }
  return v_any(args[0]);
}

Value builtin_help(Value* args, size_t argc) {
  (void)args;
  (void)argc;
  printf("\n=== Built-in Functions ===\n");
  printf("print(...)     - Print values\n");
  printf(
      "                 Supports %% placeholders: print(\"Value: %%\", x)\n");
  printf("type(x)        - Get type of value\n");
  printf("assert(...)    - Asserts two expressions\n");
  printf("exit(...)      - Exits with exit code\n");
  printf("len(obj)       - Get length\n");
  printf("range(...)     - Create range list\n");
  printf("tuple(...)     - Create tuple\n");
  printf("any(x)         - Wrap value in any type\n");
  printf("help()         - This message\n");
  printf("\n=== Type Conversion ===\n");
  printf("int(x)         - Convert to integer\n");
  printf("double(x)      - Convert to double\n");
  printf("str(x)         - Convert to string\n");
  printf("bool(x)        - Convert to boolean\n");
  printf("is_error(x)    - Check if value is error\n");
  printf("is_null(x)     - Check if value is null/NULL pointer\n");
  printf("\n=== Pointer Operations ===\n");
  printf("ptr_to_int(p)  - Convert pointer to integer\n");
  printf("int_to_ptr(i)  - Convert integer to pointer\n");
  printf("\n=== FFI (Foreign Function Interface) ===\n");
  printf("link \"lib.so\"   - Load C shared library\n");
  printf("extern f = c_func(int, string): int - Declare C function\n");
  printf(
      "  Supported types: int, double, string, void, ptr, long, float, "
      "char, bool, any\n");
  printf("\n=== Working with Structs (via FFI) ===\n");
  printf("1. Create C wrapper functions that return/accept pointers\n");
  printf("2. Declare wrappers with 'extern'\n");
  printf("3. Use 'ptr' type for struct pointers\n");
  printf("Example:\n");
  printf("  extern new_vec = Vector2_new(float, float): ptr\n");
  printf("  extern get_x = Vector2_get_x(ptr): float\n");
  printf("  v = new_vec(10.0, 20.0)\n");
  printf("  x = get_x(v)\n");
  printf("\n=== String Interpolation ===\n");
  printf("Method 1 - {var} syntax: \"Hello {name}\"\n");
  printf("Method 2 - %% placeholder: print(\"Hello %%\", name)\n");
  printf("  Use {varname} in strings for variable interpolation\n");
  printf("  Use {{ }} and %%%% to escape braces and percent signs\n");
  printf("\n=== Syntax ===\n");
  printf("x = 10                   - Variable\n");
  printf("const pi = 3.14          - Constant variable\n");
  printf("f(x) = x * 2             - Function\n");
  printf("lambda x: x * 2          - Lambda expression\n");
  printf("import \"file.calc\"       - Import file\n");
  printf("nums = [1, 2, 3]         - List literal\n");
  printf("point = (10, 20)         - Tuple literal\n");
  printf("\n");
  fflush(stdout);
  return v_null();
}

Value eval(AST* a, Env* env);

void load_library(const char* path) {
  for (size_t i = 0; i < loaded_libs_count; i++) {
    if (!strcmp(loaded_libs[i].name, path)) {
      return;
    }
  }

  void* handle = dlopen(path, RTLD_LAZY);
  if (!handle) {
    fprintf(stderr, "Error loading library '%s': %s\n", path, dlerror());
    return;
  }

  if (loaded_libs_count >= loaded_libs_capacity) {
    size_t new_cap = loaded_libs_capacity == 0 ? 4 : loaded_libs_capacity * 2;
    LoadedLib* new_libs = realloc(loaded_libs, sizeof(LoadedLib) * new_cap);
    if (!new_libs) {
      dlclose(handle);
      return;
    }
    loaded_libs = new_libs;
    loaded_libs_capacity = new_cap;
  }

  loaded_libs[loaded_libs_count].name = strdup(path);
  loaded_libs[loaded_libs_count].handle = handle;
  loaded_libs_count++;

  printf("Loaded library: %s\n", path);
  fflush(stdout);
}

void* find_symbol(const char* name) {
  for (size_t i = 0; i < loaded_libs_count; i++) {
    void* sym = dlsym(loaded_libs[i].handle, name);
    if (sym) return sym;
  }
  return NULL;
}

FFIType parse_ffi_type(const char* type_name) {
  if (!strcmp(type_name, "int")) return FFI_INT;
  if (!strcmp(type_name, "double")) return FFI_DOUBLE;
  if (!strcmp(type_name, "string")) return FFI_STRING;
  if (!strcmp(type_name, "void")) return FFI_VOID;
  if (!strcmp(type_name, "ptr")) return FFI_PTR;
  if (!strcmp(type_name, "long")) return FFI_LONG;
  if (!strcmp(type_name, "float")) return FFI_FLOAT;
  if (!strcmp(type_name, "char")) return FFI_CHAR;
  if (!strcmp(type_name, "bool")) return FFI_BOOL;
  if (!strcmp(type_name, "any")) return FFI_ANY;
  return FFI_VOID;
}

void register_extern(const char* ccalc_name, const char* c_name,
                     FFIType* param_types, size_t param_count,
                     FFIType return_type) {
  void* func_ptr = find_symbol(c_name);
  if (!func_ptr) {
    fprintf(stderr, "Error: Symbol '%s' not found in loaded libraries\n",
            c_name);
    return;
  }

  if (extern_funcs_count >= extern_funcs_capacity) {
    size_t new_cap = extern_funcs_capacity == 0 ? 8 : extern_funcs_capacity * 2;
    ExternFunc* new_funcs = realloc(extern_funcs, sizeof(ExternFunc) * new_cap);
    if (!new_funcs) return;
    extern_funcs = new_funcs;
    extern_funcs_capacity = new_cap;
  }

  extern_funcs[extern_funcs_count].name = strdup(ccalc_name);
  extern_funcs[extern_funcs_count].c_name = strdup(c_name);
  extern_funcs[extern_funcs_count].func_ptr = func_ptr;
  extern_funcs[extern_funcs_count].param_types =
      malloc(sizeof(FFIType) * param_count);
  memcpy(extern_funcs[extern_funcs_count].param_types, param_types,
         sizeof(FFIType) * param_count);
  extern_funcs[extern_funcs_count].param_count = param_count;
  extern_funcs[extern_funcs_count].return_type = return_type;
  extern_funcs_count++;

  Function* ffi_func = xmalloc(sizeof(Function));
  ffi_func->is_builtin = false;
  ffi_func->arity = param_count;
  ffi_func->params = NULL;
  ffi_func->body = NULL;
  ffi_func->closure_env = NULL;

  env_set(global_env, ccalc_name, v_func(ffi_func), false);

  printf("Registered extern function: %s -> %s\n", ccalc_name, c_name);
  fflush(stdout);
}

ExternFunc* find_extern(const char* name) {
  for (size_t i = 0; i < extern_funcs_count; i++) {
    if (!strcmp(extern_funcs[i].name, name)) {
      return &extern_funcs[i];
    }
  }
  return NULL;
}

Value call_extern(ExternFunc* ext, Value* args) {
  if (!ext || !ext->func_ptr) {
    return v_error("extern function not found or not loaded");
  }

  void* func = ext->func_ptr;
  long long params[16] = {0};

  for (size_t i = 0; i < ext->param_count && i < 16; i++) {
    Value arg = args[i];

    if (arg.type == VAL_ANY && arg.any_val) {
      arg = *arg.any_val;
    }

    switch (ext->param_types[i]) {
      case FFI_ANY:
        if (arg.type == VAL_INT)
          params[i] = arg.i;
        else if (arg.type == VAL_DOUBLE) {
          memcpy(&params[i], &arg.d, sizeof(double));
        } else if (arg.type == VAL_BOOL)
          params[i] = arg.b ? 1 : 0;
        else if (arg.type == VAL_STRING)
          params[i] = (long long)arg.s;
        else if (arg.type == VAL_PTR)
          params[i] = (long long)arg.ptr;
        else
          params[i] = 0;
        break;

      case FFI_INT:
      case FFI_LONG:
      case FFI_CHAR:
      case FFI_BOOL:
        if (arg.type == VAL_INT)
          params[i] = arg.i;
        else if (arg.type == VAL_DOUBLE)
          params[i] = (long long)arg.d;
        else if (arg.type == VAL_BOOL)
          params[i] = arg.b ? 1 : 0;
        else
          return v_error("invalid argument type for FFI int parameter");
        break;

      case FFI_DOUBLE:
      case FFI_FLOAT: {
        double dval;
        if (arg.type == VAL_DOUBLE)
          dval = arg.d;
        else if (arg.type == VAL_INT)
          dval = (double)arg.i;
        else
          return v_error("invalid argument type for FFI double parameter");

        if (ext->param_types[i] == FFI_FLOAT) {
          float fval = (float)dval;
          memcpy(&params[i], &fval, sizeof(float));
        } else {
          memcpy(&params[i], &dval, sizeof(double));
        }
        break;
      }

      case FFI_STRING:
        if (arg.type == VAL_STRING)
          params[i] = (long long)arg.s;
        else
          return v_error("invalid argument type for FFI string parameter");
        break;

      case FFI_PTR:
        if (arg.type == VAL_PTR)
          params[i] = (long long)arg.ptr;
        else if (arg.type == VAL_STRING)
          params[i] = (long long)arg.s;
        else if (arg.type == VAL_INT)
          params[i] = arg.i;
        else
          params[i] = 0;
        break;

      case FFI_VOID:
        break;
    }
  }

  long long result;
  switch (ext->param_count) {
    case 0:
      result = ((long long (*)(void))func)();
      break;
    case 1:
      result = ((long long (*)(long long))func)(params[0]);
      break;
    case 2:
      result =
          ((long long (*)(long long, long long))func)(params[0], params[1]);
      break;
    case 3:
      result = ((long long (*)(long long, long long, long long))func)(
          params[0], params[1], params[2]);
      break;
    case 4:
      result =
          ((long long (*)(long long, long long, long long, long long))func)(
              params[0], params[1], params[2], params[3]);
      break;
    case 5:
      result = ((long long (*)(long long, long long, long long, long long,
                               long long))func)(params[0], params[1], params[2],
                                                params[3], params[4]);
      break;
    case 6:
      result = ((long long (*)(long long, long long, long long, long long,
                               long long, long long))func)(
          params[0], params[1], params[2], params[3], params[4], params[5]);
      break;
    case 7:
      result = ((long long (*)(long long, long long, long long, long long,
                               long long, long long, long long))func)(
          params[0], params[1], params[2], params[3], params[4], params[5],
          params[6]);
      break;
    case 8:
      result =
          ((long long (*)(long long, long long, long long, long long, long long,
                          long long, long long, long long))func)(
              params[0], params[1], params[2], params[3], params[4], params[5],
              params[6], params[7]);
      break;
    default:
      return v_error("FFI calls support max 8 parameters");
  }

  switch (ext->return_type) {
    case FFI_INT:
    case FFI_LONG:
    case FFI_CHAR:
    case FFI_BOOL:
    case FFI_ANY:
      return v_int(result);
    case FFI_DOUBLE:
    case FFI_FLOAT: {
      if (ext->return_type == FFI_FLOAT) {
        float fval;
        memcpy(&fval, &result, sizeof(float));
        return v_double((double)fval);
      } else {
        double dval;
        memcpy(&dval, &result, sizeof(double));
        return v_double(dval);
      }
    }
    case FFI_STRING:
      if (result == 0) return v_null();
      return v_str((const char*)result);
    case FFI_PTR:
      return v_ptr((void*)result);
    case FFI_VOID:
      return v_null();
  }

  return v_null();
}

Value call_values(Function* fn, Value* vals, size_t argc) {
  if (fn->is_builtin) {
    return fn->builtin(vals, argc);
  }

  if (argc < fn->arity) {
    Function* nf = xmalloc(sizeof(Function));
    *nf = *fn;
    nf->params += argc;
    nf->arity -= argc;
    return v_func(nf);
  }

  Env* local = env_new();
  local->next = fn->closure_env ? fn->closure_env : global_env;

  for (size_t i = 0; i < fn->arity; i++)
    env_set(local, fn->params[i], vals[i], false);

  Value result = eval(fn->body, local);
  if (result.cf == CF_RETURN) {
    result.cf = CF_NONE;
  }
  return result;
}

Value call(Function* fn, AST** args, size_t argc, Env* caller) {
  if (fn->is_builtin) {
    Value* vals = xmalloc(sizeof(Value) * argc);
    for (size_t i = 0; i < argc; i++) vals[i] = eval(args[i], caller);
    Value result = fn->builtin(vals, argc);
    return result;
  }

  if (argc < fn->arity) {
    Function* nf = xmalloc(sizeof(Function));
    *nf = *fn;
    nf->params += argc;
    nf->arity -= argc;
    return v_func(nf);
  }

  Value* vals = xmalloc(sizeof(Value) * fn->arity);
  for (size_t i = 0; i < fn->arity; i++) vals[i] = eval(args[i], caller);

  return call_values(fn, vals, fn->arity);
}

Value call_method(Value obj, const char* method, Value* args, size_t argc) {
  if (obj.type == VAL_ANY && obj.any_val) {
    return call_method(*obj.any_val, method, args, argc);
  }

  if (obj.type == VAL_STRUCT) {
    for (size_t i = 0; i < obj.struct_val->def->field_count; i++) {
      if (strcmp(method, obj.struct_val->def->fields[i]) == 0) {
        Value val = obj.struct_val->values[i];
        if (val.type == VAL_FUNC) {
          Value* new_args = xmalloc(sizeof(Value) * (argc + 1));
          new_args[0] = obj;
          for (size_t k = 0; k < argc; k++) new_args[k + 1] = args[k];
          Value result = call_values(val.fn, new_args, argc + 1);
          return result;
        }
        return val;
      }
    }

    for (size_t i = 0; i < obj.struct_val->def->method_count; i++) {
      if (strcmp(method, obj.struct_val->def->method_names[i]) == 0) {
        Value val_fn = v_func(obj.struct_val->def->methods[i]);
        Value* new_args = xmalloc(sizeof(Value) * (argc + 1));
        new_args[0] = obj;
        for (size_t k = 0; k < argc; k++) new_args[k + 1] = args[k];
        Value result = call_values(val_fn.fn, new_args, argc + 1);
        return result;
      }
    }

    return v_error("method/member not found");
  }

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
      char* p = buf;
      if (neg) *p++ = '-';
      *p++ = '0';
      *p++ = 'b';
      for (int j = i - 1; j >= 0; j--) *p++ = bits[j];
      *p = 0;
      return v_str(buf);
    } else if (!strcmp(method, "hex")) {
      char buf[64];
      sprintf(buf, "0x%llx", obj.i);
      return v_str(buf);
    }
  }
  if (obj.type == VAL_STRING) {
    if (!strcmp(method, "upper")) {
      char* s = xstrdup(obj.s);
      for (char* p = s; *p; p++) *p = toupper(*p);
      return v_str(s);
    } else if (!strcmp(method, "lower")) {
      char* s = xstrdup(obj.s);
      for (char* p = s; *p; p++) *p = tolower(*p);
      return v_str(s);
    }
  }
  if (obj.type == VAL_LIST) {
    if (!strcmp(method, "append") && argc == 1) {
      list_append(obj.list, args[0]);
      return v_null();
    } else if (!strcmp(method, "pop") && obj.list->size > 0) {
      return obj.list->items[--obj.list->size];
    }
  }
  return v_null();
}

char* value_to_str(Value v) {
  if (v.type == VAL_ANY && v.any_val) {
    return value_to_str(*v.any_val);
  }

  char buf[256];
  switch (v.type) {
    case VAL_INT:
      snprintf(buf, sizeof(buf), "%lld", v.i);
      return xstrdup(buf);
    case VAL_DOUBLE:
      snprintf(buf, sizeof(buf), "%g", v.d);
      return xstrdup(buf);
    case VAL_STRING:
      return xstrdup(v.s);
    case VAL_BOOL:
      return xstrdup(v.b ? "True" : "False");
    case VAL_NULL:
      return xstrdup("None");
    case VAL_PTR:
      snprintf(buf, sizeof(buf), "<ptr:%p>", v.ptr);
      return xstrdup(buf);
    case VAL_ERROR:
      snprintf(buf, sizeof(buf), "Error: %s", v.s);
      return xstrdup(buf);
    default:
      return xstrdup("<object>");
  }
}

Value eval(AST* a, Env* env) {
  switch (a->type) {
    case A_INT:
      return v_int(a->i);
    case A_DOUBLE:
      return v_double(a->d);
    case A_STRING:
      return v_str(a->s);
    case A_BOOL:
      return v_bool(a->b);
    case A_VAR:
      return env_get(env, a->name);
    case A_STRING_INTERP: {
      size_t total_len = 0;
      for (size_t i = 0; i <= a->str_interp.count; i++) {
        total_len += strlen(a->str_interp.parts[i]);
      }

      char** expr_strs = xmalloc(sizeof(char*) * a->str_interp.count);
      for (size_t i = 0; i < a->str_interp.count; i++) {
        Value expr_val = eval(a->str_interp.exprs[i], env);
        expr_strs[i] = value_to_str(expr_val);
        total_len += strlen(expr_strs[i]);
      }
      char* result = xmalloc(total_len + 1);
      result[0] = '\0';

      for (size_t i = 0; i <= a->str_interp.count; i++) {
        strcat(result, a->str_interp.parts[i]);
        if (i < a->str_interp.count) {
          strcat(result, expr_strs[i]);
        }
      }

      return v_str(result);
    }

    case A_LIST: {
      Value v = v_list();
      for (size_t i = 0; i < a->list.count; i++)
        list_append(v.list, eval(a->list.items[i], env));
      return v;
    }
    case A_TUPLE: {
      Value* items = xmalloc(sizeof(Value) * a->list.count);
      for (size_t i = 0; i < a->list.count; i++)
        items[i] = eval(a->list.items[i], env);
      return v_tuple(items, a->list.count);
    }
    case A_INDEX: {
      Value obj = eval(a->index.obj, env);
      Value idx = eval(a->index.idx, env);

      if (obj.type == VAL_ERROR) return obj;
      if (idx.type == VAL_ERROR) return idx;

      if (obj.type == VAL_ANY && obj.any_val) {
        obj = *obj.any_val;
      }

      if (obj.type == VAL_LIST && idx.type == VAL_INT) {
        if (idx.i < 0) {
          return v_error("list index cannot be negative");
        }
        if ((size_t)idx.i >= obj.list->size) {
          return v_error("list index out of range");
        }
        return obj.list->items[idx.i];
      }
      if (obj.type == VAL_TUPLE && idx.type == VAL_INT) {
        if (idx.i < 0) {
          return v_error("tuple index cannot be negative");
        }
        if ((size_t)idx.i >= obj.tuple->size) {
          return v_error("tuple index out of range");
        }
        return obj.tuple->items[idx.i];
      }
      if (obj.type == VAL_STRING && idx.type == VAL_INT) {
        size_t len = strlen(obj.s);
        if (idx.i < 0) {
          return v_error("string index cannot be negative");
        }
        if ((size_t)idx.i >= len) {
          return v_error("string index out of range");
        }
        char buf[2] = {obj.s[idx.i], '\0'};
        return v_str(buf);
      }
      return v_error("cannot index non-sequence or with non-integer");
    }
    case A_METHOD: {
      Value obj = eval(a->method.obj, env);
      Value* args = NULL;
      if (a->method.argc > 0) {
        args = xmalloc(sizeof(Value) * a->method.argc);
        for (size_t i = 0; i < a->method.argc; i++)
          args[i] = eval(a->method.args[i], env);
      }
      Value result = call_method(obj, a->method.method, args, a->method.argc);
      return result;
    }
    case A_BINOP: {
      Value l = eval(a->bin.l, env);
      Value r = eval(a->bin.r, env);

      if (l.type == VAL_ERROR) return l;
      if (r.type == VAL_ERROR) return r;

      if (l.type == VAL_ANY && l.any_val) {
        l = *l.any_val;
      }
      if (r.type == VAL_ANY && r.any_val) {
        r = *r.any_val;
      }

      if (a->bin.op == 'E') {
        if (l.type == VAL_INT && r.type == VAL_INT) return v_bool(l.i == r.i);
        if (l.type == VAL_BOOL && r.type == VAL_BOOL) return v_bool(l.b == r.b);
        if (l.type == VAL_PTR && r.type == VAL_PTR)
          return v_bool(l.ptr == r.ptr);
        if (l.type == VAL_STRING && r.type == VAL_STRING)
          return v_bool(strcmp(l.s, r.s) == 0);
        return v_bool(false);
      }
      if (a->bin.op == 'N') {
        if (l.type == VAL_INT && r.type == VAL_INT) return v_bool(l.i != r.i);
        if (l.type == VAL_BOOL && r.type == VAL_BOOL) return v_bool(l.b != r.b);
        if (l.type == VAL_PTR && r.type == VAL_PTR)
          return v_bool(l.ptr != r.ptr);
        if (l.type == VAL_STRING && r.type == VAL_STRING)
          return v_bool(strcmp(l.s, r.s) != 0);
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
        if (a->bin.op == '+') return v_double(ld + rd);
        if (a->bin.op == '-') return v_double(ld - rd);
        if (a->bin.op == '*') return v_double(ld * rd);
        if (a->bin.op == '/') {
          if (rd == 0.0) return v_error("division by zero");
          return v_double(ld / rd);
        }
        if (a->bin.op == '^') return v_double(pow(ld, rd));
      }
      if (l.type == VAL_INT && r.type == VAL_INT) {
        if (a->bin.op == '+') return v_int(l.i + r.i);
        if (a->bin.op == '-') return v_int(l.i - r.i);
        if (a->bin.op == '*') return v_int(l.i * r.i);
        if (a->bin.op == '/') {
          if (r.i == 0) return v_error("division by zero");
          return v_int(l.i / r.i);
        }
        if (a->bin.op == '%') {
          if (r.i == 0) return v_error("modulo by zero");
          return v_int(l.i % r.i);
        }
        if (a->bin.op == '^') {
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
      if (a->bin.op == '+' && l.type == VAL_STRING && r.type == VAL_STRING) {
        char* s = xmalloc(strlen(l.s) + strlen(r.s) + 1);
        strcpy(s, l.s);
        strcat(s, r.s);
        Value v;
        memset(&v, 0, sizeof(v));
        v.type = VAL_STRING;
        v.s = s;
        return v;
      }
      return v_error("invalid operand types for operation");
    }
    case A_CALL: {
      Value f = eval(a->call.fn, env);

      if (a->call.fn->type == A_VAR) {
        ExternFunc* ext = find_extern(a->call.fn->name);
        if (ext) {
          if (a->call.argc != ext->param_count) {
            return v_error("extern function argument count mismatch");
          }
          Value* vals = xmalloc(sizeof(Value) * a->call.argc);
          for (size_t i = 0; i < a->call.argc; i++)
            vals[i] = eval(a->call.args[i], env);
          return call_extern(ext, vals);
        }
      }

      if (f.type != VAL_FUNC) return v_null();
      return call(f.fn, a->call.args, a->call.argc, env);
    }
    case A_LAMBDA: {
      Function* f = xmalloc(sizeof(Function));
      f->params = a->lambda.params;
      f->arity = a->lambda.arity;
      f->body = a->lambda.body;
      f->is_builtin = false;
      f->closure_env = env;
      return v_func(f);
    }
    case A_ASSIGN: {
      Value v = eval(a->assign.value, env);
      env_set(env, a->assign.name, v, false);
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
    case A_STRUCT_DEF: {
      Value v;
      v.type = VAL_STRUCT_DEF;
      v.struct_def = xmalloc(sizeof(StructDef));
      v.struct_def->name = a->struct_def.name;
      v.struct_def->fields = a->struct_def.fields;
      v.struct_def->field_count = a->struct_def.count;

      size_t mcount = a->struct_def.method_count;
      v.struct_def->method_count = mcount;
      v.struct_def->methods = xmalloc(sizeof(Function*) * mcount);
      v.struct_def->method_names = xmalloc(sizeof(char*) * mcount);

      for (size_t i = 0; i < mcount; i++) {
        AST* assign = a->struct_def.methods[i];
        if (assign->type == A_ASSIGN) {
          Value val = eval(assign->assign.value, env);
          if (val.type == VAL_FUNC) {
            v.struct_def->methods[i] = val.fn;
            v.struct_def->method_names[i] = assign->assign.name;
          } else {
            v.struct_def->methods[i] = NULL;
          }
        }
      }

      env_set(env, a->struct_def.name, v, false);
      return v;
    }
    case A_STRUCT_INIT: {
      Value def_val = env_get(env, a->struct_init.name);
      if (def_val.type != VAL_STRUCT_DEF) {
        return v_error("struct not defined");
      }
      StructDef* def = def_val.struct_def;
      Value* values = xmalloc(sizeof(Value) * def->field_count);
      for (size_t i = 0; i < def->field_count; i++) values[i] = v_null();

      for (size_t i = 0; i < a->struct_init.count; i++) {
        char* fname = a->struct_init.fields[i];
        bool found = false;
        for (size_t j = 0; j < def->field_count; j++) {
          if (strcmp(fname, def->fields[j]) == 0) {
            values[j] = eval(a->struct_init.values[i], env);
            found = true;
            break;
          }
        }
        if (!found) {
          return v_error("field not found in struct");
        }
      }

      Value v;
      v.type = VAL_STRUCT;
      v.struct_val = xmalloc(sizeof(StructVal));
      v.struct_val->def = def;
      v.struct_val->values = values;
      return v;
    }
    case A_MEMBER: {
      Value obj = eval(a->member.obj, env);
      if (obj.type == VAL_STRUCT) {
        for (size_t i = 0; i < obj.struct_val->def->field_count; i++) {
          if (strcmp(a->member.member, obj.struct_val->def->fields[i]) == 0) {
            return obj.struct_val->values[i];
          }
        }
      }
      return call_method(obj, a->member.member, NULL, 0);
    }
    case A_ASSIGN_UNPACK: {
      Value rhs = eval(a->assign_unpack.value, env);
      if (rhs.type != VAL_TUPLE && rhs.type != VAL_LIST) {
        return v_error("cannot unpack non-sequence");
      }
      size_t count = (rhs.type == VAL_TUPLE) ? rhs.tuple->size : rhs.list->size;
      Value* items =
          (rhs.type == VAL_TUPLE) ? rhs.tuple->items : rhs.list->items;

      if (count != a->assign_unpack.count) {
        return v_error("unpacking count mismatch");
      }

      for (size_t i = 0; i < count; i++) {
        env_set(env, a->assign_unpack.names[i], items[i], false);
      }
      return rhs;
    }
  }
  return v_null();
}

Function* make_builtin(Value (*fn)(Value*, size_t)) {
  Function* f = xmalloc(sizeof(Function));
  f->is_builtin = true;
  f->builtin = fn;
  f->arity = 0;
  f->closure_env = NULL;
  return f;
}

void run_file(const char* filename);

void run_repl(void) {
  char line[2048];
  while (1) {
    printf("λ ");
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin)) break;
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[len - 1] = 0;
    char* trimmed = line;
    while (*trimmed && isspace(*trimmed)) trimmed++;
    if (strlen(trimmed) == 0 || trimmed[0] == '#') continue;
    if (!strcmp(trimmed, "quit") || !strcmp(trimmed, "exit")) {
      printf("\n");
      break;
    }

    src = line;
    src_start = line;
    current_loc.filename = "<stdin>";
    current_loc.line = 1;
    current_loc.column = 1;
    errors_occurred = false;

    next_token();

    if (tok.type == T_IMPORT) {
      next_token();
      if (tok.type != T_STRING) {
        error_at(tok.loc, "import requires a filename string");
        continue;
      }
      char filename[256];
      strcpy(filename, tok.text);
      next_token();
      bool saved_import_mode = import_mode;
      import_mode = true;
      run_file(filename);
      import_mode = saved_import_mode;
      continue;
    }

    if (tok.type == T_LINK) {
      next_token();
      if (tok.type != T_STRING) {
        error_at(tok.loc, "link requires a library path string");
        continue;
      }
      char libpath[256];
      strcpy(libpath, tok.text);
      next_token();
      load_library(libpath);
      continue;
    }

    if (tok.type == T_EXTERN) {
      next_token();
      if (tok.type != T_IDENT) {
        error_at(tok.loc, "extern requires function name");
        continue;
      }
      char ccalc_name[256];
      strcpy(ccalc_name, tok.text);
      next_token();

      if (tok.type != T_ASSIGN) {
        error_at(tok.loc, "expected '=' after extern function name");
        continue;
      }
      next_token();

      if (tok.type != T_IDENT) {
        error_at(tok.loc, "expected C function name");
        continue;
      }
      char c_name[256];
      strcpy(c_name, tok.text);
      next_token();

      if (tok.type != T_LP) {
        error_at(tok.loc, "expected '(' for parameter types");
        continue;
      }
      next_token();

      FFIType param_types[16];
      size_t param_count = 0;

      while (tok.type == T_IDENT && param_count < 16) {
        param_types[param_count++] = parse_ffi_type(tok.text);
        next_token();
        if (tok.type == T_COMMA)
          next_token();
        else
          break;
      }

      if (tok.type != T_RP) {
        error_at(tok.loc, "expected ')' after parameters");
        continue;
      }
      next_token();

      if (tok.type != T_COLON) {
        error_at(tok.loc, "expected ':' before return type");
        continue;
      }
      next_token();

      if (tok.type != T_IDENT) {
        error_at(tok.loc, "expected return type");
        continue;
      }
      FFIType return_type = parse_ffi_type(tok.text);
      next_token();

      register_extern(ccalc_name, c_name, param_types, param_count,
                      return_type);
      continue;
    }

    bool is_const = false;
    if (tok.type == T_CONST) {
      is_const = true;
      next_token();
    }

    if (tok.type == T_IDENT) {
      char name[256];
      strcpy(name, tok.text);
      next_token();
      if (tok.type == T_LP) {
        next_token();
        char** params = xmalloc(sizeof(char*) * 16);
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
              if (tok.type == T_LP)
                depth++;
              else if (tok.type == T_RP)
                depth--;
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
            AST* body = parse_expr();
            if (!errors_occurred) {
              AST* lambda = ast_new(A_LAMBDA);
              lambda->lambda.params = params;
              lambda->lambda.arity = n;
              lambda->lambda.body = body;
              Value fn = eval(lambda, global_env);
              env_set(global_env, name, fn, is_const);
            }
            continue;
          }
        }
        src = line;
        current_loc.column = 1;
        next_token();
      } else if (tok.type == T_ASSIGN) {
        next_token();
        AST* expr = parse_expr();
        if (!errors_occurred) {
          Value v = eval(expr, global_env);
          env_set(global_env, name, v, is_const);
        }
        continue;
      }
    }

    src = line;
    current_loc.column = 1;
    next_token();
    AST* e = parse_stmt();
    if (errors_occurred) continue;

    Value v = eval(e, global_env);

    const char* color = value_type_color(v);
    const char* reset = use_colors ? COLOR_RESET : "";

    if (v.type == VAL_INT)
      printf("%s%lld%s\n", color, v.i, reset);
    else if (v.type == VAL_DOUBLE)
      printf("%s%g%s\n", color, v.d, reset);
    else if (v.type == VAL_STRING)
      printf("%s\"%s\"%s\n", color, v.s, reset);
    else if (v.type == VAL_BOOL)
      printf("%s%s%s\n", color, v.b ? "True" : "False", reset);
    else if (v.type == VAL_ERROR)
      printf("%sError: %s%s\n", color, v.s, reset);
    else if (v.type == VAL_PTR)
      printf("%s<ptr:%p>%s\n", color, v.ptr, reset);
    else if (v.type == VAL_ANY) {
      printf("%s<any:", color);
      if (v.any_val)
        print_value(*v.any_val);
      else
        printf("null");
      printf(">%s\n", reset);
    } else if (v.type == VAL_TUPLE) {
      printf("%s(%s", color, reset);
      for (size_t i = 0; i < v.tuple->size; i++) {
        if (i > 0) printf(", ");
        const char* item_color = value_type_color(v.tuple->items[i]);
        if (v.tuple->items[i].type == VAL_INT)
          printf("%s%lld%s", item_color, v.tuple->items[i].i, reset);
        else if (v.tuple->items[i].type == VAL_DOUBLE)
          printf("%s%g%s", item_color, v.tuple->items[i].d, reset);
        else if (v.tuple->items[i].type == VAL_STRING)
          printf("%s\"%s\"%s", item_color, v.tuple->items[i].s, reset);
        else if (v.tuple->items[i].type == VAL_PTR)
          printf("%s<ptr:%p>%s", item_color, v.tuple->items[i].ptr, reset);
      }
      printf("%s)%s\n", color, reset);
    } else if (v.type == VAL_LIST) {
      printf("%s[%s", color, reset);
      for (size_t i = 0; i < v.list->size; i++) {
        if (i > 0) printf(", ");
        const char* item_color = value_type_color(v.list->items[i]);
        if (v.list->items[i].type == VAL_INT)
          printf("%s%lld%s", item_color, v.list->items[i].i, reset);
        else if (v.list->items[i].type == VAL_DOUBLE)
          printf("%s%g%s", item_color, v.list->items[i].d, reset);
        else if (v.list->items[i].type == VAL_STRING)
          printf("%s\"%s\"%s", item_color, v.list->items[i].s, reset);
        else if (v.list->items[i].type == VAL_PTR)
          printf("%s<ptr:%p>%s", item_color, v.list->items[i].ptr, reset);
      }
      printf("%s]%s\n", color, reset);
    }
    fflush(stdout);
  }
}

void run_file(const char* filename);

char* resolve_import_path(const char* import_name, const char* current_file) {
  static char resolved[512];

  if (import_name[0] == '/') {
    strcpy(resolved, import_name);
    FILE* f = fopen(resolved, "r");
    if (f) {
      fclose(f);
      return resolved;
    }
  }

  if (current_file && strchr(current_file, '/')) {
    const char* last_slash = strrchr(current_file, '/');
    size_t dir_len = last_slash - current_file + 1;
    strncpy(resolved, current_file, dir_len);
    resolved[dir_len] = '\0';
    strcat(resolved, import_name);

    FILE* f = fopen(resolved, "r");
    if (f) {
      fclose(f);
      return resolved;
    }
  }

  strcpy(resolved, import_name);
  FILE* f = fopen(resolved, "r");
  if (f) {
    fclose(f);
    return resolved;
  }

  strcpy(resolved, "stdlib/");
  strcat(resolved, import_name);
  f = fopen(resolved, "r");
  if (f) {
    fclose(f);
    return resolved;
  }

  return NULL;
}

void run_file(const char* filename) {
  FILE* f = fopen(filename, "r");
  if (!f) {
    fprintf(stderr, "%s:1:1: error: could not open file\n", filename);
    return;
  }
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* content = xmalloc(fsize + 1);
  size_t bytes_read = fread(content, 1, fsize, f);
  content[bytes_read] = 0;
  fclose(f);

  src = content;
  src_start = content;
  current_loc.filename = xstrdup(filename);
  current_loc.line = 1;
  current_loc.column = 1;
  errors_occurred = false;

  next_token();

  while (tok.type != T_EOF) {
    if (tok.type == T_ERROR) {
      next_token();
      continue;
    }

    if (tok.type == T_IMPORT) {
      next_token();
      if (tok.type != T_STRING) {
        error_at(tok.loc, "import requires a filename string");
        next_token();
        continue;
      }

      const char* import_name = tok.text;
      char* resolved_path =
          resolve_import_path(import_name, current_loc.filename);

      if (!resolved_path) {
        error_at(tok.loc, "could not find import file: %s", import_name);
        next_token();
        continue;
      }

      if (is_file_imported(resolved_path)) {
        next_token();
        continue;
      }

      mark_file_imported(resolved_path);
      next_token();

      const char* saved_src = src;
      const char* saved_src_start = src_start;
      Token saved_tok = tok;
      SourceLoc saved_loc = current_loc;

      bool saved_import_mode = import_mode;
      import_mode = true;
      run_file(resolved_path);
      import_mode = saved_import_mode;

      src = saved_src;
      src_start = saved_src_start;
      tok = saved_tok;
      current_loc = saved_loc;

      continue;
    }

    if (tok.type == T_LINK) {
      next_token();
      if (tok.type != T_STRING) {
        error_at(tok.loc, "link requires a library path string");
        next_token();
        continue;
      }
      char libpath[256];
      strcpy(libpath, tok.text);
      next_token();
      load_library(libpath);
      continue;
    }

    if (tok.type == T_EXTERN) {
      next_token();
      if (tok.type != T_IDENT) {
        error_at(tok.loc, "extern requires function name");
        next_token();
        continue;
      }
      char ccalc_name[256];
      strcpy(ccalc_name, tok.text);
      next_token();

      if (tok.type != T_ASSIGN) {
        error_at(tok.loc, "expected '=' after extern function name");
        next_token();
        continue;
      }
      char c_name[256];
      strcpy(c_name, tok.text);
      next_token();

      if (tok.type != T_LP) {
        error_at(tok.loc, "expected '(' for parameter types");
        next_token();
        continue;
      }
      next_token();

      FFIType param_types[16];
      size_t param_count = 0;

      while (tok.type == T_IDENT && param_count < 16) {
        param_types[param_count++] = parse_ffi_type(tok.text);
        next_token();
        if (tok.type == T_COMMA)
          next_token();
        else
          break;
      }

      if (tok.type != T_RP) {
        error_at(tok.loc, "expected ')' after parameters");
        next_token();
        continue;
      }
      next_token();

      if (tok.type != T_COLON) {
        error_at(tok.loc, "expected ':' before return type");
        next_token();
        continue;
      }
      next_token();

      if (tok.type != T_IDENT) {
        error_at(tok.loc, "expected return type");
        next_token();
        continue;
      }
      FFIType return_type = parse_ffi_type(tok.text);
      next_token();

      register_extern(ccalc_name, c_name, param_types, param_count,
                      return_type);
      continue;
    }

    bool is_const = false;
    if (tok.type == T_CONST) {
      is_const = true;
      next_token();
    }

    AST* stmt = parse_stmt();
    if (errors_occurred) {
      errors_occurred = false;
      while (tok.type != T_SEMI && tok.type != T_EOF) {
        next_token();
      }
      if (tok.type == T_SEMI) next_token();
      continue;
    }

    if (stmt->type == A_VAR && tok.type == T_ASSIGN) {
      char* name = stmt->name;
      next_token();
      AST* expr = parse_expr();
      if (!errors_occurred) {
        Value v = eval(expr, global_env);
        env_set(global_env, name, v, is_const);
      }
    } else if (stmt->type == A_CALL && stmt->call.fn->type == A_VAR &&
               tok.type == T_ASSIGN) {
      char* name = stmt->call.fn->name;
      AST** args = stmt->call.args;
      size_t argc = stmt->call.argc;

      char** params = xmalloc(sizeof(char*) * argc);
      bool all_idents = true;
      for (size_t i = 0; i < argc; i++) {
        if (args[i]->type == A_VAR) {
          params[i] = args[i]->name;
        } else {
          error_at(args[i]->loc, "function parameters must be identifiers");
          all_idents = false;
          break;
        }
      }

      if (all_idents) {
        next_token();
        AST* body = parse_expr();
        if (!errors_occurred) {
          AST* lambda = ast_new(A_LAMBDA);
          lambda->lambda.params = params;
          lambda->lambda.arity = argc;
          lambda->lambda.body = body;
          Value fn = eval(lambda, global_env);
          env_set(global_env, name, fn, is_const);
        }
      }
    } else {
      if (!import_mode && !errors_occurred) {
        eval(stmt, global_env);
      }
    }

    if (tok.type == T_SEMI) {
      next_token();
    }
  }
}

int main(int argc, char** argv) {
  global_arena = arena_new(65536);
  global_env = env_new();
  init_import_tracker();

  int file_arg = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--color")) {
      use_colors = true;
    } else if (!strcmp(argv[i], "--help")) {
      printf("Usage: %s [options] [file]\n", argv[0]);
      printf("Options:\n");
      printf("  --color    Enable colored output\n");
      printf("  --help     Show this help message\n");
      return 0;
    } else {
      file_arg = i;
    }
  }

  env_set(global_env, "print", v_func(make_builtin(builtin_print)), true);
  env_set(global_env, "type", v_func(make_builtin(builtin_type)), true);
  env_set(global_env, "len", v_func(make_builtin(builtin_len)), true);
  env_set(global_env, "range", v_func(make_builtin(builtin_range)), true);
  env_set(global_env, "tuple", v_func(make_builtin(builtin_tuple)), true);
  env_set(global_env, "help", v_func(make_builtin(builtin_help)), true);
  env_set(global_env, "assert", v_func(make_builtin(builtin_assert)), true);
  env_set(global_env, "exit", v_func(make_builtin(builtin_exit)), true);
  env_set(global_env, "test", v_func(make_builtin(builtin_test)), true);

  env_set(global_env, "int", v_func(make_builtin(builtin_int)), true);
  env_set(global_env, "double", v_func(make_builtin(builtin_double)), true);
  env_set(global_env, "str", v_func(make_builtin(builtin_str)), true);
  env_set(global_env, "bool", v_func(make_builtin(builtin_bool)), true);
  env_set(global_env, "is_error", v_func(make_builtin(builtin_is_error)), true);
  env_set(global_env, "is_null", v_func(make_builtin(builtin_is_null)), true);
  env_set(global_env, "ptr_to_int", v_func(make_builtin(builtin_ptr_to_int)),
          true);
  env_set(global_env, "int_to_ptr", v_func(make_builtin(builtin_int_to_ptr)),
          true);
  env_set(global_env, "any", v_func(make_builtin(builtin_any)), true);

  if (file_arg > 0) {
    run_file(argv[file_arg]);
  } else {
    printf("λ-calculus REPL with FFI, Closures, and Tuples\n");
    printf("Type 'help()' for syntax or 'quit' to exit\n\n");
    run_repl();
  }
  arena_free(global_arena);
  return errors_occurred ? 1 : 0;
}
