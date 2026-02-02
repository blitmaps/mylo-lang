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
    // Sprintf to internal memory
    bool print_to_memory;
    // DAP mode
    bool debug_mode;
    // Disables DLOpen to allow for c-generation
    bool build_mode;
    // Function pointer for printer overloading
    void (*print_callback)(const char *);
    void (*error_callback)(const char*);
    // Error Recovery Buffer for REPL
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
    OP_NEW_ARENA,  // New: Create a region
    OP_DEL_ARENA,  // New: Clear a region
    OP_SET_CTX     // New: Set current allocation context (Arena ID)
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
} MemoryArena;

typedef struct {
    double* stack;
    double* globals;
    double* constants;

    // Arena System
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
} VM;

extern VM vm;

typedef void (*NativeFunc)(VM *);

extern NativeFunc natives[MAX_NATIVES];

// --- API STRUCT ---
typedef struct {
    void (*push)(double, int);
    double (*pop)();
    int (*make_string)(const char*);
    // Updated: Returns double (Packed Pointer)
    double (*heap_alloc)(int);

    // Reference Management Bridge
    double (*store_copy)(void*, size_t, const char*);
    double (*store_ptr)(void*, const char*);
    void* (*get_ref)(int, const char*);
    void (*free_ref)(int);

    NativeFunc* natives_array;
    char (*string_pool)[MAX_STRING_LENGTH];
    // Removed: double* heap; (Access via API functions only)
} MyloAPI;

// --- EXPORTED FUNCTIONS ---

void vm_init();
void vm_cleanup();
void vm_push(double val, int type);
double vm_pop();
int make_string(const char *s);
int make_const(double val);

// Updated: Returns double (Packed Pointer)
double heap_alloc(int size);

void run_vm_from(int start_ip, bool debug_trace);

// Core Execution
void run_vm(bool debug_trace);
int vm_step(bool debug_trace); // Single step execution
void mylo_reset();

// Pointer Resolution
double* vm_resolve_ptr(double ptr_val); // Get pointer to data
int* vm_resolve_type(double ptr_val);   // Get pointer to type data

// Ref counting Prototypes
double vm_store_copy(void* data, size_t size, const char* type_name);
double vm_store_ptr(void* ptr, const char* type_name);
void* vm_get_ref(int id, const char* expected_type_name);
void vm_free_ref(int id);

// Prototype for function finder helper
int vm_find_function(VM* vm, const char* name);
void vm_register_function(VM* vm, const char* name, int addr);

// Macros
#define MYLO_STORE(val, type_name) vm_store_copy(&(val), sizeof(val), type_name)
#define MYLO_RETRIEVE(id, c_type, type_name) (c_type*)vm_get_ref((int)(id), type_name)
#define MYLO_REGISTER(ptr, type_name) vm_store_ptr((void*)(ptr), type_name)

#endif
#endif