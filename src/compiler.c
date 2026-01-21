#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h> // Required for error()
#include <stdbool.h>
#include "mylolib.h"
#include "vm.h"
#include "utils.h"
#include "defines.h"

// --- Tokenizer & Structures ---

typedef enum {
    TK_FN, TK_VAR, TK_IF, TK_FOR, TK_RET, TK_PRINT, TK_IN, TK_STRUCT, TK_ELSE,
    TK_MOD, // 'mod' keyword
    TK_IMPORT,
    TK_ID, TK_NUM, TK_STR, TK_RANGE,
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET,
    TK_PLUS, TK_MINUS,
    TK_LT, TK_GT, TK_LE, TK_GE, TK_EQ, TK_NEQ,
    TK_EQ_ASSIGN, TK_QUESTION,
    TK_EOF, TK_FSTR,
    TK_COLON, TK_COMMA, TK_DOT,
    TK_SCOPE, // ::
    TK_ARROW, // ->
    TK_BREAK, TK_CONTINUE,
    TK_ENUM,
    TK_MODULE_PATH,
    TK_TRUE, TK_FALSE,
    TK_MUL, TK_DIV,
    TK_MOD_OP // % operator
} MyloTokenType;

typedef struct {
    MyloTokenType type;
    char text[MAX_IDENTIFIER];
    double val_float;
    int line; // New: Track line number for this token
} Token;

typedef struct {
    char name[MAX_IDENTIFIER];
    char type[MAX_IDENTIFIER];
} FFIArg;

typedef struct {
    int id;
    char code_body[MAX_C_BLOCK_SIZE];
    FFIArg args[MAX_FFI_ARGS];
    int arg_count;
    char return_type[MAX_IDENTIFIER];
} FFIBlock;

FFIBlock ffi_blocks[MAX_NATIVES];
int ffi_count = 0;

// Internal Compiler State
static char *src;
static Token curr;
static bool inside_function = false;
static char current_namespace[MAX_IDENTIFIER] = "";
static int line = 1; // Global line counter

// Search Paths
char search_paths[MAX_SEARCH_PATHS][MAX_STRING_LENGTH];
int search_path_count = 0;

// Loop Control Stack
typedef struct {
    int break_patches[MAX_JUMPS_PER_LOOP];
    int break_count;
    int continue_patches[MAX_JUMPS_PER_LOOP];
    int continue_count;
} LoopControl;

LoopControl loop_stack[MAX_LOOP_NESTING];
int loop_depth = 0;

// Symbol Tables
struct {
    char name[MAX_IDENTIFIER];
    int addr;
    int type_id;
    bool is_array;
} globals[MAX_GLOBALS];

int global_count = 0;

struct {
    char name[MAX_IDENTIFIER];
    int offset;
    int type_id;
    bool is_array;
} locals[MAX_GLOBALS];

int local_count = 0;

struct {
    char name[MAX_IDENTIFIER];
    int addr;
} funcs[MAX_GLOBALS];

int func_count = 0;

struct StructDef {
    char name[MAX_IDENTIFIER];
    char fields[MAX_FIELDS][MAX_IDENTIFIER];
    int field_count;
} struct_defs[MAX_STRUCTS];

int struct_count = 0;

// Enum Table
struct {
    char name[MAX_IDENTIFIER];
    int value;
} enum_entries[MAX_ENUM_MEMBERS];

int enum_entry_count = 0;

// --- Forward Declarations ---
void parse_internal(char *source, bool is_import);

void parse_struct_literal(int struct_idx);

void parse_map_literal();

// --- Error Handling ---
void error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[Line %d] Error: ", curr.line > 0 ? curr.line : line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

// --- Helper Prototypes ---
void emit(int op) {
    if (vm.code_size >= MAX_CODE) error("Code overflow");
    vm.bytecode[vm.code_size++] = op;
}

int find_local(char *name) {
    for (int i = 0; i < local_count; i++) if (strcmp(locals[i].name, name) == 0) return i;
    return -1;
}

int find_global(char *name) {
    for (int i = 0; i < global_count; i++) if (strcmp(globals[i].name, name) == 0) return i;
    return -1;
}

int find_func(char *name) {
    for (int i = 0; i < func_count; i++) if (strcmp(funcs[i].name, name) == 0) return funcs[i].addr;
    return -1;
}

int find_struct(char *name) {
    for (int i = 0; i < struct_count; i++) if (strcmp(struct_defs[i].name, name) == 0) return i;
    return -1;
}

int find_field(int struct_idx, char *field) {
    for (int i = 0; i < struct_defs[struct_idx].field_count; i++) if (
        strcmp(struct_defs[struct_idx].fields[i], field) == 0) return i;
    return -1;
}

int find_enum_val(char *name) {
    for (int i = 0; i < enum_entry_count; i++) {
        if (strcmp(enum_entries[i].name, name) == 0) return enum_entries[i].value;
    }
    return -1;
}

int find_stdlib_func(char *name) {
    int i = 0;
    while (std_library[i].name != NULL) {
        if (strcmp(std_library[i].name, name) == 0) return i;
        i++;
    }
    return -1;
}

void get_mangled_name(char *out, char *raw_name) {
    if (strlen(current_namespace) > 0) {
        sprintf(out, "%s_%s", current_namespace, raw_name);
    } else {
        strcpy(out, raw_name);
    }
}

// --- Loop Control Helpers ---
void push_loop() {
    if (loop_depth >= MAX_LOOP_NESTING) error("Loop nesting too deep");
    loop_stack[loop_depth].break_count = 0;
    loop_stack[loop_depth].continue_count = 0;
    loop_depth++;
}

void pop_loop(int continue_addr, int break_addr) {
    loop_depth--;
    LoopControl *loop = &loop_stack[loop_depth];
    for (int i = 0; i < loop->continue_count; i++) {
        vm.bytecode[loop->continue_patches[i]] = continue_addr;
    }
    for (int i = 0; i < loop->break_count; i++) {
        vm.bytecode[loop->break_patches[i]] = break_addr;
    }
}

void emit_break() {
    if (loop_depth == 0) error("'break' outside of loop");
    LoopControl *loop = &loop_stack[loop_depth - 1];
    if (loop->break_count >= MAX_JUMPS_PER_LOOP) error("Too many 'break' statements");
    emit(OP_JMP);
    loop->break_patches[loop->break_count++] = vm.code_size;
    emit(0);
}

void emit_continue() {
    if (loop_depth == 0) error("'continue' outside of loop");
    LoopControl *loop = &loop_stack[loop_depth - 1];
    if (loop->continue_count >= MAX_JUMPS_PER_LOOP) error("Too many 'continue' statements");
    emit(OP_JMP);
    loop->continue_patches[loop->continue_count++] = vm.code_size;
    emit(0);
}

// --- Lexer & Parser Implementation ---

void next_token() {
    while (1) {
        unsigned char c = (unsigned char) *src;
        if (isspace(c)) {
            if (c == '\n') line++;
            src++;
            continue;
        }
        if (*src == '/' && *(src + 1) == '/') {
            while (*src != '\n' && *src != '\0') src++;
            continue;
        }
        break;
    }

    curr.line = line; // Record line number for the new token

    if (*src == 'f' && *(src + 1) == '"') {
        src += 2;
        char *start = src;
        int brace_depth = 0;
        while (*src) {
            if (*src == '"' && brace_depth == 0) break;
            if (*src == '\n') line++; // Handle multiline F-strings
            if (*src == '{') brace_depth++;
            else if (*src == '}') { if (brace_depth > 0) brace_depth--; }
            src++;
        }
        int len = (int) (src - start);
        if (len > MAX_IDENTIFIER - 1) len = MAX_IDENTIFIER - 1;
        strncpy(curr.text, start, len);
        curr.text[len] = '\0';
        if (*src == '"') src++;
        curr.type = TK_FSTR;
        return;
    }

    if (*src == 0) {
        curr.type = TK_EOF;
        return;
    }

    if (isdigit((unsigned char)*src) || (*src == '.' && isdigit((unsigned char)*(src+1)))) {
        curr.type = TK_NUM;
        char *end;
        curr.val_float = strtod(src, &end);
        if (end > src && *(end - 1) == '.' && *end == '.') end--;
        int len = (int) (end - src);
        if (len > MAX_IDENTIFIER - 1) len = MAX_IDENTIFIER - 1;
        strncpy(curr.text, src, len);
        curr.text[len] = '\0';
        src = end;
        return;
    }

    if (isalpha((unsigned char)*src)) {
        char *start = src;
        while (isalnum((unsigned char)*src) || *src == '_') src++;
        int len = (int) (src - start);
        if (len > MAX_IDENTIFIER - 1) len = MAX_IDENTIFIER - 1;
        strncpy(curr.text, start, len);
        curr.text[len] = '\0';
        if (strcmp(curr.text, "fn") == 0) curr.type = TK_FN;
        else if (strcmp(curr.text, "var") == 0) curr.type = TK_VAR;
        else if (strcmp(curr.text, "struct") == 0) curr.type = TK_STRUCT;
        else if (strcmp(curr.text, "if") == 0) curr.type = TK_IF;
        else if (strcmp(curr.text, "else") == 0) curr.type = TK_ELSE;
        else if (strcmp(curr.text, "for") == 0) curr.type = TK_FOR;
        else if (strcmp(curr.text, "in") == 0) curr.type = TK_IN;
        else if (strcmp(curr.text, "ret") == 0) curr.type = TK_RET;
        else if (strcmp(curr.text, "print") == 0) curr.type = TK_PRINT;
        else if (strcmp(curr.text, "mod") == 0) curr.type = TK_MOD;
        else if (strcmp(curr.text, "import") == 0) curr.type = TK_IMPORT;
        else if (strcmp(curr.text, "break") == 0) curr.type = TK_BREAK;
        else if (strcmp(curr.text, "continue") == 0) curr.type = TK_CONTINUE;
        else if (strcmp(curr.text, "enum") == 0) curr.type = TK_ENUM;
        else if (strcmp(curr.text, "module_path") == 0) curr.type = TK_MODULE_PATH;
        else if (strcmp(curr.text, "true") == 0) curr.type = TK_TRUE;
        else if (strcmp(curr.text, "false") == 0) curr.type = TK_FALSE;
        else curr.type = TK_ID;
        return;
    }
    if (*src == '"') {
        src++;
        char *start = src;
        while (*src != '"' && *src != 0) {
            if (*src == '\n') line++;
            src++;
        }
        int len = (int) (src - start);
        if (len > MAX_IDENTIFIER - 1) len = MAX_IDENTIFIER - 1;
        strncpy(curr.text, start, len);
        curr.text[len] = '\0';
        if (*src == '"') src++;
        curr.type = TK_STR;
        return;
    }
    if (strncmp(src, "...", 3) == 0) {
        src += 3;
        curr.type = TK_RANGE;
        return;
    }
    if (strncmp(src, "::", 2) == 0) {
        src += 2;
        curr.type = TK_SCOPE;
        return;
    }
    if (strncmp(src, "->", 2) == 0) {
        src += 2;
        curr.type = TK_ARROW;
        return;
    }

    switch (*src++) {
        case '(': curr.type = TK_LPAREN;
            break;
        case ')': curr.type = TK_RPAREN;
            break;
        case '{': curr.type = TK_LBRACE;
            break;
        case '}': curr.type = TK_RBRACE;
            break;
        case '[': curr.type = TK_LBRACKET;
            break;
        case ']': curr.type = TK_RBRACKET;
            break;
        case '+': curr.type = TK_PLUS;
            break;
        case '-': curr.type = TK_MINUS;
            break;
        case '*': curr.type = TK_MUL;
            break;
        case '/': curr.type = TK_DIV;
            break;
        case '%': curr.type = TK_MOD_OP;
            break;
        case ':': curr.type = TK_COLON;
            break;
        case ',': curr.type = TK_COMMA;
            break;
        case '.': curr.type = TK_DOT;
            break;
        case '?': curr.type = TK_QUESTION;
            break;
        case '<': if (*src == '=') {
                src++;
                curr.type = TK_LE;
            } else curr.type = TK_LT;
            break;
        case '>': if (*src == '=') {
                src++;
                curr.type = TK_GE;
            } else curr.type = TK_GT;
            break;
        case '!': if (*src == '=') {
                src++;
                curr.type = TK_NEQ;
            } else error("Unexpected char '!'");
            break;
        case '=': if (*src == '=') {
                src++;
                curr.type = TK_EQ;
            } else curr.type = TK_EQ_ASSIGN;
            break;
        default: error("Unknown char '%c'", *(src - 1));
    }
}

void match(MyloTokenType t) {
    if (curr.type == t) next_token();
    else error("Expected token type %d, got %d ('%s')", t, curr.type, curr.text);
}

void expression();

void statement();

bool parse_namespaced_id(char *out_name) {
    strcpy(out_name, curr.text);
    match(TK_ID);
    if (curr.type == TK_SCOPE) {
        match(TK_SCOPE);
        char sub[64];
        strcpy(sub, curr.text);
        match(TK_ID);
        char combined[128];
        sprintf(combined, "%s_%s", out_name, sub);
        strcpy(out_name, combined);
        return true;
    }
    return false;
}

void factor() {
    if (curr.type == TK_NUM) {
        int idx = make_const(curr.val_float);
        emit(OP_PSH_NUM);
        emit(idx);
        match(TK_NUM);
    } else if (curr.type == TK_STR) {
        int id = make_string(curr.text);
        emit(OP_PSH_STR);
        emit(id);
        match(TK_STR);
    } else if (curr.type == TK_TRUE) {
        emit(OP_PSH_NUM);
        emit(make_const(1.0));
        match(TK_TRUE);
    } else if (curr.type == TK_FALSE) {
        emit(OP_PSH_NUM);
        emit(make_const(0.0));
        match(TK_FALSE);
    } else if (curr.type == TK_MINUS) {
        match(TK_MINUS);
        if (curr.type == TK_NUM) {
            int idx = make_const(-curr.val_float);
            emit(OP_PSH_NUM);
            emit(idx);
            match(TK_NUM);
        } else {
            emit(OP_PSH_NUM);
            emit(make_const(0.0));
            factor();
            emit(OP_SUB);
        }
    } else if (curr.type == TK_LBRACKET) {
        // Array Literal
        match(TK_LBRACKET);
        int count = 0;
        if (curr.type != TK_RBRACKET) {
            expression();
            count++;
            while (curr.type == TK_COMMA) {
                match(TK_COMMA);
                expression();
                count++;
            }
        }
        match(TK_RBRACKET);
        emit(OP_ARR);
        emit(count);
    } else if (curr.type == TK_LBRACE) {
        char *safe_src = src;
        Token safe_curr = curr;
        int safe_line = line;
        match(TK_LBRACE);
        bool is_map = (curr.type == TK_STR);
        src = safe_src;
        curr = safe_curr;
        line = safe_line;

        if (is_map) { parse_map_literal(); } else {
            match(TK_LBRACE);
            char first_field[MAX_IDENTIFIER];
            strcpy(first_field, curr.text);
            src = safe_src;
            curr = safe_curr;
            line = safe_line;
            int st_idx = -1;
            for (int s = 0; s < struct_count; s++) {
                if (find_field(s, first_field) != -1) {
                    st_idx = s;
                    break;
                }
            }
            if (st_idx != -1) parse_struct_literal(st_idx);
            else error("Could not infer struct type from field '%s'", first_field);
        }
    } else if (curr.type == TK_ID && strcmp(curr.text, "C") == 0) {
        match(TK_ID);
        int ffi_idx = ffi_count++;
        ffi_blocks[ffi_idx].id = ffi_idx;
        ffi_blocks[ffi_idx].arg_count = 0;
        ffi_blocks[ffi_idx].return_type[0] = '\0';
        if (curr.type == TK_LPAREN) {
            match(TK_LPAREN);
            while (curr.type != TK_RPAREN) {
                if (ffi_blocks[ffi_idx].arg_count >= MAX_FFI_ARGS) exit(1);
                strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].name, curr.text);
                match(TK_ID);
                if (curr.type == TK_COLON) {
                    match(TK_COLON);
                    strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                    match(TK_ID);
                } else { strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, "num"); }
                match(TK_EQ_ASSIGN);
                expression();
                ffi_blocks[ffi_idx].arg_count++;
                if (curr.type == TK_COMMA) match(TK_COMMA);
            }
            match(TK_RPAREN);
        }
        if (curr.type == TK_ARROW) {
            match(TK_ARROW);
            strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
            match(TK_ID);
        }
        if (curr.type != TK_LBRACE) exit(1);
        char *start = src + 1;
        int braces = 1;
        char *end = start;
        while (*end && braces > 0) {
            if (*end == '{') braces++;
            if (*end == '}') braces--;
            if (*end == '\n') line++; // Track lines in C blocks
            if (braces > 0) end++;
        }
        int len = (int) (end - start);
        if (len >= MAX_C_BLOCK_SIZE) len = MAX_C_BLOCK_SIZE - 1;
        strncpy(ffi_blocks[ffi_idx].code_body, start, len);
        ffi_blocks[ffi_idx].code_body[len] = '\0';
        src = end + 1;
        next_token();
        emit(OP_NATIVE);
        int std_count = 0;
        while (std_library[std_count].name != NULL) std_count++;
        emit(std_count + ffi_idx);
    } else if (curr.type == TK_FSTR) {
        int empty_id = make_string("");
        emit(OP_PSH_STR);
        emit(empty_id);
        char *raw = curr.text;
        char *ptr = raw;
        char *start = ptr;
        while (*ptr) {
            if (*ptr == '{') {
                if (ptr > start) {
                    char chunk[MAX_STRING_LENGTH];
                    int len = (int) (ptr - start);
                    if (len >= MAX_STRING_LENGTH) len = MAX_STRING_LENGTH - 1;
                    strncpy(chunk, start, len);
                    chunk[len] = '\0';
                    int id = make_string(chunk);
                    emit(OP_PSH_STR);
                    emit(id);
                    emit(OP_CAT);
                }
                ptr++;
                char *expr_start = ptr;
                while (*ptr && *ptr != '}') ptr++;
                if (*ptr == '}') {
                    char expr_code[256];
                    int len = (int) (ptr - expr_start);
                    strncpy(expr_code, expr_start, len);
                    expr_code[len] = '\0';
                    char *old_src = src;
                    Token old_token = curr;
                    int old_line = line;
                    src = expr_code;
                    next_token();
                    expression();
                    src = old_src;
                    curr = old_token;
                    line = old_line;
                    emit(OP_CAT);
                    ptr++;
                    start = ptr;
                }
            } else ptr++;
        }
        if (ptr > start) {
            char chunk[MAX_STRING_LENGTH];
            int len = (int) (ptr - start);
            if (len >= MAX_STRING_LENGTH) len = MAX_STRING_LENGTH - 1;
            strncpy(chunk, start, len);
            chunk[len] = '\0';
            int id = make_string(chunk);
            emit(OP_PSH_STR);
            emit(id);
            emit(OP_CAT);
        }
        match(TK_FSTR);
    } else if (curr.type == TK_ID) {
        char name[MAX_IDENTIFIER];
        parse_namespaced_id(name);
        int enum_val = find_enum_val(name);
        if (enum_val != -1) {
            int idx = make_const((double) enum_val);
            emit(OP_PSH_NUM);
            emit(idx);
            return;
        }
        if (curr.type == TK_LPAREN) {
            match(TK_LPAREN);
            int arg_count = 0;
            if (curr.type != TK_RPAREN) {
                expression();
                arg_count++;
                while (curr.type == TK_COMMA) {
                    match(TK_COMMA);
                    expression();
                    arg_count++;
                }
            }
            match(TK_RPAREN);
            int faddr = find_func(name);
            if (faddr != -1) {
                emit(OP_CALL);
                emit(faddr);
                emit(arg_count);
                return;
            }
            int std_idx = find_stdlib_func(name);
            if (std_idx != -1) {
                if (std_library[std_idx].arg_count != arg_count) error("StdLib function '%s' expects %d args", name,
                                                                       std_library[std_idx].arg_count);
                emit(OP_NATIVE);
                emit(std_idx);
                return;
            }
            char m[MAX_IDENTIFIER * 2];
            get_mangled_name(m, name);
            faddr = find_func(m);
            if (faddr != -1) {
                emit(OP_CALL);
                emit(faddr);
                emit(arg_count);
                return;
            }
            error("Undefined function '%s'", name);
        } else {
            int loc = find_local(name);
            int type_id = -1;
            bool is_array = false;
            if (loc != -1) {
                emit(OP_LVAR);
                emit(locals[loc].offset);
                type_id = locals[loc].type_id;
                is_array = locals[loc].is_array;
            } else {
                int glob = find_global(name);
                if (glob == -1) {
                    char m[MAX_IDENTIFIER * 2];
                    get_mangled_name(m, name);
                    glob = find_global(m);
                }
                if (glob == -1) error("Undefined var '%s'", name);
                emit(OP_GET);
                emit(globals[glob].addr);
                type_id = globals[glob].type_id;
                is_array = globals[glob].is_array;
            }
            while (curr.type == TK_DOT || curr.type == TK_LBRACKET) {
                if (curr.type == TK_DOT) {
                    match(TK_DOT);
                    char f[MAX_IDENTIFIER];
                    strcpy(f, curr.text);
                    match(TK_ID);
                    if (type_id == -1) error("Accessing member '%s' of untyped var.", f);
                    int offset = find_field(type_id, f);
                    if (offset == -1) error("Struct '%s' has no field '%s'", struct_defs[type_id].name, f);
                    emit(OP_HGET);
                    emit(offset);
                    emit(type_id);
                    type_id = -1;
                } else if (curr.type == TK_LBRACKET) {
                    match(TK_LBRACKET);
                    expression();
                    if (curr.type == TK_COLON) {
                        match(TK_COLON);
                        expression();
                        match(TK_RBRACKET);
                        emit(OP_SLICE);
                    } else {
                        match(TK_RBRACKET);
                        emit(OP_AGET);
                        if (is_array) is_array = false;
                        else type_id = -1;
                    }
                }
            }
        }
    } else if (curr.type == TK_LPAREN) {
        match(TK_LPAREN);
        expression();
        match(TK_RPAREN);
    } else {
        error("Unexpected token '%s' in expression", curr.text);
    }
}

// --- Term (Multiplicative) ---
void term() {
    factor();
    while (curr.type == TK_MUL || curr.type == TK_DIV || curr.type == TK_MOD_OP) {
        MyloTokenType op = curr.type;
        next_token();
        factor();
        switch (op) {
            case TK_MUL: emit(OP_MUL);
                break;
            case TK_DIV: emit(OP_DIV);
                break;
            case TK_MOD_OP: emit(OP_MOD);
                break;
            default: break;
        }
    }
}

// --- Additive (Additive) ---
void additive_expr() {
    term();
    while (curr.type == TK_PLUS || curr.type == TK_MINUS) {
        MyloTokenType op = curr.type;
        next_token();
        term();
        switch (op) {
            case TK_PLUS: emit(OP_ADD);
                break;
            case TK_MINUS: emit(OP_SUB);
                break;
            default: break;
        }
    }
}

// --- Relation (Comparisons) ---
void relation_expr() {
    additive_expr();
    while (curr.type >= TK_LT && curr.type <= TK_NEQ) {
        // Relational Ops
        MyloTokenType op = curr.type;
        next_token();
        additive_expr();
        switch (op) {
            case TK_LT: emit(OP_LT);
                break;
            case TK_GT: emit(OP_GT);
                break;
            case TK_LE: emit(OP_LE);
                break;
            case TK_GE: emit(OP_GE);
                break;
            case TK_EQ: emit(OP_EQ);
                break;
            case TK_NEQ: emit(OP_NEQ);
                break;
            default: break;
        }
    }
}

void expression() {
    relation_expr();
    if (curr.type == TK_QUESTION) {
        match(TK_QUESTION);
        emit(OP_JZ);
        int p1 = vm.code_size;
        emit(0);
        expression();
        emit(OP_JMP);
        int p2 = vm.code_size;
        emit(0);
        vm.bytecode[p1] = vm.code_size;
        match(TK_ELSE);
        expression();
        vm.bytecode[p2] = vm.code_size;
    }
}

#define EMIT_SET(is_loc, addr) if(is_loc) { emit(OP_SVAR); emit(addr); } else { emit(OP_SET); emit(addr); }
#define EMIT_GET(is_loc, addr) if(is_loc) { emit(OP_LVAR); emit(addr); } else { emit(OP_GET); emit(addr); }

int alloc_var(bool is_loc, char *name, int type_id, bool is_array) {
    if (is_loc) {
        if (name) strcpy(locals[local_count].name, name);
        locals[local_count].offset = local_count;
        locals[local_count].type_id = type_id;
        locals[local_count].is_array = is_array;
        return local_count++;
    } else {
        char m[MAX_IDENTIFIER * 2];
        get_mangled_name(m, name);
        if (name) strcpy(globals[global_count].name, m);
        globals[global_count].addr = global_count;
        globals[global_count].type_id = type_id;
        globals[global_count].is_array = is_array;
        return global_count++;
    }
}

void for_statement() {
    match(TK_FOR);
    match(TK_LPAREN);
    char name[MAX_IDENTIFIER] = {0};
    bool is_iter = false;
    int explicit_type = -1;

    if (curr.type == TK_VAR) {
        match(TK_VAR);
        strcpy(name, curr.text);
        match(TK_ID);
        if (curr.type == TK_COLON) {
            match(TK_COLON);
            char tn[MAX_IDENTIFIER];
            parse_namespaced_id(tn);
            explicit_type = find_struct(tn);
        }
        is_iter = true;
    } else if (curr.type == TK_ID) {
        char *safe_src = src;
        Token safe_curr = curr;
        int safe_line = line;
        char temp_name[MAX_IDENTIFIER];
        strcpy(temp_name, curr.text);
        match(TK_ID);
        if (curr.type == TK_COLON) {
            match(TK_COLON);
            char tn[MAX_IDENTIFIER];
            parse_namespaced_id(tn);
            explicit_type = find_struct(tn);
        }
        if (curr.type == TK_IN) {
            is_iter = true;
            strcpy(name, temp_name);
        } else {
            src = safe_src;
            curr = safe_curr;
            line = safe_line;
        }
    }

    if (is_iter) {
        push_loop();
        int var_addr = -1;
        bool is_local = inside_function;
        bool created = false;
        if (is_local) {
            int loc = find_local(name);
            if (loc != -1) var_addr = loc;
            else {
                var_addr = alloc_var(true, name, explicit_type, false);
                created = true;
            }
        } else {
            char m[MAX_IDENTIFIER * 2];
            get_mangled_name(m, name);
            int glob = find_global(m);
            if (glob != -1) var_addr = glob;
            else var_addr = alloc_var(false, name, explicit_type, false);
        }
        if (created) {
            emit(OP_PSH_NUM);
            emit(make_const(0.0));
        }

        match(TK_IN);
        expression();

        if (curr.type == TK_RANGE) {
            if (!is_local) {
                emit(OP_SET);
                emit(var_addr);
            } else { EMIT_SET(is_local, var_addr); }
            match(TK_RANGE);
            int t = make_const(curr.val_float);
            match(TK_NUM);
            int s = alloc_var(is_local, "_step", -1, false);
            EMIT_GET(is_local, var_addr);
            emit(OP_PSH_NUM);
            emit(t);
            emit(OP_LT);
            emit(OP_JZ);
            int p1 = vm.code_size;
            emit(0);
            emit(OP_PSH_NUM);
            emit(make_const(1.0));
            emit(OP_JMP);
            int p2 = vm.code_size;
            emit(0);
            vm.bytecode[p1] = vm.code_size;
            EMIT_GET(is_local, var_addr);
            emit(OP_PSH_NUM);
            emit(t);
            emit(OP_GT);
            emit(OP_JZ);
            int p3 = vm.code_size;
            emit(0);
            emit(OP_PSH_NUM);
            emit(make_const(-1.0));
            emit(OP_JMP);
            int p4 = vm.code_size;
            emit(0);
            vm.bytecode[p3] = vm.code_size;
            emit(OP_PSH_NUM);
            emit(make_const(0.0));
            vm.bytecode[p2] = vm.code_size;
            vm.bytecode[p4] = vm.code_size;
            if (!is_local) {
                emit(OP_SET);
                emit(s);
            }
            int loop = vm.code_size;
            match(TK_RPAREN);
            match(TK_LBRACE);
            while (curr.type != TK_RBRACE && curr.type != TK_EOF) statement();
            match(TK_RBRACE);
            int continue_dest = vm.code_size;
            EMIT_GET(is_local, var_addr);
            EMIT_GET(is_local, s);
            emit(OP_ADD);
            EMIT_SET(is_local, var_addr);
            emit(OP_PSH_NUM);
            emit(t);
            EMIT_GET(is_local, var_addr);
            emit(OP_SUB);
            emit(OP_JNZ);
            emit(loop);
            pop_loop(continue_dest, vm.code_size);
        } else {
            int a = alloc_var(is_local, "_arr", -1, true);
            if (!is_local) {
                emit(OP_SET);
                emit(a);
            }
            int i = alloc_var(is_local, "_idx", -1, false);
            emit(OP_PSH_NUM);
            emit(make_const(0.0));
            if (!is_local) {
                emit(OP_SET);
                emit(i);
            }
            int loop = vm.code_size;
            EMIT_GET(is_local, i);
            EMIT_GET(is_local, a);
            emit(OP_ALEN);
            emit(OP_LT);
            emit(OP_JZ);
            int exit = vm.code_size;
            emit(0);
            EMIT_GET(is_local, a);
            EMIT_GET(is_local, i);
            emit(OP_AGET);
            EMIT_SET(is_local, var_addr);
            match(TK_RPAREN);
            match(TK_LBRACE);
            while (curr.type != TK_RBRACE && curr.type != TK_EOF) statement();
            match(TK_RBRACE);
            int continue_dest = vm.code_size;
            EMIT_GET(is_local, i);
            emit(OP_PSH_NUM);
            emit(make_const(1.0));
            emit(OP_ADD);
            EMIT_SET(is_local, i);
            emit(OP_JMP);
            emit(loop);
            vm.bytecode[exit] = vm.code_size;
            pop_loop(continue_dest, vm.code_size);
        }
    } else {
        int loop = vm.code_size;
        expression();
        match(TK_RPAREN);
        emit(OP_JZ);
        int exit = vm.code_size;
        emit(0);
        match(TK_LBRACE);
        while (curr.type != TK_RBRACE && curr.type != TK_EOF) statement();
        match(TK_RBRACE);
        emit(OP_JMP);
        emit(loop);
        vm.bytecode[exit] = vm.code_size;
    }
}

void struct_decl() {
    match(TK_STRUCT);
    char name[MAX_IDENTIFIER];
    parse_namespaced_id(name);
    char m[MAX_IDENTIFIER * 2];
    get_mangled_name(m, name);
    match(TK_LBRACE);
    int idx = struct_count++;
    strcpy(struct_defs[idx].name, m);
    struct_defs[idx].field_count = 0;
    while (curr.type == TK_VAR) {
        match(TK_VAR);
        strcpy(struct_defs[idx].fields[struct_defs[idx].field_count++], curr.text);
        match(TK_ID);
    }
    match(TK_RBRACE);
}

void enum_decl() {
    match(TK_ENUM);
    char enum_name[MAX_IDENTIFIER];
    strcpy(enum_name, curr.text);
    match(TK_ID);
    match(TK_LBRACE);

    int val = 0;
    while (curr.type != TK_RBRACE && curr.type != TK_EOF) {
        char entry_name[MAX_IDENTIFIER * 2];
        sprintf(entry_name, "%s_%s", enum_name, curr.text);

        if (enum_entry_count >= MAX_ENUM_MEMBERS) error("Too many enum members");

        strcpy(enum_entries[enum_entry_count].name, entry_name);
        enum_entries[enum_entry_count].value = val++;
        enum_entry_count++;

        match(TK_ID);
        if (curr.type == TK_COMMA) match(TK_COMMA);
    }
    match(TK_RBRACE);
}

void parse_struct_literal(int struct_idx) {
    match(TK_LBRACE);
    emit(OP_ALLOC);
    emit(struct_defs[struct_idx].field_count);
    emit(struct_idx);
    while (curr.type != TK_RBRACE && curr.type != TK_EOF) {
        char field_name[MAX_IDENTIFIER];
        strcpy(field_name, curr.text);
        match(TK_ID);
        int offset = find_field(struct_idx, field_name);
        if (curr.type == TK_COLON) match(TK_COLON);
        else match(TK_EQ_ASSIGN);
        expression();
        emit(OP_HSET);
        emit(offset);
        emit(struct_idx);
        if (curr.type == TK_COMMA) match(TK_COMMA);
    }
    match(TK_RBRACE);
}

void parse_map_literal() {
    match(TK_LBRACE);
    emit(OP_MAP); // Create map

    while (curr.type != TK_RBRACE && curr.type != TK_EOF) {
        emit(OP_DUP); // Keep Map Ref on stack for next set

        // Key
        if (curr.type != TK_STR) error("Map keys must be strings");
        int id = make_string(curr.text);
        emit(OP_PSH_STR);
        emit(id);
        match(TK_STR);

        match(TK_EQ_ASSIGN);

        // Value
        expression();

        emit(OP_ASET); // Map[Key] = Value
        emit(OP_POP); // ASET returns Value, but we are in literal init, discard it.

        if (curr.type == TK_COMMA) match(TK_COMMA);
    }
    match(TK_RBRACE);
}

void statement() {
    if (curr.type == TK_PRINT) {
        match(TK_PRINT);
        match(TK_LPAREN);
        if (curr.type == TK_STR) {
            int id = make_string(curr.text);
            emit(OP_PSH_STR);
            emit(id);
            match(TK_STR);
        } else expression();
        match(TK_RPAREN);
        emit(OP_PRN);
    } else if (curr.type == TK_IMPORT) {
        match(TK_IMPORT);
        char f[MAX_STRING_LENGTH];
        strcpy(f, curr.text);
        match(TK_STR);

        char *c = read_file(f);
        if (!c) {
            for (int i = 0; i < search_path_count; i++) {
                char tmp[MAX_STRING_LENGTH];
                sprintf(tmp, "%s/%s", search_paths[i], f);
                c = read_file(tmp);
                if (c) break;
            }
        }

        if (c) parse_internal(c, true);
        else error("Cannot find import '%s'", f);
    } else if (curr.type == TK_MOD) {
        match(TK_MOD);
        char m[MAX_IDENTIFIER];
        strcpy(m, curr.text);
        match(TK_ID);
        match(TK_LBRACE);
        char old[MAX_IDENTIFIER];
        strcpy(old, current_namespace);
        if (strlen(current_namespace) > 0) sprintf(current_namespace, "%s_%s", old, m);
        else strcpy(current_namespace, m);
        while (curr.type != TK_RBRACE && curr.type != TK_EOF) {
            if (curr.type == TK_FN) {
                void function();
                function();
            } else statement();
        }
        match(TK_RBRACE);
        strcpy(current_namespace, old);
    } else if (curr.type == TK_BREAK) {
        match(TK_BREAK);
        emit_break();
    } else if (curr.type == TK_CONTINUE) {
        match(TK_CONTINUE);
        emit_continue();
    } else if (curr.type == TK_ENUM) { enum_decl(); } else if (curr.type == TK_MODULE_PATH) {
        match(TK_MODULE_PATH);
        match(TK_LPAREN);
        char path[MAX_STRING_LENGTH];
        strcpy(path, curr.text);
        match(TK_STR);
        match(TK_RPAREN);
        if (search_path_count < MAX_SEARCH_PATHS) {
            strcpy(search_paths[search_path_count++], path);
        } else {
            fprintf(stderr, "Warning: Max search paths reached\n");
        }
    } else if (curr.type == TK_VAR) {
        match(TK_VAR);
        char name[MAX_IDENTIFIER];
        strcpy(name, curr.text);
        match(TK_ID);
        int st = -1;
        bool is_arr = false;
        if (curr.type == TK_COLON) {
            match(TK_COLON);
            char tn[MAX_IDENTIFIER];
            parse_namespaced_id(tn);
            if (curr.type == TK_LBRACKET) {
                match(TK_LBRACKET);
                match(TK_RBRACKET);
                is_arr = true;
            }
            st = find_struct(tn);
            if (st == -1) {
                char m[MAX_IDENTIFIER * 2];
                get_mangled_name(m, tn);
                st = find_struct(m);
            }
        }
        match(TK_EQ_ASSIGN);

        if (st != -1 && curr.type == TK_LBRACKET) {
            match(TK_LBRACKET);
            int count = 0;
            if (curr.type != TK_RBRACKET) {
                do {
                    parse_struct_literal(st);
                    count++;
                    if (curr.type == TK_COMMA) match(TK_COMMA);
                } while (curr.type != TK_RBRACKET && curr.type != TK_EOF);
            }
            match(TK_RBRACKET);
            emit(OP_ARR);
            emit(count);
            is_arr = true;
        } else if (st != -1 && curr.type == TK_LBRACKET) { parse_struct_literal(st); } else if (
            st != -1 && curr.type == TK_LBRACE) { parse_struct_literal(st); } else if (curr.type == TK_LBRACE) {
            char *safe_src = src;
            Token safe_curr = curr;
            int safe_line = line;
            match(TK_LBRACE);
            bool is_map = (curr.type == TK_STR);
            src = safe_src;
            curr = safe_curr;
            line = safe_line;
            if (is_map) parse_map_literal();
            else error("Struct literal without type");
        } else { expression(); }

        alloc_var(inside_function, name, st, is_arr);
        if (!inside_function) {
            emit(OP_SET);
            emit(global_count - 1);
        }
    } else if (curr.type == TK_FOR) { for_statement(); } else if (curr.type == TK_ID) {
        char name[MAX_IDENTIFIER];
        parse_namespaced_id(name);

        if (curr.type == TK_EQ_ASSIGN) {
            match(TK_EQ_ASSIGN);
            expression();
            int loc = find_local(name);
            if (loc != -1) {
                emit(OP_SVAR);
                emit(locals[loc].offset);
            } else {
                int glob = -1;
                char m[MAX_IDENTIFIER * 2];
                get_mangled_name(m, name);
                if ((glob = find_global(m)) != -1) {
                } else if ((glob = find_global(name)) != -1) {
                }
                if (glob == -1) error("Undefined var '%s'", name);
                emit(OP_SET);
                emit(globals[glob].addr);
            }
        } else if (curr.type == TK_LPAREN) {
            match(TK_LPAREN);
            int arg_count = 0;
            if (curr.type != TK_RPAREN) {
                expression();
                arg_count++;
                while (curr.type == TK_COMMA) {
                    match(TK_COMMA);
                    expression();
                    arg_count++;
                }
            }
            match(TK_RPAREN);
            int faddr = find_func(name);
            if (faddr != -1) {
                emit(OP_CALL);
                emit(faddr);
                emit(arg_count);
                emit(OP_POP);
                return;
            }
            int std_idx = find_stdlib_func(name);
            if (std_idx != -1) {
                if (std_library[std_idx].arg_count != arg_count) error("StdLib function '%s' expects %d args", name,
                                                                       std_library[std_idx].arg_count);
                emit(OP_NATIVE);
                emit(std_idx);
                emit(OP_POP);
                return;
            }
            char m[MAX_IDENTIFIER * 2];
            get_mangled_name(m, name);
            faddr = find_func(m);
            if (faddr != -1) {
                emit(OP_CALL);
                emit(faddr);
                emit(arg_count);
                emit(OP_POP);
                return;
            }
            error("Undefined function '%s'", name);
        } else if (curr.type == TK_DOT || curr.type == TK_LBRACKET) {
            int loc = find_local(name);
            int type_id = -1;
            bool is_array = false;
            if (loc != -1) {
                emit(OP_LVAR);
                emit(locals[loc].offset);
                type_id = locals[loc].type_id;
                is_array = locals[loc].is_array;
            } else {
                int glob = find_global(name);
                if (glob == -1) {
                    char m[MAX_IDENTIFIER * 2];
                    get_mangled_name(m, name);
                    glob = find_global(m);
                }
                if (glob == -1) error("Undefined var '%s'", name);
                emit(OP_GET);
                emit(globals[glob].addr);
                type_id = globals[glob].type_id;
                is_array = globals[glob].is_array;
            }

            while (curr.type == TK_DOT || curr.type == TK_LBRACKET) {
                if (curr.type == TK_DOT) {
                    match(TK_DOT);
                    char f[MAX_IDENTIFIER];
                    strcpy(f, curr.text);
                    match(TK_ID);
                    if (type_id == -1) error("Accessing member '%s' of untyped var.", f);
                    if (is_array) error("Accessing member '%s' on array.", f);
                    int offset = find_field(type_id, f);
                    if (offset == -1) error("Struct '%s' has no field '%s'", struct_defs[type_id].name, f);

                    if (curr.type == TK_EQ_ASSIGN) {
                        match(TK_EQ_ASSIGN);
                        expression();
                        emit(OP_HSET);
                        emit(offset);
                        emit(type_id);
                        emit(OP_POP);
                        break;
                    } else {
                        emit(OP_HGET);
                        emit(offset);
                        emit(type_id);
                        type_id = -1;
                    }
                } else if (curr.type == TK_LBRACKET) {
                    match(TK_LBRACKET);
                    expression(); // Key/Index

                    if (curr.type == TK_COLON) {
                        match(TK_COLON);
                        expression();
                        match(TK_RBRACKET);
                        emit(OP_SLICE);
                    } else {
                        match(TK_RBRACKET);
                        if (curr.type == TK_EQ_ASSIGN) {
                            match(TK_EQ_ASSIGN);
                            expression();
                            emit(OP_ASET);
                            emit(OP_POP);
                            break;
                        } else {
                            emit(OP_AGET);
                            if (is_array) is_array = false;
                            else type_id = -1;
                        }
                    }
                }
            }
        }
    } else if (curr.type == TK_STRUCT) struct_decl();
    else if (curr.type == TK_IF) {
        match(TK_IF);
        expression();
        emit(OP_JZ);
        int p1 = vm.code_size;
        emit(0);
        match(TK_LBRACE);
        while (curr.type != TK_RBRACE) statement();
        match(TK_RBRACE);
        if (curr.type == TK_ELSE) {
            emit(OP_JMP);
            int p2 = vm.code_size;
            emit(0);
            vm.bytecode[p1] = vm.code_size;
            match(TK_ELSE);
            match(TK_LBRACE);
            while (curr.type != TK_RBRACE) statement();
            match(TK_RBRACE);
            vm.bytecode[p2] = vm.code_size;
        } else { vm.bytecode[p1] = vm.code_size; }
    }

    // --- UPDATED: Allow empty return (ret) ---
    else if (curr.type == TK_RET) {
        match(TK_RET);
        // If next is '}' or start of new statement/block, push 0 (void)
        // Simple heuristic: If it's a closing brace, it's definitely void return.
        if (curr.type == TK_RBRACE) {
            emit(OP_PSH_NUM);
            emit(make_const(0.0));
        } else {
            expression();
        }
        emit(OP_RET);
    } else if (curr.type != TK_EOF) next_token();
}

void function() {
    match(TK_FN);
    char name[MAX_IDENTIFIER];
    strcpy(name, curr.text);
    match(TK_ID);
    emit(OP_JMP);
    int p = vm.code_size;
    emit(0);
    char m[MAX_IDENTIFIER * 2];
    get_mangled_name(m, name);
    strcpy(funcs[func_count].name, m);
    funcs[func_count++].addr = vm.code_size;
    bool ps = inside_function;
    int pl = local_count;
    inside_function = true;
    local_count = 0;

    match(TK_LPAREN);
    while (curr.type != TK_RPAREN) {
        char arg_name[MAX_IDENTIFIER];
        strcpy(arg_name, curr.text);
        match(TK_ID);
        int arg_type = -1;
        bool arg_arr = false;
        if (curr.type == TK_COLON) {
            match(TK_COLON);
            char tn[MAX_IDENTIFIER];
            parse_namespaced_id(tn);
            if (curr.type == TK_LBRACKET) {
                match(TK_LBRACKET);
                match(TK_RBRACKET);
                arg_arr = true;
            }
            arg_type = find_struct(tn);
        }
        alloc_var(true, arg_name, arg_type, arg_arr);
        if (curr.type == TK_COMMA) match(TK_COMMA);
    }
    match(TK_RPAREN);

    match(TK_LBRACE);
    while (curr.type != TK_RBRACE) statement();
    match(TK_RBRACE);
    emit(OP_PSH_NUM);
    int z = make_const(0.0);
    emit(z);
    emit(OP_RET);
    vm.bytecode[p] = vm.code_size;
    inside_function = ps;
    local_count = pl;
}

void parse_internal(char *source, bool is_import) {
    char *os = src;
    Token oc = curr;
    src = source;
    next_token();
    while (curr.type != TK_EOF) {
        if (curr.type == TK_FN) function();
        else statement();
    }
    if (!is_import) emit(OP_HLT);
    src = os;
    curr = oc;
}

void parse(char *source) { parse_internal(source, false); }

void compile_to_c_source(const char *output_filename) {
    FILE *fp = fopen(output_filename, "w");
    if (!fp) error("Failed to open output file %s", output_filename);

    fprintf(fp, "// Generated by Mylo Compiler\n");
    fprintf(fp, "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n");
    fprintf(fp, "#include \"vm.h\"\n");
    fprintf(fp, "#include \"mylolib.h\"\n\n");

    // GENERATE C STRUCTS FROM MYLO STRUCTS
    fprintf(fp, "// --- GENERATED C STRUCTS ---\n");
    for (int i = 0; i < struct_count; i++) {
        fprintf(fp, "typedef struct { ");
        for (int j = 0; j < struct_defs[i].field_count; j++) {
            fprintf(fp, "double %s; ", struct_defs[i].fields[j]);
        }
        fprintf(fp, "} c_%s;\n", struct_defs[i].name);
    }
    fprintf(fp, "\n");

    // EMIT USER BLOCKS
    fprintf(fp, "// --- USER INLINE C BLOCKS ---\n");
    for (int i = 0; i < ffi_count; i++) {
        char *ret_type = ffi_blocks[i].return_type;
        if (strcmp(ret_type, "num") == 0) fprintf(fp, "double");
        else if (strcmp(ret_type, "string") == 0) fprintf(fp, "char*");
        else if (strlen(ret_type) > 0) fprintf(fp, "c_%s", ret_type);
        else fprintf(fp, "MyloReturn");

        fprintf(fp, " __mylo_user_%d(", i);
        for (int a = 0; a < ffi_blocks[i].arg_count; a++) {
            char *type = ffi_blocks[i].args[a].type;
            if (strcmp(type, "str") == 0 || strcmp(type, "string") == 0) {
                fprintf(fp, "char* %s", ffi_blocks[i].args[a].name);
            } else {
                fprintf(fp, "double %s", ffi_blocks[i].args[a].name);
            }
            if (a < ffi_blocks[i].arg_count - 1) fprintf(fp, ", ");
        }
        fprintf(fp, ") {\n%s\n}\n\n", ffi_blocks[i].code_body);
    }

    // EMIT WRAPPERS
    fprintf(fp, "\n// --- NATIVE WRAPPERS ---\n");
    int std_count = 0;
    while (std_library[std_count].name != NULL) std_count++;

    for (int i = 0; i < ffi_count; i++) {
        fprintf(fp, "void __wrapper_%d(VM* vm) {\n", i);
        for (int a = ffi_blocks[i].arg_count - 1; a >= 0; a--) {
            fprintf(fp, "    double _raw_%s = vm_pop();\n", ffi_blocks[i].args[a].name);
        }

        char *ret_type = ffi_blocks[i].return_type;
        if (strcmp(ret_type, "num") == 0) {
            fprintf(fp, "    double res = __mylo_user_%d(", i);
        } else if (strcmp(ret_type, "string") == 0) {
            fprintf(fp, "    char* s = __mylo_user_%d(", i);
        } else if (strlen(ret_type) > 0) {
            fprintf(fp, "    c_%s val = __mylo_user_%d(", ret_type, i);
        } else {
            fprintf(fp, "    MyloReturn res = __mylo_user_%d(", i);
        }

        for (int a = 0; a < ffi_blocks[i].arg_count; a++) {
            char *type = ffi_blocks[i].args[a].type;
            if (strcmp(type, "str") == 0 || strcmp(type, "string") == 0) {
                fprintf(fp, "vm->string_pool[(int)_raw_%s]", ffi_blocks[i].args[a].name);
            } else {
                fprintf(fp, "_raw_%s", ffi_blocks[i].args[a].name);
            }
            if (a < ffi_blocks[i].arg_count - 1) fprintf(fp, ", ");
        }
        fprintf(fp, ");\n");

        if (strcmp(ret_type, "num") == 0) {
            fprintf(fp, "    vm_push(res, T_NUM);\n");
        } else if (strcmp(ret_type, "string") == 0) {
            fprintf(fp, "    int id = make_string(s);\n    vm_push((double)id, T_STR);\n");
        } else if (strlen(ret_type) > 0) {
            int st_idx = -1;
            for (int s = 0; s < struct_count; s++) if (strcmp(struct_defs[s].name, ret_type) == 0) st_idx = s;
            if (st_idx != -1) {
                fprintf(fp, "    int ptr = heap_alloc(%d + HEAP_HEADER_STRUCT);\n", struct_defs[st_idx].field_count);
                fprintf(fp, "    vm->heap[ptr + HEAP_OFFSET_TYPE] = %d.0;\n", st_idx);
                for (int f = 0; f < struct_defs[st_idx].field_count; f++) {
                    fprintf(fp, "    vm->heap[ptr + HEAP_HEADER_STRUCT + %d] = val.%s;\n", f,
                            struct_defs[st_idx].fields[f]);
                }
                fprintf(fp, "    vm_push((double)ptr, T_OBJ);\n");
            } else {
                fprintf(fp, "    // Error: Unknown struct type for marshalling\n");
            }
        } else {
            fprintf(fp, "    vm_push(res.value, res.type);\n");
        }
        fprintf(fp, "}\n");
    }

    fprintf(fp, "\n// --- STATIC DATA ---\n");
    fprintf(fp, "int static_bytecode[] = {");
    for (int i = 0; i < vm.code_size; i++) fprintf(fp, "%d,", vm.bytecode[i]);
    fprintf(fp, "};\n");
    fprintf(fp, "double static_constants[] = {");
    for (int i = 0; i < vm.const_count; i++) fprintf(fp, "%f,", vm.constants[i]);
    fprintf(fp, "};\n");

    if (vm.str_count > 0) {
        fprintf(fp, "const char* static_strings[] = {");
        for (int i = 0; i < vm.str_count; i++) {
            fprintf(fp, "\"");
            for (int c = 0; vm.string_pool[i][c]; c++) {
                if (vm.string_pool[i][c] == '"') fprintf(fp, "\\\"");
                else if (vm.string_pool[i][c] == '\n') fprintf(fp, "\\n");
                else fputc(vm.string_pool[i][c], fp);
            }
            fprintf(fp, "\",");
        }
        fprintf(fp, "};\n");
    } else {
        fprintf(fp, "const char** static_strings = NULL;\n");
    }

    fprintf(
        fp,
        "\n// --- MAIN ---\nint main() {\n    vm_init();\n    memcpy(vm.bytecode, static_bytecode, sizeof(static_bytecode));\n    vm.code_size = %d;\n    memcpy(vm.constants, static_constants, sizeof(static_constants));\n    vm.const_count = %d;\n",
        vm.code_size, vm.const_count);

    if (vm.str_count > 0) {
        fprintf(
            fp, "    for(int i=0; i<%d; i++) strcpy(vm.string_pool[i], static_strings[i]);\n    vm.str_count = %d;\n",
            vm.str_count, vm.str_count);
    } else {
        fprintf(fp, "    vm.str_count = 0;\n");
    }

    fprintf(fp, "    // Register StdLib\n");
    fprintf(fp, "    int std_idx = 0;\n");
    fprintf(fp, "    while(std_library[std_idx].name != NULL) {\n");
    fprintf(fp, "        natives[std_idx] = std_library[std_idx].func;\n");
    fprintf(fp, "        std_idx++;\n");
    fprintf(fp, "    }\n");

    for (int i = 0; i < ffi_count; i++) fprintf(fp, "    natives[std_idx + %d] = __wrapper_%d;\n", i, i);
    fprintf(fp, "    run_vm(false);\n    return 0;\n}\n");
    fclose(fp);
    printf("Build complete. Source generated at %s\n", output_filename);
    printf("To build the application you need the Mylo VM (.h, .c), Mylo utils and MyloLib\n");
    printf("------------------------------------------------------------------------------\n");
    printf("From the mylo-lang root directory, you can run this command to build a binary.\n");
    printf("Compile with: cc %s src/vm.c src/mylolib.c src/utils.c -Isrc/ -o mylo_app -lm\n", output_filename);
}

void mylo_reset() {
    vm_init();
    global_count = 0;
    local_count = 0;
    func_count = 0;
    struct_count = 0;
    ffi_count = 0;
    inside_function = false;
    current_namespace[0] = '\0';
    enum_entry_count = 0;
    search_path_count = 0;
    line = 1; // Reset line number for new parse

    // FIX: Register Standard Library for the Interpreter
    int i = 0;
    while (std_library[i].name != NULL) {
        natives[i] = std_library[i].func;
        i++;
    }
}
