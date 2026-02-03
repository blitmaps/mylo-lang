#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include "mylolib.h"
#include "vm.h"
#include "utils.h"
#include "defines.h"
#include "loader.h"
#include "compiler.h"
#include <setjmp.h>

// --- Tokenizer & Structures ---

typedef enum {
    TK_FN, TK_VAR, TK_IF, TK_FOR, TK_RET, TK_PRINT, TK_IN, TK_STRUCT, TK_ELSE,
    TK_MOD,
    TK_IMPORT,
    TK_FOREVER,
    TK_ID, TK_NUM, TK_STR, TK_RANGE,
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET,
    TK_PLUS, TK_MINUS,
    TK_LT, TK_GT, TK_LE, TK_GE, TK_EQ, TK_NEQ,
    TK_EQ_ASSIGN, TK_QUESTION,
    TK_EOF, TK_FSTR,
    TK_BSTR,
    TK_COLON, TK_COMMA, TK_DOT,
    TK_SCOPE, // ::
    TK_ARROW, // ->
    TK_BREAK, TK_CONTINUE,
    TK_ENUM,
    TK_MODULE_PATH,
    TK_TRUE, TK_FALSE,
    TK_MUL, TK_DIV,
    TK_MOD_OP, // % operator
    TK_EMBED,
    TK_TYPE_DEF,
    TK_REGION,
    TK_CLEAR,
    TK_MONITOR,
} MyloTokenType;

// Token Names for pretty printing
const char *TOKEN_NAMES[] = {
    "fn", "var", "if", "for", "ret", "print", "in", "struct", "else",
    "mod", "import", "forever",
    "Identifier", "Number", "String", "Range (...)",
    "(", ")", "{", "}", "[", "]",
    "+", "-", "<", ">", "<=", ">=", "==", "!=",
    "=", "?",
    "End of File", "Format String", "Byte String",
    ":", ",", ".", "::", "->",
    "break", "continue", "enum", "module_path",
    "true", "false", "*", "/", "%",
    "embed", "Type Definition", "region", "clear", "monitor"
};

const char *get_token_name(MyloTokenType t) {
    if (t < 0 || t > TK_CLEAR) return "Unknown";
    return TOKEN_NAMES[t];
}

typedef struct {
    MyloTokenType type;
    char text[MAX_IDENTIFIER];
    double val_float;
    int line;
    // Context tracking
    char *start;
    int length;
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
int bound_ffi_count = 0;

static char *src;
static char *current_file_start = NULL; // Start of the current file buffer
static Token curr;
static bool inside_function = false;
static char current_namespace[MAX_IDENTIFIER] = "";
static int line = 1;

char search_paths[MAX_SEARCH_PATHS][MAX_STRING_LENGTH];
int search_path_count = 0;

char c_headers[MAX_C_HEADERS][MAX_STRING_LENGTH];
int c_header_count = 0;

typedef struct {
    int break_patches[MAX_JUMPS_PER_LOOP];
    int break_count;
    int continue_patches[MAX_JUMPS_PER_LOOP];
    int continue_count;
} LoopControl;

LoopControl loop_stack[MAX_LOOP_NESTING];
int loop_depth = 0;

Symbol globals[MAX_GLOBALS];
int global_count = 0;

LocalSymbol locals[MAX_GLOBALS];
int local_count = 0;

DebugSym debug_symbols[MAX_DEBUG_SYMBOLS];
int debug_symbol_count = 0;

FuncDebugInfo funcs[MAX_GLOBALS];
int func_count = 0;

struct StructDef {
    char name[MAX_IDENTIFIER];
    char fields[MAX_FIELDS][MAX_IDENTIFIER];
    int field_count;
} struct_defs[MAX_STRUCTS];

int struct_count = 0;

struct {
    char name[MAX_IDENTIFIER];
    int value;
} enum_entries[MAX_ENUM_MEMBERS];

int enum_entry_count = 0;

// Forward Declarations
void parse_internal(char *source, bool is_import);

void parse_struct_literal(int struct_idx);

void parse_map_literal();

// --- Error Handling ---

void print_line_slice(char *start, char *end) {
    while (start < end) {
        fputc(*start, stderr);
        start++;
    }
    fputc('\n', stderr);
}

void print_error_context() {
    if (!current_file_start || !curr.start) return;

    // 1. Find boundaries of the CURRENT line
    char *line_start = curr.start;
    while (line_start > current_file_start && *(line_start - 1) != '\n') {
        line_start--;
    }

    char *line_end = curr.start;
    while (*line_end && *line_end != '\n') {
        line_end++;
    }

    // 2. Print PREVIOUS Line (if it exists) - White
    if (line_start > current_file_start) {
        char *prev_line_end = line_start - 1;
        // Handle Windows \r\n
        if (prev_line_end > current_file_start && *prev_line_end == '\r') prev_line_end--;

        char *prev_line_start = prev_line_end;
        while (prev_line_start > current_file_start && *(prev_line_start - 1) != '\n') {
            prev_line_start--;
        }

        if (prev_line_end >= prev_line_start) {
            fsetTerminalColor(stderr, MyloFgWhite, MyloBgColorDefault); // Use stderr
            fprintf(stderr, "    ");
            print_line_slice(prev_line_start, prev_line_end);
        }
    }

    // 3. Print CURRENT Line - Blue
    fsetTerminalColor(stderr, MyloFgBlue, MyloBgColorDefault); // Use stderr
    fprintf(stderr, "    ");
    print_line_slice(line_start, line_end);

    // 4. Print UNDERLINE - Red Carets (^)
    fprintf(stderr, "    ");
    int offset = (int) (curr.start - line_start);

    // Print spaces up to the error (handling tabs)
    for (int i = 0; i < offset; i++) {
        char c = line_start[i];
        if (c == '\t') fputc('\t', stderr);
        else fputc(' ', stderr);
    }

    fsetTerminalColor(stderr, MyloFgRed, MyloBgColorDefault); // Use stderr
    int len = curr.length;
    if (len <= 0) len = 1;
    for (int i = 0; i < len; i++) fputc('^', stderr);
    fprintf(stderr, "\n");

    // 5. Print NEXT Line (if it exists) - White
    if (*line_end) {
        char *next_line_start = line_end + 1; // Skip the \n
        if (*next_line_start) {
            char *next_line_end = next_line_start;
            while (*next_line_end && *next_line_end != '\n') next_line_end++;

            fsetTerminalColor(stderr, MyloFgWhite, MyloBgColorDefault); // Use stderr
            fprintf(stderr, "    ");
            print_line_slice(next_line_start, next_line_end);
        }
    }

    fresetTerminal(stderr);
}

void error(const char *fmt, ...) {
    fprintf(stderr, "\n"); // Spacing (stderr)
    print_error_context();
    fprintf(stderr, "\n"); // Spacing (stderr)

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int offset = snprintf(buffer, 1024, "[Line %d] Error: ", curr.line > 0 ? curr.line : line);
    vsnprintf(buffer + offset, 1024 - offset, fmt, args);
    va_end(args);

    if (MyloConfig.debug_mode && MyloConfig.error_callback) {
        MyloConfig.error_callback(buffer);
        if (MyloConfig.repl_jmp_buf) longjmp(*(jmp_buf *) MyloConfig.repl_jmp_buf, 1);
        exit(1);
    }

    fsetTerminalColor(stderr, MyloFgRed, MyloBgColorDefault); // Use stderr
    fprintf(stderr, "%s\n", buffer);
    fresetTerminal(stderr);

    if (MyloConfig.repl_jmp_buf) {
        longjmp(*(jmp_buf *) MyloConfig.repl_jmp_buf, 1);
    }
    exit(1);
}

void emit(int op) {
    if (vm.code_size >= MAX_CODE) {
        fprintf(stderr, "Error: Code overflow\n");
        exit(1);
    }
    vm.bytecode[vm.code_size] = op;
    vm.lines[vm.code_size] = curr.line > 0 ? curr.line : line;
    vm.code_size++;
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
    for (int i = 0; i < struct_defs[struct_idx].field_count; i++)
        if (strcmp(struct_defs[struct_idx].fields[i], field) == 0) return i;
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

    curr.line = line;
    curr.start = src;

    if (*src == 'b' && *(src + 1) == '"') {
        src += 2;
        int idx = 0;
        while (*src != '"' && *src != 0) {
            if (*src == '\n') line++;
            if (*src == '\\' && *(src + 1) == 'x' && isxdigit((unsigned char)*(src+2)) && isxdigit(
                    (unsigned char)*(src+3))) {
                char c1 = *(src + 2);
                char c2 = *(src + 3);
                int v1 = (isdigit(c1) ? c1 - '0' : tolower(c1) - 'a' + 10);
                int v2 = (isdigit(c2) ? c2 - '0' : tolower(c2) - 'a' + 10);
                if (idx < MAX_IDENTIFIER - 1) curr.text[idx++] = (char) ((v1 << 4) | v2);
                src += 4;
            } else {
                if (idx < MAX_IDENTIFIER - 1) curr.text[idx++] = *src;
                src++;
            }
        }
        curr.text[idx] = '\0';
        if (*src == '"') src++;
        curr.type = TK_BSTR;
        curr.length = (int) (src - curr.start);
        return;
    }
    if (*src == 'f' && *(src + 1) == '"') {
        src += 2;
        char *start = src;
        int brace_depth = 0;
        while (*src) {
            if (*src == '"' && brace_depth == 0) break;
            if (*src == '\n') line++;
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
        curr.length = (int) (src - curr.start);
        return;
    }

    if (*src == 0) {
        curr.type = TK_EOF;
        curr.length = 0;
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
        curr.length = (int) (src - curr.start);
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
        else if (strcmp(curr.text, "forever") == 0) curr.type = TK_FOREVER;
        else if (strcmp(curr.text, "embed") == 0) curr.type = TK_EMBED;
        else if (strcmp(curr.text, "region") == 0) curr.type = TK_REGION;
        else if (strcmp(curr.text, "clear") == 0) curr.type = TK_CLEAR;
        else if (strcmp(curr.text, "monitor") == 0) curr.type = TK_MONITOR;
        else if (strcmp(curr.text, "f64") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "f32") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "i32") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "i16") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "i64") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "byte") == 0) curr.type = TK_TYPE_DEF;
        else if (strcmp(curr.text, "bool") == 0) curr.type = TK_TYPE_DEF;
        else curr.type = TK_ID;
        curr.length = (int) (src - curr.start);
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
        curr.length = (int) (src - curr.start);
        return;
    }
    if (strncmp(src, "...", 3) == 0) {
        src += 3;
        curr.type = TK_RANGE;
        curr.length = 3;
        return;
    }
    if (strncmp(src, "::", 2) == 0) {
        src += 2;
        curr.type = TK_SCOPE;
        curr.length = 2;
        return;
    }
    if (strncmp(src, "->", 2) == 0) {
        src += 2;
        curr.type = TK_ARROW;
        curr.length = 2;
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
    curr.length = (int) (src - curr.start);
}

int get_type_id_from_token(const char *txt) {
    if (strcmp(txt, "byte") == 0) return TYPE_BYTES;
    if (strcmp(txt, "i32") == 0) return TYPE_I32_ARRAY;
    if (strcmp(txt, "f32") == 0) return TYPE_F32_ARRAY;
    if (strcmp(txt, "i16") == 0) return TYPE_I16_ARRAY;
    if (strcmp(txt, "i64") == 0) return TYPE_I64_ARRAY;
    if (strcmp(txt, "bool") == 0) return TYPE_BOOL_ARRAY;
    return TYPE_ARRAY;
}

void match(MyloTokenType t) {
    if (curr.type == t) next_token();
    else error("Expected '%s', got '%s'", get_token_name(t), get_token_name(curr.type));
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
        bool is_map = (curr.type == TK_STR || curr.type == TK_RBRACE);
        src = safe_src;
        curr = safe_curr;
        line = safe_line;
        if (is_map) parse_map_literal();
        else {
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
                    if (curr.type == TK_TYPE_DEF) {
                        strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                        match(TK_TYPE_DEF);
                    } else {
                        strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                        match(TK_ID);
                    }
                } else strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, "num");
                match(TK_EQ_ASSIGN);
                expression();
                ffi_blocks[ffi_idx].arg_count++;
                if (curr.type == TK_COMMA) match(TK_COMMA);
            }
            match(TK_RPAREN);
        }
        if (curr.type == TK_ARROW) {
            match(TK_ARROW);
            if (curr.type == TK_TYPE_DEF) {
                strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
                match(TK_TYPE_DEF);
            } else {
                strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
                match(TK_ID);
            }
        }
        if (curr.type != TK_LBRACE) exit(1);
        char *start = src + 1;
        int braces = 1;
        char *end = start;
        while (*end && braces > 0) {
            if (*end == '{') braces++;
            if (*end == '}') braces--;
            if (*end == '\n') line++;
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
    } else if (curr.type == TK_BSTR) {
        int id = make_string(curr.text);
        emit(OP_PSH_STR);
        emit(id);
        emit(OP_MK_BYTES);
        match(TK_BSTR);
    } else if (curr.type == TK_ID) {
        Token start_token = curr; // Save token for error reporting
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

            // Error: Restore token to point to function name
            curr = start_token;
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
    } else error("Unexpected token '%s' in expression", curr.text);
}

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

void relation_expr() {
    additive_expr();
    while (curr.type >= TK_LT && curr.type <= TK_NEQ) {
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
        if (debug_symbol_count < MAX_DEBUG_SYMBOLS) {
            if (name) strcpy(debug_symbols[debug_symbol_count].name, name);
            debug_symbols[debug_symbol_count].stack_offset = local_count;
            debug_symbols[debug_symbol_count].start_ip = vm.code_size;
            debug_symbols[debug_symbol_count].end_ip = -1;
            debug_symbol_count++;
        }
        return local_count++;
    }

    char m[MAX_IDENTIFIER * 2];
    get_mangled_name(m, name);

    // --- FIX: Check for existing global to prevent duplicates ---
    for (int i = 0; i < global_count; i++) {
        if (strcmp(globals[i].name, m) == 0) {
            // Update existing variable definition
            globals[i].type_id = type_id;
            globals[i].is_array = is_array;
            return i; // Return existing index
        }
    }
    // ------------------------------------------------------------

    if (name) strcpy(globals[global_count].name, m);
    globals[global_count].addr = global_count;
    globals[global_count].type_id = type_id;
    globals[global_count].is_array = is_array;
    return global_count++;
}

int get_var_addr(char *n, bool is_local, int explicit_type) {
    if (is_local) {
        int loc = find_local(n);
        if (loc != -1) return loc;
        emit(OP_PSH_NUM);
        emit(make_const(0.0));
        return alloc_var(true, n, explicit_type, false);
    } else {
        char m[MAX_IDENTIFIER * 2];
        get_mangled_name(m, n);
        int glob = find_global(m);
        if (glob != -1) return glob;
        emit(OP_PSH_NUM);
        emit(make_const(0.0));
        return alloc_var(false, n, explicit_type, false);
    }
}

void for_statement() {
    match(TK_FOR);
    match(TK_LPAREN);
    char name1[MAX_IDENTIFIER] = {0};
    char name2[MAX_IDENTIFIER] = {0};
    bool is_iter = false;
    bool is_pair = false;
    int explicit_type = -1;

    if (curr.type == TK_VAR) {
        match(TK_VAR);
        strcpy(name1, curr.text);
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
        strcpy(name1, curr.text);
        match(TK_ID);
        if (curr.type == TK_COMMA) {
            match(TK_COMMA);
            strcpy(name2, curr.text);
            match(TK_ID);
            is_pair = true;
        }
        if (!is_pair && curr.type == TK_COLON) {
            match(TK_COLON);
            char tn[MAX_IDENTIFIER];
            parse_namespaced_id(tn);
            explicit_type = find_struct(tn);
        }
        if (curr.type == TK_IN) is_iter = true;
        else {
            src = safe_src;
            curr = safe_curr;
            line = safe_line;
            is_iter = false;
        }
    }

    if (is_iter) {
        push_loop();
        int var1_addr = -1;
        int var2_addr = -1;
        bool is_local = inside_function;
        var1_addr = get_var_addr(name1, is_local, explicit_type);
        if (is_pair) var2_addr = get_var_addr(name2, is_local, explicit_type);
        match(TK_IN);
        expression();

        if (curr.type == TK_RANGE) {
            if (is_pair) error("Cannot unpack key/value from range loop");
            EMIT_SET(is_local, var1_addr);
            match(TK_RANGE);
            int t = make_const(curr.val_float);
            match(TK_NUM);
            int s = alloc_var(is_local, "_step", -1, false);
            EMIT_GET(is_local, var1_addr);
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
            EMIT_GET(is_local, var1_addr);
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
            EMIT_GET(is_local, var1_addr);
            EMIT_GET(is_local, s);
            emit(OP_ADD);
            EMIT_SET(is_local, var1_addr);
            emit(OP_PSH_NUM);
            emit(t);
            EMIT_GET(is_local, var1_addr);
            emit(OP_SUB);
            emit(OP_JNZ);
            emit(loop);
            pop_loop(continue_dest, vm.code_size);
            return;
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
            if (is_pair) {
                EMIT_GET(is_local, a);
                EMIT_GET(is_local, i);
                emit(OP_IT_KEY);
                EMIT_SET(is_local, var1_addr);
                EMIT_GET(is_local, a);
                EMIT_GET(is_local, i);
                emit(OP_IT_VAL);
                EMIT_SET(is_local, var2_addr);
            } else {
                EMIT_GET(is_local, a);
                EMIT_GET(is_local, i);
                emit(OP_IT_DEF);
                EMIT_SET(is_local, var1_addr);
            }
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
        push_loop();
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
        pop_loop(loop, vm.code_size);
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
    emit(OP_MAP);
    while (curr.type != TK_RBRACE && curr.type != TK_EOF) {
        emit(OP_DUP);
        if (curr.type != TK_STR) error("Map keys must be strings");
        int id = make_string(curr.text);
        emit(OP_PSH_STR);
        emit(id);
        match(TK_STR);
        match(TK_EQ_ASSIGN);
        expression();
        emit(OP_ASET);
        emit(OP_POP);
        if (curr.type == TK_COMMA) match(TK_COMMA);
    }
    match(TK_RBRACE);
}

void statement() {
    if (curr.type == TK_REGION) {
        match(TK_REGION);
        char name[MAX_IDENTIFIER];
        strcpy(name, curr.text);
        match(TK_ID);
        emit(OP_NEW_ARENA);
        int var_idx = alloc_var(inside_function, name, -1, false);
        if (inside_function) {
            emit(OP_SVAR);
            emit(locals[var_idx].offset);
        } else {
            emit(OP_SET);
            emit(globals[var_idx].addr);
        }
    } else if (curr.type == TK_CLEAR) {
        match(TK_CLEAR);
        match(TK_LPAREN);
        expression();
        match(TK_RPAREN);
        emit(OP_DEL_ARENA);
    } else if (curr.type == TK_MONITOR) {
        match(TK_MONITOR);
        match(TK_LPAREN);
        match(TK_RPAREN);
        emit(OP_MONITOR);
    } else if (curr.type == TK_PRINT) {
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
        if (curr.type == TK_ID && strcmp(curr.text, "native") == 0) {
            match(TK_ID);
            if (curr.type != TK_STR) error("Expected filename");
            char filename[MAX_STRING_LENGTH];
            strcpy(filename, curr.text);
            match(TK_STR);
            int std_count = 0;
            while (std_library[std_count].name != NULL) std_count++;
            int start_ffi_index = ffi_count;
            char *c = read_file(filename);
            if (!c) {
                for (int i = 0; i < search_path_count; i++) {
                    char tmp[MAX_STRING_LENGTH];
                    sprintf(tmp, "%s/%s", search_paths[i], filename);
                    c = read_file(tmp);
                    if (c) break;
                }
            }
            if (!c) error("Cannot find native import '%s'", filename);
            parse_internal(c, true);
            if (!MyloConfig.build_mode) {
                int added_natives = ffi_count - start_ffi_index;
                char lib_name[MAX_STRING_LENGTH];
                get_lib_name(lib_name, filename);
                if (!MyloConfig.debug_mode) fprintf(stderr, "Mylo: Loading Native Module '%s'...\n", lib_name);
                void *lib = load_library(lib_name);
                if (!lib) error("Could not load native binary '%s'", lib_name);
                typedef void (*BindFunc)(VM *, int, MyloAPI *);
                BindFunc binder = (BindFunc) get_symbol(lib, "mylo_bind_lib");
                if (!binder) error("Native module '%s' invalid", lib_name);
                MyloAPI api;
                api.push = vm_push;
                api.pop = vm_pop;
                api.make_string = make_string;
                api.heap_alloc = heap_alloc;
                api.resolve_ptr = vm_resolve_ptr;
                api.store_copy = vm_store_copy;
                api.store_ptr = vm_store_ptr;
                api.get_ref = vm_get_ref;
                api.free_ref = vm_free_ref;
                api.natives_array = natives;
                api.string_pool = vm.string_pool;
                binder(&vm, std_count + start_ffi_index, &api);
                bound_ffi_count += added_natives;
            }
            return;
        } else if (curr.type == TK_ID && strcmp(curr.text, "C") == 0) {
            match(TK_ID);
            if (curr.type == TK_STR) {
                if (c_header_count < MAX_C_HEADERS) {
                    strcpy(c_headers[c_header_count++], curr.text);
                }
                match(TK_STR);
                return;
            }
            int ffi_idx = ffi_count++;
            ffi_blocks[ffi_idx].id = ffi_idx;
            ffi_blocks[ffi_idx].arg_count = 0;
            strcpy(ffi_blocks[ffi_idx].return_type, "void");
            if (curr.type == TK_LPAREN) {
                match(TK_LPAREN);
                while (curr.type != TK_RPAREN) {
                    if (ffi_blocks[ffi_idx].arg_count >= MAX_FFI_ARGS) exit(1);
                    strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].name, curr.text);
                    match(TK_ID);
                    if (curr.type == TK_COLON) {
                        match(TK_COLON);
                        if (curr.type == TK_TYPE_DEF) {
                            strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                            match(TK_TYPE_DEF);
                        } else {
                            strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                            match(TK_ID);
                        }
                    } else strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, "num");
                    match(TK_EQ_ASSIGN);
                    expression();
                    ffi_blocks[ffi_idx].arg_count++;
                    if (curr.type == TK_COMMA) match(TK_COMMA);
                }
                match(TK_RPAREN);
            }
            if (curr.type == TK_ARROW) {
                match(TK_ARROW);
                if (curr.type == TK_TYPE_DEF) {
                    strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
                    match(TK_TYPE_DEF);
                } else {
                    strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
                    match(TK_ID);
                }
            }
            if (curr.type != TK_LBRACE) error("Expected '{'");
            char *start = src + 1;
            int braces = 1;
            char *end = start;
            while (*end && braces > 0) {
                if (*end == '{') braces++;
                if (*end == '}') braces--;
                if (*end == '\n') line++;
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
            emit(OP_POP);
        } else {
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
        }
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
    } else if (curr.type == TK_ENUM) {
        enum_decl();
    } else if (curr.type == TK_MODULE_PATH) {
        match(TK_MODULE_PATH);
        match(TK_LPAREN);
        char path[MAX_STRING_LENGTH];
        strcpy(path, curr.text);
        match(TK_STR);
        match(TK_RPAREN);
        if (search_path_count < MAX_SEARCH_PATHS) strcpy(search_paths[search_path_count++], path);
    } else if (curr.type == TK_ID && strcmp(curr.text, "C") == 0) {
        match(TK_ID);
        int ffi_idx = ffi_count++;
        ffi_blocks[ffi_idx].id = ffi_idx;
        ffi_blocks[ffi_idx].arg_count = 0;
        strcpy(ffi_blocks[ffi_idx].return_type, "void");
        if (curr.type == TK_LPAREN) {
            match(TK_LPAREN);
            while (curr.type != TK_RPAREN) {
                if (ffi_blocks[ffi_idx].arg_count >= MAX_FFI_ARGS) exit(1);
                strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].name, curr.text);
                match(TK_ID);
                if (curr.type == TK_COLON) {
                    match(TK_COLON);
                    if (curr.type == TK_TYPE_DEF) {
                        strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                        match(TK_TYPE_DEF);
                    } else {
                        strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, curr.text);
                        match(TK_ID);
                    }
                } else strcpy(ffi_blocks[ffi_idx].args[ffi_blocks[ffi_idx].arg_count].type, "num");
                match(TK_EQ_ASSIGN);
                expression();
                ffi_blocks[ffi_idx].arg_count++;
                if (curr.type == TK_COMMA) match(TK_COMMA);
            }
            match(TK_RPAREN);
        }
        if (curr.type == TK_ARROW) {
            match(TK_ARROW);
            if (curr.type == TK_TYPE_DEF) {
                strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
                match(TK_TYPE_DEF);
            } else {
                strcpy(ffi_blocks[ffi_idx].return_type, curr.text);
                match(TK_ID);
            }
        }
        if (curr.type != TK_LBRACE) error("Expected '{'");
        char *start = src + 1;
        int braces = 1;
        char *end = start;
        while (*end && braces > 0) {
            if (*end == '{') braces++;
            if (*end == '}') braces--;
            if (*end == '\n') line++;
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
        emit(OP_POP);
    } else if (curr.type == TK_VAR) {
        match(TK_VAR);
        char name[MAX_IDENTIFIER];
        strcpy(name, curr.text);
        match(TK_ID);
        bool specific_region = false;
        char region_var_name[MAX_IDENTIFIER];
        if (curr.type == TK_SCOPE) {
            match(TK_SCOPE);
            strcpy(region_var_name, name);
            specific_region = true;
            char member_name[MAX_IDENTIFIER];
            strcpy(member_name, curr.text);
            match(TK_ID);
            sprintf(name, "%s_%s", region_var_name, member_name);

            int reg_loc = find_local(region_var_name);
            if (reg_loc != -1) {
                emit(OP_LVAR);
                emit(locals[reg_loc].offset);
            } else {
                int reg_glob = find_global(region_var_name);
                if (reg_glob != -1) {
                    emit(OP_GET);
                    emit(globals[reg_glob].addr);
                } else error("Undefined region");
            }
            emit(OP_SET_CTX);
        }
        int explicit_struct = -1;
        int explicit_primitive = TYPE_ARRAY;
        bool is_arr = false;
        if (curr.type == TK_COLON) {
            match(TK_COLON);
            if (curr.type == TK_TYPE_DEF) {
                explicit_primitive = get_type_id_from_token(curr.text);
                match(TK_TYPE_DEF);
                if (curr.type == TK_LBRACKET) {
                    match(TK_LBRACKET);
                    match(TK_RBRACKET);
                    is_arr = true;
                }
            } else {
                char tn[MAX_IDENTIFIER];
                parse_namespaced_id(tn);
                if (curr.type == TK_LBRACKET) {
                    match(TK_LBRACKET);
                    match(TK_RBRACKET);
                    is_arr = true;
                }
                explicit_struct = find_struct(tn);
                if (explicit_struct == -1) {
                    char m[MAX_IDENTIFIER * 2];
                    get_mangled_name(m, tn);
                    explicit_struct = find_struct(m);
                }
            }
        }
        match(TK_EQ_ASSIGN);

        // --- HANDLER SELECTION ---
        bool handled = false;

        // 1. Typed Primitive Array: var x: i32[] = [...]
        if (is_arr && explicit_primitive != TYPE_ARRAY && curr.type == TK_LBRACKET) {
            match(TK_LBRACKET);
            int count = 0;
            if (curr.type != TK_RBRACKET) {
                do {
                    expression();
                    count++;
                    if (curr.type == TK_COMMA) match(TK_COMMA);
                } while (curr.type != TK_RBRACKET && curr.type != TK_EOF);
            }
            match(TK_RBRACKET);
            emit(OP_MAKE_ARR);
            emit(count);
            emit(explicit_primitive);
            handled = true;
        }
        // 2. Typed Struct Array: var x: Struct[] = [{...}, ...]
        else if (is_arr && explicit_struct != -1 && curr.type == TK_LBRACKET) {
            match(TK_LBRACKET);
            int count = 0;
            if (curr.type != TK_RBRACKET) {
                do {
                    if (curr.type == TK_LBRACE) parse_struct_literal(explicit_struct);
                    else expression();
                    count++;
                    if (curr.type == TK_COMMA) match(TK_COMMA);
                } while (curr.type != TK_RBRACKET && curr.type != TK_EOF);
            }
            match(TK_RBRACKET);
            emit(OP_ARR);
            emit(count);
            handled = true;
        }
        // 3. Typed Struct: var x: Struct = {...}
        else if (explicit_struct != -1 && curr.type == TK_LBRACE) {
            parse_struct_literal(explicit_struct);
            handled = true;
        }
        // 4. Untyped Map/Struct Inference: var x = {...}
        else if (explicit_struct == -1 && curr.type == TK_LBRACE) {
            char *safe_src = src;
            Token safe_curr = curr;
            int safe_line = line;
            match(TK_LBRACE);
            bool is_map = (curr.type == TK_STR || curr.type == TK_RBRACE);
            src = safe_src;
            curr = safe_curr;
            line = safe_line;
            if (is_map) parse_map_literal();
            else error("Struct literal requires type");
            handled = true;
        }

        // 5. Default Expression (Arrays [...], Numbers, Strings, etc)
        if (!handled) {
            expression();
        }

        int var_idx = alloc_var(inside_function, name, explicit_struct, is_arr);
        if (!inside_function) {
            emit(OP_SET);
            emit(globals[var_idx].addr);
        }

        if (specific_region) {
            emit(OP_PSH_NUM);
            emit(make_const(0.0));
            emit(OP_SET_CTX);
        }
    } else if (curr.type == TK_FOR) { for_statement(); } else if (curr.type == TK_ID) {
        Token start_token = curr;
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
            curr = start_token;
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
                    expression();
                    if (curr.type == TK_COLON) {
                        match(TK_COLON);
                        expression();
                        match(TK_RBRACKET);
                        if (curr.type == TK_EQ_ASSIGN) {
                            match(TK_EQ_ASSIGN);
                            expression();
                            emit(OP_SLICE_SET);
                            emit(OP_POP);
                            break;
                        }
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
        } else vm.bytecode[p1] = vm.code_size;
    } else if (curr.type == TK_FOREVER) {
        match(TK_FOREVER);
        match(TK_LBRACE);
        push_loop();
        int loop_start = vm.code_size;
        while (curr.type != TK_RBRACE && curr.type != TK_EOF) statement();
        match(TK_RBRACE);
        emit(OP_JMP);
        emit(loop_start);
        pop_loop(loop_start, vm.code_size);
    } else if (curr.type == TK_EMBED) {
        match(TK_EMBED);
        match(TK_LPAREN);
        char name[MAX_IDENTIFIER];
        strcpy(name, curr.text);
        match(TK_ID);
        match(TK_COMMA);
        if (curr.type != TK_STR) error("embed expects filename");
        char filename[MAX_STRING_LENGTH];
        strcpy(filename, curr.text);
        match(TK_STR);
        match(TK_RPAREN);
        FILE *f = fopen(filename, "rb");
        if (!f) {
            for (int i = 0; i < search_path_count; i++) {
                char tmp[MAX_STRING_LENGTH];
                sprintf(tmp, "%s/%s", search_paths[i], filename);
                f = fopen(tmp, "rb");
                if (f) break;
            }
        }
        if (!f) error("embed: Could not open file '%s'", filename);
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        unsigned char *data = malloc(fsize);
        fread(data, 1, fsize, f);
        fclose(f);
        emit(OP_EMBED);
        emit((int) fsize);
        for (long i = 0; i < fsize; i++) emit((int) data[i]);
        free(data);
        int var_idx = alloc_var(inside_function, name, -1, false);
        if (inside_function) {
            emit(OP_SVAR);
            emit(locals[var_idx].offset);
        } else {
            emit(OP_SET);
            emit(globals[var_idx].addr);
        }
    } else if (curr.type == TK_RET) {
        match(TK_RET);
        if (curr.type == TK_RBRACE) {
            emit(OP_PSH_NUM);
            emit(make_const(0.0));
        } else expression();
        emit(OP_RET);
    } else if (curr.type != TK_EOF) next_token();
}

void function() {
    match(TK_FN);
    if (curr.type == TK_ID && strcmp(curr.text, "C") == 0) error("'C' is reserved");
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
    vm_register_function(&vm, name, vm.code_size);
    int start_debug_idx = debug_symbol_count;
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
    int func_end_ip = vm.code_size;
    for (int i = start_debug_idx; i < debug_symbol_count; i++) {
        if (debug_symbols[i].end_ip == -1) debug_symbols[i].end_ip = func_end_ip;
    }
    inside_function = ps;
    local_count = pl;
}

void parse_internal(char *source, bool is_import) {
    char *os = src;
    Token oc = curr;
    int saved_line = line;
    char *ofs = current_file_start; // Save previous file context

    if (is_import) line = 1;
    int start_debug_idx = debug_symbol_count;

    current_file_start = source; // Set new file context
    src = source;
    next_token();

    while (curr.type != TK_EOF) {
        if (curr.type == TK_FN) function();
        else statement();
    }
    if (!is_import) emit(OP_HLT);

    int end_ip = vm.code_size;
    for (int i = start_debug_idx; i < debug_symbol_count; i++) {
        if (debug_symbols[i].end_ip == -1) debug_symbols[i].end_ip = end_ip;
    }
    // This allows the VM to print names during OP_MONITOR without needing the compiler's arrays
    if (!is_import) {
        // Only do this for the main module or once
        if (vm.global_symbols) free(vm.global_symbols);
        vm.global_symbols = malloc(sizeof(VMSymbol) * global_count);
        vm.global_symbol_count = global_count;
        for (int i = 0; i < global_count; i++) {
            strcpy(vm.global_symbols[i].name, globals[i].name);
            vm.global_symbols[i].addr = globals[i].addr;
        }

        if (vm.local_symbols) free(vm.local_symbols);
        vm.local_symbols = malloc(sizeof(VMLocalInfo) * debug_symbol_count);
        vm.local_symbol_count = debug_symbol_count;
        for (int i = 0; i < debug_symbol_count; i++) {
            strcpy(vm.local_symbols[i].name, debug_symbols[i].name);
            vm.local_symbols[i].stack_offset = debug_symbols[i].stack_offset;
            vm.local_symbols[i].start_ip = debug_symbols[i].start_ip;
            vm.local_symbols[i].end_ip = debug_symbols[i].end_ip;
        }
    }
    src = os;
    curr = oc;
    line = saved_line;
    current_file_start = ofs; // Restore
}

void parse(char *source) { parse_internal(source, false); }

void generate_binding_c_source(const char *output_filename) {
    FILE *fp = fopen(output_filename, "w");
    if (!fp) {
        printf("Failed to open output file\n");
        exit(1);
    }

    fprintf(fp, "// Generated by Mylo Binding Generator\n");
    fprintf(fp, "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n");
    fprintf(fp, "#define MYLO_BINDING_MODE\n");
    fprintf(fp, "\n// --- USER IMPORTS ---\n");
    for (int i = 0; i < c_header_count; i++) {
        if (c_headers[i][0] == '<') fprintf(fp, "#include %s\n", c_headers[i]);
        else fprintf(fp, "#include \"%s\"\n", c_headers[i]);
    }
    fprintf(fp, "#include \"vm.h\"\n#include \"mylolib.h\"\n\n");

    fprintf(fp, "// --- HOST API BRIDGE ---\n");
    fprintf(fp, "void (*host_vm_push)(double, int);\n");
    fprintf(fp, "double (*host_vm_pop)();\n");
    fprintf(fp, "int (*host_make_string)(const char*);\n");
    fprintf(fp, "double (*host_heap_alloc)(int);\n");
    fprintf(fp, "double* (*host_vm_resolve_ptr)(double);\n");
    fprintf(fp, "double (*host_vm_store_copy)(void*, size_t, const char*);\n");
    fprintf(fp, "double (*host_vm_store_ptr)(void*, const char*);\n");
    fprintf(fp, "void* (*host_vm_get_ref)(int, const char*);\n");
    fprintf(fp, "void (*host_vm_free_ref)(int);\n");
    fprintf(fp, "NativeFunc* host_natives_array;\n");

    fprintf(fp, "#define vm_push (*host_vm_push)\n");
    fprintf(fp, "#define vm_pop (*host_vm_pop)\n");
    fprintf(fp, "#define make_string (*host_make_string)\n");
    fprintf(fp, "#define heap_alloc (*host_heap_alloc)\n");
    fprintf(fp, "#define vm_resolve_ptr (*host_vm_resolve_ptr)\n");
    fprintf(fp, "#define vm_store_copy (*host_vm_store_copy)\n");
    fprintf(fp, "#define vm_store_ptr (*host_vm_store_ptr)\n");
    fprintf(fp, "#define vm_get_ref (*host_vm_get_ref)\n");
    fprintf(fp, "#define vm_free_ref (*host_vm_free_ref)\n");
    fprintf(fp, "#define natives host_natives_array\n");

    for (int i = 0; i < struct_count; i++) {
        fprintf(fp, "typedef struct { ");
        for (int j = 0; j < struct_defs[i].field_count; j++) fprintf(fp, "double %s; ", struct_defs[i].fields[j]);
        fprintf(fp, "} c_%s;\n", struct_defs[i].name);
    }
    fprintf(fp, "\n");

    for (int i = 0; i < ffi_count; i++) {
        char *ret_type = ffi_blocks[i].return_type;
        if (strcmp(ret_type, "num") == 0) fprintf(fp, "double");
        else if (strcmp(ret_type, "void") == 0) fprintf(fp, "void");
        else if (strcmp(ret_type, "string") == 0 || strcmp(ret_type, "str") == 0) fprintf(fp, "char*");
        else if (strcmp(ret_type, "i32") == 0) fprintf(fp, "int");
        else if (strcmp(ret_type, "i64") == 0) fprintf(fp, "long long");
        else if (strcmp(ret_type, "f32") == 0) fprintf(fp, "float");
        else if (strcmp(ret_type, "i16") == 0) fprintf(fp, "short");
        else if (strcmp(ret_type, "bool") == 0 || strcmp(ret_type, "byte") == 0) fprintf(fp, "unsigned char");
        else if (strlen(ret_type) > 0) fprintf(fp, "c_%s", ret_type);
        else fprintf(fp, "MyloReturn");

        fprintf(fp, " __mylo_user_%d(", i);
        for (int a = 0; a < ffi_blocks[i].arg_count; a++) {
            char *type = ffi_blocks[i].args[a].type;
            if (strcmp(type, "str") == 0 || strcmp(type, "string") == 0) fprintf(
                fp, "char* %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "num") == 0) fprintf(fp, "double %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i32") == 0) fprintf(fp, "int %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i64") == 0) fprintf(fp, "long long %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "f32") == 0) fprintf(fp, "float %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i16") == 0) fprintf(fp, "short %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "bool") == 0 || strcmp(type, "byte") == 0) fprintf(
                fp, "unsigned char %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "bytes") == 0) fprintf(fp, "unsigned char* %s", ffi_blocks[i].args[a].name);
            else if (strstr(type, "[]")) fprintf(fp, "void* %s", ffi_blocks[i].args[a].name);
            else fprintf(fp, "c_%s* %s", type, ffi_blocks[i].args[a].name);
            if (a < ffi_blocks[i].arg_count - 1) fprintf(fp, ", ");
        }
        fprintf(fp, ") {\n%s\n}\n\n", ffi_blocks[i].code_body);
    }

    fprintf(fp, "\n// --- NATIVE WRAPPERS ---\n");
    int std_count = 0;
    while (std_library[std_count].name != NULL) std_count++;
    for (int i = 0; i < ffi_count; i++) {
        fprintf(fp, "void __wrapper_%d(VM* vm) {\n", i);
        for (int a = ffi_blocks[i].arg_count - 1; a >= 0; a--) fprintf(fp, "    double _raw_%s = vm_pop();\n",
                                                                       ffi_blocks[i].args[a].name);
        char *ret_type = ffi_blocks[i].return_type;
        if (strcmp(ret_type, "num") == 0) fprintf(fp, "    double res = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "void") == 0) fprintf(fp, "    __mylo_user_%d(", i);
        else if (strcmp(ret_type, "string") == 0 || strcmp(ret_type, "str") == 0) fprintf(
            fp, "    char* s = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "i32") == 0) fprintf(fp, "    int res = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "i64") == 0) fprintf(fp, "    long long res = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "f32") == 0) fprintf(fp, "    float res = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "i16") == 0) fprintf(fp, "    short res = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "byte") == 0 || strcmp(ret_type, "bool") == 0) fprintf(
            fp, "    unsigned char res = __mylo_user_%d(", i);
        else if (strlen(ret_type) > 0) fprintf(fp, "    c_%s val = __mylo_user_%d(", ret_type, i);
        else fprintf(fp, "    MyloReturn res = __mylo_user_%d(", i);

        for (int a = 0; a < ffi_blocks[i].arg_count; a++) {
            char *type = ffi_blocks[i].args[a].type;
            if (strcmp(type, "str") == 0 || strcmp(type, "string") == 0) fprintf(
                fp, "vm->string_pool[(int)_raw_%s]", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "num") == 0) fprintf(fp, "_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i32") == 0) fprintf(fp, "(int)_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i64") == 0) fprintf(fp, "(long long)_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "f32") == 0) fprintf(fp, "(float)_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i16") == 0) fprintf(fp, "(short)_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "bool") == 0 || strcmp(type, "byte") == 0) fprintf(
                fp, "(unsigned char)_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "bytes") == 0) fprintf(
                fp, "(unsigned char*)(vm_resolve_ptr(_raw_%s) + HEAP_HEADER_ARRAY)", ffi_blocks[i].args[a].name);
            else if (strstr(type, "[]")) fprintf(fp, "(void*)(vm_resolve_ptr(_raw_%s) + HEAP_HEADER_ARRAY)",
                                                 ffi_blocks[i].args[a].name);
            else fprintf(fp, "(c_%s*)(vm_resolve_ptr(_raw_%s) + HEAP_HEADER_STRUCT)", type, ffi_blocks[i].args[a].name);
            if (a < ffi_blocks[i].arg_count - 1) fprintf(fp, ", ");
        }
        fprintf(fp, ");\n");

        if (strcmp(ret_type, "num") == 0) fprintf(fp, "    vm_push(res, T_NUM);\n");
        else if (strcmp(ret_type, "void") == 0) fprintf(fp, "    vm_push(0.0, T_NUM);\n");
        else if (strcmp(ret_type, "string") == 0 || strcmp(ret_type, "str") == 0) fprintf(
            fp, "    int id = make_string(s);\n    vm_push((double)id, T_STR);\n");
        else if (strcmp(ret_type, "i32") == 0 || strcmp(ret_type, "i64") == 0 || strcmp(ret_type, "f32") == 0 ||
                 strcmp(ret_type, "i16") == 0 || strcmp(ret_type, "byte") == 0 || strcmp(ret_type, "bool") == 0)
            fprintf(fp, "    vm_push((double)res, T_NUM);\n");
        else if (strlen(ret_type) > 0) {
            int st_idx = -1;
            for (int s = 0; s < struct_count; s++) if (strcmp(struct_defs[s].name, ret_type) == 0) st_idx = s;
            if (st_idx != -1) {
                fprintf(fp, "    double ptr = heap_alloc(%d + HEAP_HEADER_STRUCT);\n", struct_defs[st_idx].field_count);
                fprintf(fp, "    double* base = vm_resolve_ptr(ptr);\n");
                fprintf(fp, "    base[HEAP_OFFSET_TYPE] = %d.0;\n", st_idx);
                for (int f = 0; f < struct_defs[st_idx].field_count; f++) fprintf(
                    fp, "    base[HEAP_HEADER_STRUCT + %d] = val.%s;\n", f, struct_defs[st_idx].fields[f]);
                fprintf(fp, "    vm_push(ptr, T_OBJ);\n");
            } else {
                fprintf(fp, "    // Warning: Struct '%s' not found during binding gen. Pushing 0.\n", ret_type);
                fprintf(fp, "    vm_push(0.0, T_OBJ);\n");
            }
        } else fprintf(fp, "    vm_push(res.value, res.type);\n");
        fprintf(fp, "}\n");
    }

    // --- FIX: UNDEFINE MACROS TO PREVENT STRUCT MEMBER COLLISIONS ---
    fprintf(fp, "\n#undef make_string\n");
    fprintf(fp, "#undef heap_alloc\n");
    // ----------------------------------------------------------------

    fprintf(
        fp,
        "\n#ifdef _WIN32\n__declspec(dllexport) void mylo_bind_lib(VM* vm, int start_index, MyloAPI* api) {\n#else\nvoid mylo_bind_lib(VM* vm, int start_index, MyloAPI* api) {\n#endif\n");
    fprintf(
        fp,
        "    host_vm_push = api->push;\n    host_vm_pop = api->pop;\n    host_make_string = api->make_string;\n    host_heap_alloc = api->heap_alloc;\n    host_vm_resolve_ptr = api->resolve_ptr;\n");
    fprintf(
        fp,
        "    host_vm_store_copy = api->store_copy;\n    host_vm_store_ptr = api->store_ptr;\n    host_vm_get_ref = api->get_ref;\n    host_vm_free_ref = api->free_ref;\n");
    fprintf(fp, "    host_natives_array = api->natives_array;\n");
    for (int i = 0; i < ffi_count; i++) fprintf(fp, "    host_natives_array[start_index + %d] = __wrapper_%d;\n", i, i);

    // --- FIX: ADD MISSING CLOSING BRACE ---
    fprintf(fp, "}\n");
    // --------------------------------------

    fclose(fp);

    // Binding message
    printf("\n");
    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n");
    setTerminalColor(MyloFgCyan, MyloBgColorDefault);
    printf("Binding generation complete. Wrapper C source code generated to: ");
    setTerminalColor(MyloFgMagenta, MyloBgColorDefault);
    printf(" %s\n", output_filename);
    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n\n");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    setTerminalColor(MyloFgWhite, MyloBgColorDefault);
    printf("  To build %s into an shared object you need:\n\n", output_filename);
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("The Mylo VM header (vm.h), located in 'mylo-lang/src'\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("The Mylo Standard Library header (mylolib.h), located in 'mylo-lang/src'\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("A 'C' Compiler (GCC, MSVC, Clang, Zig etc.), referred to as ('cc' below.)\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("The Shared Object (.so) file to have the same name as your .mylo wrapper `libname.so`\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("Position independent code, so the .so can be loaded at runtime by the interpreter \n\n");
    setTerminalColor(MyloFgCyan, MyloBgColorDefault);
    printf("  ! ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("To build into a single executable instead, use --build on your main mylo file \n");
    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("  Example Command:\n\n");
    setTerminalColor(MyloFgWhite, MyloBgColorDefault);
    printf("     cc -shared -fPIC %s -Isrc/ -o libname.so\n", output_filename);
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
}

void compile_to_c_source(const char *output_filename) {
    FILE *fp = fopen(output_filename, "w");
    if (!fp) {
        printf("Failed to open output file\n");
        exit(1);
    }

    fprintf(fp, "// Generated by Mylo Compiler\n");
    fprintf(fp, "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n");
    fprintf(fp, "\n// --- USER C IMPORTS ---\n");
    for (int i = 0; i < c_header_count; i++) {
        if (c_headers[i][0] == '<') fprintf(fp, "#include %s\n", c_headers[i]);
        else fprintf(fp, "#include \"%s\"\n", c_headers[i]);
    }
    fprintf(fp, "// ----------------------\n\n");
    fprintf(fp, "#include \"vm.h\"\n#include \"mylolib.h\"\n\n");

    fprintf(fp, "// --- GENERATED C STRUCTS ---\n");
    for (int i = 0; i < struct_count; i++) {
        fprintf(fp, "typedef struct { ");
        for (int j = 0; j < struct_defs[i].field_count; j++) fprintf(fp, "double %s; ", struct_defs[i].fields[j]);
        fprintf(fp, "} c_%s;\n", struct_defs[i].name);
    }
    fprintf(fp, "\n");

    for (int i = 0; i < ffi_count; i++) {
        char *ret_type = ffi_blocks[i].return_type;
        if (strcmp(ret_type, "num") == 0) fprintf(fp, "double");
        else if (strcmp(ret_type, "void") == 0) fprintf(fp, "void");
        else if (strcmp(ret_type, "string") == 0 || strcmp(ret_type, "str") == 0) fprintf(fp, "char*");
        else if (strcmp(ret_type, "i32") == 0) fprintf(fp, "int");
        else if (strcmp(ret_type, "i64") == 0) fprintf(fp, "long long");
        else if (strcmp(ret_type, "f32") == 0) fprintf(fp, "float");
        else if (strcmp(ret_type, "i16") == 0) fprintf(fp, "short");
        else if (strcmp(ret_type, "bool") == 0 || strcmp(ret_type, "byte") == 0) fprintf(fp, "unsigned char");
        else if (strlen(ret_type) > 0) fprintf(fp, "c_%s", ret_type);
        else fprintf(fp, "MyloReturn");

        fprintf(fp, " __mylo_user_%d(", i);
        for (int a = 0; a < ffi_blocks[i].arg_count; a++) {
            char *type = ffi_blocks[i].args[a].type;
            if (strcmp(type, "str") == 0 || strcmp(type, "string") == 0) fprintf(
                fp, "char* %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "num") == 0) fprintf(fp, "double %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i32") == 0) fprintf(fp, "int %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i64") == 0) fprintf(fp, "long long %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "f32") == 0) fprintf(fp, "float %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i16") == 0) fprintf(fp, "short %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "byte") == 0 || strcmp(type, "bool") == 0) fprintf(
                fp, "unsigned char %s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "bytes") == 0) fprintf(fp, "unsigned char* %s", ffi_blocks[i].args[a].name);
            else if (strstr(type, "[]")) fprintf(fp, "void* %s", ffi_blocks[i].args[a].name);
            else fprintf(fp, "c_%s* %s", type, ffi_blocks[i].args[a].name);
            if (a < ffi_blocks[i].arg_count - 1) fprintf(fp, ", ");
        }
        fprintf(fp, ") {\n%s\n}\n\n", ffi_blocks[i].code_body);
    }

    fprintf(fp, "\n// --- NATIVE WRAPPERS ---\n");
    int std_count = 0;
    while (std_library[std_count].name != NULL) std_count++;
    for (int i = 0; i < ffi_count; i++) {
        fprintf(fp, "void __wrapper_%d(VM* vm) {\n", i);
        for (int a = ffi_blocks[i].arg_count - 1; a >= 0; a--) fprintf(fp, "    double _raw_%s = vm_pop();\n",
                                                                       ffi_blocks[i].args[a].name);
        char *ret_type = ffi_blocks[i].return_type;
        if (strcmp(ret_type, "num") == 0) fprintf(fp, "    double res = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "void") == 0) fprintf(fp, "    __mylo_user_%d(", i);
        else if (strcmp(ret_type, "string") == 0 || strcmp(ret_type, "str") == 0) fprintf(
            fp, "    char* s = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "i32") == 0) fprintf(fp, "    int res = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "i64") == 0) fprintf(fp, "    long long res = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "f32") == 0) fprintf(fp, "    float res = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "i16") == 0) fprintf(fp, "    short res = __mylo_user_%d(", i);
        else if (strcmp(ret_type, "byte") == 0 || strcmp(ret_type, "bool") == 0) fprintf(
            fp, "    unsigned char res = __mylo_user_%d(", i);
        else if (strlen(ret_type) > 0) fprintf(fp, "    c_%s val = __mylo_user_%d(", ret_type, i);
        else fprintf(fp, "    MyloReturn res = __mylo_user_%d(", i);

        for (int a = 0; a < ffi_blocks[i].arg_count; a++) {
            char *type = ffi_blocks[i].args[a].type;
            if (strcmp(type, "str") == 0 || strcmp(type, "string") == 0) fprintf(
                fp, "vm->string_pool[(int)_raw_%s]", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "num") == 0) fprintf(fp, "_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i32") == 0) fprintf(fp, "(int)_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i64") == 0) fprintf(fp, "(long long)_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "f32") == 0) fprintf(fp, "(float)_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "i16") == 0) fprintf(fp, "(short)_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "bool") == 0 || strcmp(type, "byte") == 0) fprintf(
                fp, "(unsigned char)_raw_%s", ffi_blocks[i].args[a].name);
            else if (strcmp(type, "bytes") == 0) fprintf(
                fp, "(unsigned char*)(vm_resolve_ptr(_raw_%s) + HEAP_HEADER_ARRAY)", ffi_blocks[i].args[a].name);
            else if (strstr(type, "[]")) fprintf(fp, "(void*)(vm_resolve_ptr(_raw_%s) + HEAP_HEADER_ARRAY)",
                                                 ffi_blocks[i].args[a].name);
            else fprintf(fp, "(c_%s*)(vm_resolve_ptr(_raw_%s) + HEAP_HEADER_STRUCT)", type, ffi_blocks[i].args[a].name);
            if (a < ffi_blocks[i].arg_count - 1) fprintf(fp, ", ");
        }
        fprintf(fp, ");\n");

        if (strcmp(ret_type, "num") == 0) fprintf(fp, "    vm_push(res, T_NUM);\n");
        else if (strcmp(ret_type, "void") == 0) fprintf(fp, "    vm_push(0.0, T_NUM);\n");
        else if (strcmp(ret_type, "string") == 0 || strcmp(ret_type, "str") == 0) fprintf(
            fp, "    int id = make_string(s);\n    vm_push((double)id, T_STR);\n");
        else if (strcmp(ret_type, "i32") == 0 || strcmp(ret_type, "i64") == 0 ||
                 strcmp(ret_type, "f32") == 0 || strcmp(ret_type, "i16") == 0 ||
                 strcmp(ret_type, "byte") == 0 || strcmp(ret_type, "bool") == 0) {
            fprintf(fp, "    vm_push((double)res, T_NUM);\n");
        } else if (strlen(ret_type) > 0) {
            int st_idx = -1;
            for (int s = 0; s < struct_count; s++) if (strcmp(struct_defs[s].name, ret_type) == 0) st_idx = s;
            if (st_idx != -1) {
                fprintf(fp, "    double ptr = heap_alloc(%d + HEAP_HEADER_STRUCT);\n", struct_defs[st_idx].field_count);
                fprintf(fp, "    double* base = vm_resolve_ptr(ptr);\n");
                fprintf(fp, "    base[HEAP_OFFSET_TYPE] = %d.0;\n", st_idx);
                for (int f = 0; f < struct_defs[st_idx].field_count; f++) fprintf(
                    fp, "    base[HEAP_HEADER_STRUCT + %d] = val.%s;\n", f, struct_defs[st_idx].fields[f]);
                fprintf(fp, "    vm_push(ptr, T_OBJ);\n");
            } else {
                fprintf(fp, "    // Warning: Struct '%s' not found during C-gen. Pushing 0.\n", ret_type);
                fprintf(fp, "    vm_push(0.0, T_OBJ);\n");
            }
        } else fprintf(fp, "    vm_push(res.value, res.type);\n");
        fprintf(fp, "}\n");
    }

    fprintf(fp, "\n// --- STATIC DATA ---\n");
    fprintf(fp, "int static_bytecode[] = {");
    for (int i = 0; i < vm.code_size; i++) fprintf(fp, "%d,", vm.bytecode[i]);
    fprintf(fp, "};\n");
    fprintf(fp, "int static_lines[] = {");
    for (int i = 0; i < vm.code_size; i++) fprintf(fp, "%d,", vm.lines[i]);
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
    } else fprintf(fp, "const char** static_strings = NULL;\n");

    fprintf(fp, "\n// --- MAIN ---\nint main() {\n    vm_init();\n");
    fprintf(fp, "    memcpy(vm.bytecode, static_bytecode, sizeof(static_bytecode));\n");
    fprintf(fp, "    memcpy(vm.lines, static_lines, sizeof(static_lines));\n");
    fprintf(fp, "    vm.code_size = %d;\n", vm.code_size);
    fprintf(fp, "    memcpy(vm.constants, static_constants, sizeof(static_constants));\n");
    fprintf(fp, "    vm.const_count = %d;\n", vm.const_count);

    if (vm.str_count > 0) fprintf(
        fp, "    for(int i=0; i<%d; i++) strcpy(vm.string_pool[i], static_strings[i]);\n    vm.str_count = %d;\n",
        vm.str_count, vm.str_count);
    else fprintf(fp, "    vm.str_count = 0;\n");

    fprintf(
        fp,
        "    int std_idx = 0;\n    while(std_library[std_idx].name != NULL) { natives[std_idx] = std_library[std_idx].func; std_idx++; }\n");
    for (int i = 0; i < ffi_count; i++) fprintf(fp, "    natives[std_idx + %d] = __wrapper_%d;\n", i, i);
    fprintf(fp, "    run_vm(false);\n    vm_cleanup();\n    return 0;\n}\n");
    fclose(fp);

    printf("\n");
    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n");
    setTerminalColor(MyloFgCyan, MyloBgColorDefault);
    printf("Bytecode generation complete. Standalone C source code generated to: ");
    setTerminalColor(MyloFgMagenta, MyloBgColorDefault);
    printf(" %s\n", output_filename);
    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n\n");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    setTerminalColor(MyloFgWhite, MyloBgColorDefault);
    printf("  To build %s into an executable you need:\n\n", output_filename);
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("The Mylo VM implementation code (vm.c and vm.h), located in 'mylo-lang/src'\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("The Mylo Standard Library (mylolib.c and mylolib.h), located in 'mylo-lang/src'\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("A 'C' Compiler (GCC, MSVC, Clang, Zig etc.), referred to as ('cc' below.)\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("    + ");
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
    printf("Link the C Standard Math Library and any other libraries you are using (-lm below)\n\n");


    setTerminalColor(MyloFgBlue, MyloBgColorDefault);
    printf("----------------------------------------------------------------------------------------\n");
    setTerminalColor(MyloFgYellow, MyloBgColorDefault);
    printf("  Example Command:\n\n");
    setTerminalColor(MyloFgWhite, MyloBgColorDefault);
    printf("     cc %s src/vm.c src/mylolib.c -Isrc/ -o mylo_executable -lm\n\n", output_filename);
    setTerminalColor(MyloFgDefault, MyloBgColorDefault);
}

void compile_repl(char *source, int *out_start_ip) {
    current_file_start = source;
    line = 1;

    src = source;
    next_token();
    *out_start_ip = vm.code_size;

    if (curr.type == TK_EOF) return;

    if (curr.type == TK_FN) function();
    else if (curr.type == TK_STRUCT) struct_decl();
    else if (curr.type == TK_VAR || curr.type == TK_IF || curr.type == TK_FOR ||
             curr.type == TK_FOREVER || curr.type == TK_PRINT || curr.type == TK_IMPORT ||
             curr.type == TK_RET || curr.type == TK_BREAK || curr.type == TK_CONTINUE ||
             curr.type == TK_ENUM || curr.type == TK_REGION || curr.type == TK_CLEAR ||
             curr.type == TK_MONITOR) {
        statement();
    } else if (curr.type == TK_ID) {
        int saved_code_size = vm.code_size;
        int saved_line = line;

        statement();

        if (curr.type != TK_EOF) {
            vm.code_size = saved_code_size;
            src = source;
            line = saved_line;
            next_token();
            expression();
        }
    } else {
        expression();
    }
    emit(OP_HLT);

    // --- FIX: Sync Debug Symbols for REPL ---
    // This ensures monitor() knows about variables defined in previous REPL lines
    if (vm.global_symbols) free(vm.global_symbols);
    vm.global_symbols = malloc(sizeof(VMSymbol) * global_count);
    vm.global_symbol_count = global_count;
    for (int i = 0; i < global_count; i++) {
        strcpy(vm.global_symbols[i].name, globals[i].name);
        vm.global_symbols[i].addr = globals[i].addr;
    }
}

void mylo_reset() {
    vm_init();
    global_count = 0;
    local_count = 0;
    func_count = 0;
    struct_count = 0;
    ffi_count = 0;
    bound_ffi_count = 0;
    inside_function = false;
    current_namespace[0] = '\0';
    enum_entry_count = 0;
    search_path_count = 0;
    c_header_count = 0;
    // Fix: Reset Line Count
    line = 1;
    int i = 0;
    while (std_library[i].name != NULL) {
        natives[i] = std_library[i].func;
        i++;
    }
}
