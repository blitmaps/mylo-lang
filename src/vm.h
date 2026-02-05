#ifndef MYLO_VM_H
#define MYLO_VM_H

#ifndef VM_H
#define VM_H

#include <stdbool.h>
#include "defines.h"
#include <stddef.h>

// --- Types ---
#define T_NUM 0
#define T_STR 1
#define T_OBJ 2

// Return type for C-Blocks
typedef struct {
    double value;
    int type;
} MyloReturn;

// Helper Macros for C-Blocks
#define MYLO_RET_NUM(v) (MyloReturn){ .value = (double)(v), .type = T_NUM }
#define MYLO_RET_OBJ(v) (MyloReturn){ .value = (double)(v), .type = T_OBJ }

typedef struct {
    bool print_to_memory;
    bool debug_mode;
    bool build_mode;
    void (*print_callback)(const char *);
    void (*error_callback)(const char*);
    void* repl_jmp_buf;
} MyloConfigType;

extern MyloConfigType MyloConfig;

typedef enum {
    OP_PSH_NUM, OP_PSH_STR,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_LT, OP_EQ, OP_GT, OP_GE, OP_LE, OP_NEQ,
    OP_SET, OP_GET, OP_LVAR, OP_SVAR,
    OP_JMP, OP_JZ, OP_JNZ,
    OP_CALL, OP_RET,
    OP_PRN, OP_HLT, OP_CAT,
    OP_ALLOC, OP_HSET, OP_HGET, OP_DUP,
    OP_POP,
    OP_NATIVE,
    OP_ARR, OP_AGET, OP_ALEN, OP_SLICE,
    OP_MAP, OP_ASET,
    OP_MK_BYTES,
    OP_SLICE_SET,
    OP_IT_KEY,
    OP_IT_VAL,
    OP_IT_DEF,
    OP_EMBED,
    OP_MAKE_ARR,
    OP_NEW_ARENA,
    OP_DEL_ARENA,
    OP_SET_CTX,
    OP_MONITOR,
    OP_CAST,
    OP_CHECK_TYPE,
    OP_DEBUGGER
} OpCode;

extern const char *OP_NAMES[];

typedef struct {
    char name[64];
    int addr;
} VMFunction;

// --- Arena Struct ---
typedef struct {
    double* memory;
    int* types;
    int head;
    int capacity;
    bool active;
    int generation;
} MemoryArena;

// --- Debug Structures ---
typedef struct {
    char name[MAX_IDENTIFIER];
    int addr;
} VMSymbol;

typedef struct {
    char name[MAX_IDENTIFIER];
    int stack_offset;
    int start_ip;
    int end_ip;
} VMLocalInfo;

typedef struct {
    double* stack;
    double* globals;
    double* constants;
    MemoryArena arenas[MAX_ARENAS];
    int current_arena;
    int* bytecode;
    int* lines;
    int* stack_types;
    int* global_types;
    char (*string_pool)[MAX_STRING_LENGTH];
    int code_size;
    int sp;
    int fp;
    int ip;
    int str_count;
    int const_count;
    char output_char_buffer[OUTPUT_BUFFER_SIZE];
    int output_mem_pos;
    VMFunction functions[MAX_VM_FUNCTIONS];
    int function_count;
    VMSymbol* global_symbols;
    int global_symbol_count;
    VMLocalInfo* local_symbols;
    int local_symbol_count;
    // --- Debugger Fields ---
    char* source_code;
    bool cli_debug_mode;
    int last_debug_line; // <--- ADDED: Persist step state
} VM;

extern VM vm;
typedef void (*NativeFunc)(VM *);
extern NativeFunc natives[MAX_NATIVES];

typedef struct {
    void (*push)(double, int);
    double (*pop)();
    int (*make_string)(const char*);
    double (*heap_alloc)(int);
    double* (*resolve_ptr)(double);
    double (*store_copy)(void*, size_t, const char*);
    double (*store_ptr)(void*, const char*);
    void* (*get_ref)(int, const char*);
    void (*free_ref)(int);
    NativeFunc* natives_array;
    char (*string_pool)[MAX_STRING_LENGTH];
} MyloAPI;

void vm_init();
void vm_cleanup();
void vm_push(double val, int type);
double vm_pop();
int make_string(const char *s);
int make_const(double val);
double heap_alloc(int size);
void run_vm_from(int start_ip, bool debug_trace);
void run_vm(bool debug_trace);
int vm_step(bool debug_trace);
void mylo_reset();
double* vm_resolve_ptr(double ptr_val);
double* vm_resolve_ptr_safe(double ptr_val);
int* vm_resolve_type(double ptr_val);
double vm_store_copy(void* data, size_t size, const char* type_name);
double vm_store_ptr(void* ptr, const char* type_name);
void* vm_get_ref(int id, const char* expected_type_name);
void vm_free_ref(int id);
int vm_find_function(VM* vm, const char* name);
void vm_register_function(VM* vm, const char* name, int addr);
void print_recursive(double val, int type, int depth, int max_elem);
void enter_debugger();

#define MYLO_STORE(val, type_name) vm_store_copy(&(val), sizeof(val), type_name)
#define MYLO_RETRIEVE(id, c_type, type_name) (c_type*)vm_get_ref((int)(id), type_name)
#define MYLO_REGISTER(ptr, type_name) vm_store_ptr((void*)(ptr), type_name)

#endif
#endif