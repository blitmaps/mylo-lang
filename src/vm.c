#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "vm.h"
#include <setjmp.h>
#include <stdarg.h> 

// --- Global Instance ---
VM vm;
NativeFunc natives[MAX_NATIVES];
MyloConfigType MyloConfig = {false, false, NULL};

// --- Opcodes String Representation ---
const char *OP_NAMES[] = {
    "PSH_NUM", "PSH_STR",
    "ADD", "SUB", "MUL", "DIV", "MOD",
    "LT", "EQ", "GT", "GE", "LE", "NEQ",
    "SET", "GET", "LVAR", "SVAR",
    "JMP", "JZ", "JNZ",
    "CALL", "RET",
    "PRN", "HLT", "CAT",
    "ALLOC", "HSET", "HGET", "DUP",
    "POP",
    "NATIVE",
    "ARR", "AGET", "ALEN", "SLICE",
    "MAP", "ASET",
    "MK_BYTES",
    "SLICE_SET",
    "IT_KEY", "IT_VAL", "IT_DEF",
    "EMBED",
    "MAKE_ARR",
    "NEW_REG", "DEL_REG", "SET_CTX",
    "MONITOR",
    "CAST",
    "CHECK_TYPE",
    "DEBUGGER" // <--- Added
};

// --- Error Handling ---

void mylo_runtime_error(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);

    printf("[Line %d] Runtime Error: ", vm.lines[vm.ip - 1]);
    vsnprintf(buffer, 1024, fmt, args);
    printf("%s\n", buffer); // Print to console

    va_end(args);
    if (MyloConfig.debug_mode && MyloConfig.error_callback) {
        MyloConfig.error_callback(buffer);
        // We still need to stop execution, but let the callback handle the exit/event
        exit(1);
    }

    if (MyloConfig.repl_jmp_buf) {
        longjmp(*(jmp_buf*)MyloConfig.repl_jmp_buf, 1);
    } else {
        exit(1);
    }
}

#define RUNTIME_ERROR(fmt, ...) mylo_runtime_error(fmt, ##__VA_ARGS__)
#define CHECK_STACK(count) if (vm.sp < (count) - 1) RUNTIME_ERROR("Stack Underflow")
#define CHECK_OBJ(depth) if (vm.stack_types[vm.sp - (depth)] != T_OBJ) RUNTIME_ERROR("Expected Object/Array")

// --- Helpers ---

int get_type_size(int heap_type) {
    switch(heap_type) {
        case TYPE_BYTES:
        case TYPE_BOOL_ARRAY: return 1;
        case TYPE_I16_ARRAY:
        case TYPE_F16_ARRAY:  return 2;
        case TYPE_I32_ARRAY:
        case TYPE_F32_ARRAY:  return 4;
        case TYPE_I64_ARRAY:
        default: return 8;
    }
}

// Simple Hash (djb2)
unsigned long vm_hash_str(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

// --- Reference Management ---

#define MAX_REFS 1024
typedef struct {
    void* ptr;
    unsigned long type_hash;
    bool is_copy;
} RefEntry;

static RefEntry ref_store[MAX_REFS];
static int ref_next_id = 0;

double vm_store_copy(void* data, size_t size, const char* type_name) {
    if (ref_next_id >= MAX_REFS) {
        printf("Runtime Error: Reference limit reached\n");
        return -1.0;
    }
    void* copy = malloc(size);
    if (!copy) return -1.0;
    memcpy(copy, data, size);

    int id = ref_next_id++;
    ref_store[id].ptr = copy;
    ref_store[id].type_hash = vm_hash_str(type_name);
    ref_store[id].is_copy = true;
    return (double)id;
}

double vm_store_ptr(void* ptr, const char* type_name) {
    if (ref_next_id >= MAX_REFS) return -1.0;
    int id = ref_next_id++;
    ref_store[id].ptr = ptr;
    ref_store[id].type_hash = vm_hash_str(type_name);
    ref_store[id].is_copy = false;
    return (double)id;
}

void* vm_get_ref(int id, const char* expected_type_name) {
    if (id < 0 || id >= ref_next_id) return NULL;
    unsigned long expected_hash = vm_hash_str(expected_type_name);
    if (ref_store[id].type_hash != expected_hash) return NULL;
    return ref_store[id].ptr;
}

void vm_free_ref(int id) {
    if (id >= 0 && id < ref_next_id) {
        if (ref_store[id].is_copy && ref_store[id].ptr) free(ref_store[id].ptr);
        ref_store[id].ptr = NULL;
        ref_store[id].type_hash = 0;
    }
}

double* vm_resolve_ptr_safe(double ptr_val) {
    int id = UNPACK_ARENA(ptr_val);
    int offset = UNPACK_OFFSET(ptr_val);
    int gen = UNPACK_GEN(ptr_val);

    if (id < 0 || id >= MAX_ARENAS) return NULL;
    if (!vm.arenas[id].active) return NULL;
    if (vm.arenas[id].generation != gen) return NULL;
    if (offset < 0 || offset >= vm.arenas[id].capacity) return NULL;

    return &vm.arenas[id].memory[offset];
}

// --- Arena & Memory Management ---

void init_arena(int id) {
    if (id < 0 || id >= MAX_ARENAS) return;

    // Handle wrap-around for 14-bit generation (0x3FFF)
    vm.arenas[id].generation = (vm.arenas[id].generation + 1) & 0x3FFF;
    if (vm.arenas[id].generation == 0) vm.arenas[id].generation = 1;

    if (vm.arenas[id].memory == NULL) {
        vm.arenas[id].capacity = MAX_HEAP;
        // CHANGE: Use calloc to ensure heap is zeroed
        vm.arenas[id].memory = (double*)calloc(MAX_HEAP, sizeof(double));
        vm.arenas[id].types = (int*)calloc(MAX_HEAP, sizeof(int));
    }

    vm.arenas[id].head = 0;
    vm.arenas[id].active = true;

    if (!vm.arenas[id].memory || !vm.arenas[id].types) {
        fprintf(stderr, "Critical: Failed to allocate Arena %d\n", id);
        exit(1);
    }
}

void free_arena(int id) {
    if (id < 0 || id >= MAX_ARENAS) return;
    if (vm.arenas[id].memory) {
        free(vm.arenas[id].memory);
        vm.arenas[id].memory = NULL;
    }
    if (vm.arenas[id].types) {
        free(vm.arenas[id].types);
        vm.arenas[id].types = NULL;
    }
    vm.arenas[id].active = false;
    vm.arenas[id].head = 0;
}

void vm_cleanup() {
    if (vm.bytecode) { free(vm.bytecode); vm.bytecode = NULL; }
    if (vm.lines) { free(vm.lines); vm.lines = NULL; }
    if (vm.stack) { free(vm.stack); vm.stack = NULL; }
    if (vm.stack_types) { free(vm.stack_types); vm.stack_types = NULL; }
    if (vm.globals) { free(vm.globals); vm.globals = NULL; }
    if (vm.global_types) { free(vm.global_types); vm.global_types = NULL; }
    if (vm.constants) { free(vm.constants); vm.constants = NULL; }

    for (int i = 0; i < MAX_ARENAS; i++) free_arena(i);
    if (vm.string_pool) { free(vm.string_pool); vm.string_pool = NULL; }

    for (int i = 0; i < ref_next_id; i++) {
        if (ref_store[i].is_copy && ref_store[i].ptr) free(ref_store[i].ptr);
        ref_store[i].ptr = NULL;
    }
    ref_next_id = 0;

    vm.sp = -1; vm.ip = 0; vm.str_count = 0;
    if (vm.global_symbols) { free(vm.global_symbols); vm.global_symbols = NULL; }
    if (vm.local_symbols) { free(vm.local_symbols); vm.local_symbols = NULL; }
}

void vm_init() {
    if (vm.bytecode) vm_cleanup();

    vm.bytecode = (int*)malloc(MAX_CODE * sizeof(int));
    vm.lines    = (int*)malloc(MAX_CODE * sizeof(int));

    // --- FIX START: Use calloc to zero-initialize memory ---
    vm.stack       = (double*)calloc(STACK_SIZE, sizeof(double));
    vm.stack_types = (int*)calloc(STACK_SIZE, sizeof(int));
    vm.globals      = (double*)calloc(MAX_GLOBALS, sizeof(double));
    vm.global_types = (int*)calloc(MAX_GLOBALS, sizeof(int));
    // --- FIX END ---

    vm.constants = (double*)malloc(MAX_CONSTANTS * sizeof(double));
    vm.string_pool = malloc(MAX_STRINGS * MAX_STRING_LENGTH);
    memset(vm.arenas, 0, sizeof(vm.arenas));

    init_arena(0);
    vm.current_arena = 0;

    if (!vm.bytecode || !vm.stack) {
        fprintf(stderr, "Critical Error: Failed to allocate VM memory\n");
        exit(1);
    }

    vm.sp = -1;
    vm.ip = 0;
    vm.fp = 0;
    vm.code_size = 0;
    vm.str_count = 0;
    vm.const_count = 0;
    vm.function_count = 0;
    vm.output_mem_pos = 0;
    vm.output_char_buffer[0] = '\0';
    vm.global_symbols = NULL;
    vm.global_symbol_count = 0;
    vm.local_symbols = NULL;
    vm.local_symbol_count = 0;
    // New fields
    vm.source_code = NULL;
    vm.cli_debug_mode = false;
    memset(natives, 0, sizeof(natives));
}

// --- Pointer Resolution ---

double* vm_resolve_ptr(double ptr_val) {
    int id = UNPACK_ARENA(ptr_val);
    int offset = UNPACK_OFFSET(ptr_val);
    int gen = UNPACK_GEN(ptr_val);

    if (id < 0 || id >= MAX_ARENAS) {
        RUNTIME_ERROR("Access violation: Invalid Region ID %d", id);
        return NULL;
    }
    if (!vm.arenas[id].active) {
        RUNTIME_ERROR("Access violation: Region %d is freed", id);
        return NULL;
    }
    if (vm.arenas[id].generation != gen) {
        RUNTIME_ERROR("Access violation: Stale pointer to recycled Region %d", id);
        return NULL;
    }
    if (offset < 0 || offset >= vm.arenas[id].capacity) {
        RUNTIME_ERROR("Heap overflow access");
        return NULL;
    }
    return &vm.arenas[id].memory[offset];
}

int* vm_resolve_type(double ptr_val) {
    int id = UNPACK_ARENA(ptr_val);
    int offset = UNPACK_OFFSET(ptr_val);
    int gen = UNPACK_GEN(ptr_val);

    if (id < 0 || id >= MAX_ARENAS || !vm.arenas[id].active) return NULL;
    if (vm.arenas[id].generation != gen) return NULL;
    return &vm.arenas[id].types[offset];
}

double heap_alloc(int size) {
    int id = vm.current_arena;
    if (!vm.arenas[id].active) init_arena(id);

    if (vm.arenas[id].head + size >= vm.arenas[id].capacity) {
        printf("Error: Heap Overflow in Region %d!\n", id);
        exit(1);
    }
    int offset = vm.arenas[id].head;
    vm.arenas[id].head += size;
    return PACK_PTR(vm.arenas[id].generation, id, offset);
}

// --- Stack & Utils ---

void vm_push(double val, int type) {
    if (vm.sp >= STACK_SIZE - 1) { printf("Error: Stack Overflow\n"); exit(1); }
    vm.sp++;
    vm.stack[vm.sp] = val;
    vm.stack_types[vm.sp] = type;
}

double vm_pop() {
    CHECK_STACK(1);
    return vm.stack[vm.sp--];
}

int make_string(const char *s) {
    for (int j = 0; j < vm.str_count; j++) {
        if (strcmp(vm.string_pool[j], s) == 0) return j;
    }
    if (vm.str_count >= MAX_STRINGS) { printf("Error: String Pool Overflow\n"); exit(1); }
    strncpy(vm.string_pool[vm.str_count], s, MAX_STRING_LENGTH - 1);
    return vm.str_count++;
}

int make_const(double val) {
    for (int i = 0; i < vm.const_count; i++) {
        if (vm.constants[i] == val) return i;
    }
    vm.constants[vm.const_count] = val;
    return vm.const_count++;
}

void vm_register_function(VM* vm, const char* name, int addr) {
    if (vm->function_count >= MAX_VM_FUNCTIONS) { printf("VM Limit: Too many functions.\n"); exit(1); }
    VMFunction* f = &vm->functions[vm->function_count++];
    strncpy(f->name, name, 63);
    f->name[63] = '\0';
    f->addr = addr;
}

int vm_find_function(VM* vm, const char* name) {
    for (int i = 0; i < vm->function_count; i++) {
        if (strcmp(vm->functions[i].name, name) == 0) return vm->functions[i].addr;
    }
    return -1;
}

// --- Printing ---

void print_raw(const char* str) {
    if (MyloConfig.debug_mode && MyloConfig.print_callback) {
        MyloConfig.print_callback(str);
    } else if (MyloConfig.print_to_memory) {
        vm.output_mem_pos += sprintf(vm.output_char_buffer + vm.output_mem_pos, "%s", str);
    } else {
        printf("%s", str);
    }
}

void print_recursive(double val, int type, int depth, int max_elem) {
    if (depth > 10) { print_raw("..."); return; }
    char buf[64];

    if (type == T_NUM) {
        if (val == (int)val) sprintf(buf, "%d", (int)val);
        else sprintf(buf, "%g", val);
        print_raw(buf);
    } else if (type == T_STR) {
        if (depth > 0) print_raw("\"");
        print_raw(vm.string_pool[(int)val]);
        if (depth > 0) print_raw("\"");
    } else if (type == T_OBJ) {
        int arena_id = UNPACK_ARENA(val);
        int gen = UNPACK_GEN(val);

        if (arena_id < 0 || arena_id >= MAX_ARENAS || !vm.arenas[arena_id].active) {
            print_raw("[Freed Object]");
            return;
        }
        if (vm.arenas[arena_id].generation != gen) {
            print_raw("[Stale Object Ref]");
            return;
        }

        double* base = vm_resolve_ptr_safe(val);
        if (!base) {
            if (val == 0.0) print_raw("null");
            else print_raw("[Invalid Ref]");
            return;
        }
        int* types = vm_resolve_type(val);
        int obj_type = (int)base[HEAP_OFFSET_TYPE];

        if (obj_type == TYPE_ARRAY) {
            print_raw("[");
            int len = (int)base[HEAP_OFFSET_LEN];
            int limit = (max_elem != -1 && len > max_elem) ? max_elem : len;

            for (int i = 0; i < limit; i++) {
                if (i > 0) print_raw(", ");
                print_recursive(base[HEAP_HEADER_ARRAY + i], types[HEAP_HEADER_ARRAY + i], depth + 1, max_elem);
            }
            if (len > limit) print_raw(", ...");
            print_raw("]");
        } else if (obj_type == TYPE_MAP) {
            print_raw("{");
            int count = (int)base[HEAP_OFFSET_COUNT];
            int limit = (max_elem != -1 && count > max_elem) ? max_elem : count;

            double* data = vm_resolve_ptr(base[HEAP_OFFSET_DATA]);
            int* data_types = vm_resolve_type(base[HEAP_OFFSET_DATA]);

            if(data) {
                for (int i = 0; i < limit; i++) {
                    if (i > 0) print_raw(", ");
                    print_raw(vm.string_pool[(int)data[i * 2]]);
                    print_raw(": ");
                    print_recursive(data[i * 2 + 1], data_types[i * 2 + 1], depth + 1, max_elem);
                }
                if (count > limit) print_raw(", ...");
            }
            print_raw("}");
        } else if (obj_type == TYPE_BYTES) {
            int len = (int)base[HEAP_OFFSET_LEN];
            unsigned char* b = (unsigned char*)&base[HEAP_HEADER_ARRAY];
            print_raw("b\"");
            int limit = (max_elem != -1 && len > max_elem) ? max_elem : len;
            for (int i = 0; i < limit; i++) {
                if (b[i] >= 32 && b[i] <= 126) sprintf(buf, "%c", b[i]);
                else sprintf(buf, "\\x%02X", b[i]);
                print_raw(buf);
            }
            if (len > limit) print_raw("...");
            print_raw("\"");
        } else if (obj_type <= TYPE_I16_ARRAY && obj_type >= TYPE_BOOL_ARRAY) {
            print_raw("[");
            int len = (int)base[HEAP_OFFSET_LEN];
            char* data = (char*)&base[HEAP_HEADER_ARRAY];
            int limit = (max_elem != -1 && len > max_elem) ? max_elem : len;
            for (int i = 0; i < limit; i++) {
                if (i > 0) print_raw(", ");
                if (obj_type == TYPE_I32_ARRAY) sprintf(buf, "%d", ((int*)data)[i]);
                else if (obj_type == TYPE_F32_ARRAY) sprintf(buf, "%g", ((float*)data)[i]);
                else if (obj_type == TYPE_I16_ARRAY) sprintf(buf, "%d", ((short*)data)[i]);
                else if (obj_type == TYPE_I64_ARRAY) sprintf(buf, "%lld", ((long long*)data)[i]);
                else if (obj_type == TYPE_BOOL_ARRAY) { print_raw(((unsigned char*)data)[i] ? "true" : "false"); continue; }
                print_raw(buf);
            }
            if (len > limit) print_raw(", ...");
            print_raw("]");
        } else {
            sprintf(buf, "[Struct Ref:%d]", UNPACK_OFFSET(val));
            print_raw(buf);
        }
    }
}

// --- TUI Debugger Implementation ---

#define DBG_COLOR_RESET "\033[0m"
#define DBG_COLOR_CYAN  "\033[36m"
#define DBG_COLOR_YELLOW "\033[33m"
#define DBG_COLOR_GREEN "\033[32m"
#define DBG_COLOR_RED   "\033[31m"
#define DBG_BG_BLUE     "\033[44m"
#define DBG_BG_GRAY     "\033[100m"

static int dbg_breakpoints[64];
static int dbg_bp_count = 0;

void dbg_print_source_window(int current_line, int context_lines) {
    if (!vm.source_code) {
        printf("  [No source code available]\n");
        return;
    }

    // Naive line splitter for display
    int line = 1;
    char* ptr = vm.source_code;
    char* start = ptr;

    printf(DBG_COLOR_CYAN "┌─ Source ──────────────────────────────────────────────────┐\n" DBG_COLOR_RESET);

    while (*ptr) {
        if (*ptr == '\n') {
            if (line >= current_line - context_lines && line <= current_line + context_lines) {
                // Print this line
                int len = (int)(ptr - start);

                if (line == current_line) {
                    printf(DBG_BG_BLUE "│ %4d > %.*s" DBG_COLOR_RESET "\n", line, len, start);
                } else {
                    // Check breakpoints
                    bool is_bp = false;
                    for(int i=0; i<dbg_bp_count; i++) if(dbg_breakpoints[i] == line) is_bp = true;

                    if (is_bp) printf("│ " DBG_COLOR_RED "●" DBG_COLOR_RESET " %4d | %.*s\n", line, len, start);
                    else printf("│ %4d | %.*s\n", line, len, start);
                }
            }
            start = ptr + 1;
            line++;
        }
        ptr++;
    }
    printf(DBG_COLOR_CYAN "└───────────────────────────────────────────────────────────┘\n" DBG_COLOR_RESET);
}

void dbg_print_state_window() {
    printf(DBG_COLOR_YELLOW "┌─ State (FP: %d | SP: %d) ──────────────────────────┐\n" DBG_COLOR_RESET, vm.fp, vm.sp);

    // Globals (Limited view)
    printf("  " DBG_COLOR_GREEN "[Globals]" DBG_COLOR_RESET "\n");
    for (int i = 0; i < vm.global_symbol_count; i++) {
        if (i > 5) { printf("    ... (%d more)\n", vm.global_symbol_count - 5); break; }
        int addr = vm.global_symbols[i].addr;
        printf("    %s: ", vm.global_symbols[i].name);
        print_recursive(vm.globals[addr], vm.global_types[addr], 0, 1);
        printf("\n");
    }

    // Locals
    printf("  " DBG_COLOR_GREEN "[Locals]" DBG_COLOR_RESET "\n");
    bool found = false;
    if (vm.local_symbols) {
        for (int i = 0; i < vm.local_symbol_count; i++) {
            VMLocalInfo* sym = &vm.local_symbols[i];
            if ((vm.ip - 1) >= sym->start_ip && (sym->end_ip == -1 || (vm.ip - 1) <= sym->end_ip)) {
                int stack_idx = vm.fp + sym->stack_offset;
                if (stack_idx <= vm.sp) {
                    printf("    %s: ", sym->name);
                    print_recursive(vm.stack[stack_idx], vm.stack_types[stack_idx], 0, 1);
                    printf("\n");
                    found = true;
                }
            }
        }
    }
    if (!found) printf("    (None)\n");
    printf(DBG_COLOR_YELLOW "└───────────────────────────────────────────────────────────┘\n" DBG_COLOR_RESET);
}

void enter_debugger() {
    // Clear screen (ANSI)
    printf("\033[2J\033[H");

    int current_line = vm.lines[vm.ip > 0 ? vm.ip - 1 : 0];

    dbg_print_source_window(current_line, 5);
    dbg_print_state_window();

    while (1) {
        printf(DBG_COLOR_RED "(mylo-db)" DBG_COLOR_RESET " ");
        char buf[64];
        if (!fgets(buf, 64, stdin)) return;

        if (buf[0] == 'n') { // Next Line (Step Over/Next)
            int start_line = current_line;
            // Run until line changes
            while (vm.ip < vm.code_size) {
                 int op = vm_step(false);
                 if (op == -1) exit(0);
                 if (op == OP_DEBUGGER) break; // Hit manual breakpoint
                 int new_line = vm.lines[vm.ip];
                 if (new_line != start_line && new_line != 0) break;
            }
            enter_debugger(); // Re-enter UI at new spot
            return;
        }
        else if (buf[0] == 's') { // Step Instruction
            // Execute exactly one instruction
            int op = vm_step(true); // Trace mode prints op
            if (op == -1) exit(0);
            enter_debugger();
            return;
        }
        else if (buf[0] == 'c') { // Continue
            return; // Exit debugger loop, resume VM loop
        }
        else if (buf[0] == 'b') { // Breakpoint
            int line = atoi(buf + 1);
            if (line > 0) {
                dbg_breakpoints[dbg_bp_count++] = line;
                printf("Breakpoint set at line %d\n", line);
            }
        }
        else if (buf[0] == 'q') {
            exit(0);
        }
        else {
            printf("Commands: [n]ext line, [s]tep instr, [c]ontinue, [b]reak <line>, [q]uit\n");
        }
    }
}

// --- Logic Implementation ---
// (Logic, Math, Broadcast functions are unchanged)
static void broadcast_math(int op, double obj_val, double scalar_val, int scalar_type, bool obj_is_lhs) {
    double* base = vm_resolve_ptr(obj_val);
    int* types = vm_resolve_type(obj_val);
    int hType = (int)base[0];
    int len = (int)base[1];

    double newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);
    double* newBase = vm_resolve_ptr(newPtr);
    int* newTypes = vm_resolve_type(newPtr);

    newBase[0] = TYPE_ARRAY;
    newBase[1] = (double)len;

    for(int i=0; i<len; i++) {
        double el = 0;
        int elType = T_NUM;
        if(hType == TYPE_BYTES) {
            unsigned char* b = (unsigned char*)&base[HEAP_HEADER_ARRAY];
            el = (double)b[i];
        } else if (hType == TYPE_ARRAY) {
            el = base[HEAP_HEADER_ARRAY+i];
            elType = types[HEAP_HEADER_ARRAY+i];
        } else if (hType == TYPE_I32_ARRAY) el = (double)((int*)&base[HEAP_HEADER_ARRAY])[i];
        else if (hType == TYPE_F32_ARRAY) el = (double)((float*)&base[HEAP_HEADER_ARRAY])[i];

        if (scalar_type == T_NUM && elType == T_NUM) {
             double lhs = obj_is_lhs ? el : scalar_val;
             double rhs = obj_is_lhs ? scalar_val : el;
             double res = 0;
             if (op == OP_ADD) res = lhs + rhs;
             else if (op == OP_SUB) res = lhs - rhs;
             else if (op == OP_MUL) res = lhs * rhs;
             else if (op == OP_DIV) res = lhs / rhs;
             else if (op == OP_MOD) res = fmod(lhs, rhs);
             newBase[HEAP_HEADER_ARRAY+i] = res;
             newTypes[HEAP_HEADER_ARRAY+i] = T_NUM;
        } else {
             newBase[HEAP_HEADER_ARRAY+i] = 0;
             newTypes[HEAP_HEADER_ARRAY+i] = T_NUM;
        }
    }
    vm_push(newPtr, T_OBJ);
}

static void exec_math_op(int op) {
    CHECK_STACK(2);
    double b = vm_pop(); int tb = vm.stack_types[vm.sp + 1];
    double a = vm.stack[vm.sp]; int ta = vm.stack_types[vm.sp];

    if (ta == T_NUM && tb == T_NUM) {
        double res = 0;
        if (op == OP_ADD) res = a + b;
        else if (op == OP_SUB) res = a - b;
        else if (op == OP_MUL) res = a * b;
        else if (op == OP_DIV) res = a / b;
        else if (op == OP_MOD) res = fmod(a, b);
        vm.stack[vm.sp] = res;
        vm.stack_types[vm.sp] = T_NUM;
        return;
    }
    if (op == OP_ADD && ta == T_STR && tb == T_STR) {
        char buf[MAX_STRING_LENGTH * 2];
        snprintf(buf, sizeof(buf), "%s%s", vm.string_pool[(int)a], vm.string_pool[(int)b]);
        int id = make_string(buf);
        vm.stack[vm.sp] = (double)id;
        vm.stack_types[vm.sp] = T_STR;
        return;
    }
    if (ta == T_OBJ || tb == T_OBJ) {
        double arrVal = (ta == T_OBJ) ? a : b;
        double scalVal = (ta == T_OBJ) ? b : a;
        int scalType = (ta == T_OBJ) ? tb : ta;
        bool objIsLhs = (ta == T_OBJ);
        vm_pop();
        broadcast_math(op, arrVal, scalVal, scalType, objIsLhs);
        return;
    }
    RUNTIME_ERROR("Invalid types for math operation");
}

static void exec_compare_op(int op) {
    CHECK_STACK(2);
    double b = vm_pop();
    double a = vm.stack[vm.sp];
    double res = 0;
    switch(op) {
        case OP_LT: res = (a < b); break;
        case OP_GT: res = (a > b); break;
        case OP_LE: res = (a <= b); break;
        case OP_GE: res = (a >= b); break;
        case OP_EQ: res = (a == b); break;
        case OP_NEQ: res = (a != b); break;
    }
    vm.stack[vm.sp] = res;
}

static void exec_var_op(int op) {
    int arg = vm.bytecode[vm.ip++];
    if (op == OP_SET) {
        CHECK_STACK(1);
        vm.globals[arg] = vm.stack[vm.sp];
        vm.global_types[arg] = vm.stack_types[vm.sp];
        vm.sp--;
    } else if (op == OP_GET) {
        vm_push(vm.globals[arg], vm.global_types[arg]);
    } else if (op == OP_LVAR) {
        int fp = (int)vm.fp;
        vm_push(vm.stack[fp + arg], vm.stack_types[fp + arg]);
    } else if (op == OP_SVAR) {
        CHECK_STACK(1);
        int fp = (int)vm.fp;
        vm.stack[fp + arg] = vm.stack[vm.sp];
        vm.stack_types[fp + arg] = vm.stack_types[vm.sp];
        vm.sp--;
    }
}

static void exec_flow_op(int op) {
    if (op == OP_JMP) {
        vm.ip = vm.bytecode[vm.ip];
    } else if (op == OP_JZ) {
        int target = vm.bytecode[vm.ip++];
        if (vm_pop() == 0.0) vm.ip = target;
    } else if (op == OP_JNZ) {
        int target = vm.bytecode[vm.ip++];
        if (vm_pop() != 0.0) vm.ip = target;
    } else if (op == OP_CALL) {
        int target = vm.bytecode[vm.ip++];
        int argc = vm.bytecode[vm.ip++];
        CHECK_STACK(argc);
        int args_start = vm.sp - argc + 1;
        for(int i=0; i<argc; i++) {
            vm.stack[vm.sp + 2 - i] = vm.stack[vm.sp - i];
            vm.stack_types[vm.sp + 2 - i] = vm.stack_types[vm.sp - i];
        }
        vm.stack[args_start] = (double)vm.ip;
        vm.stack[args_start+1] = (double)vm.fp;
        vm.stack_types[args_start] = T_NUM;
        vm.stack_types[args_start+1] = T_NUM;
        vm.sp += 2;
        vm.fp = args_start + 2;
        vm.ip = target;
    } else if (op == OP_RET) {
        CHECK_STACK(1);
        double rv = vm.stack[vm.sp];
        int rt = vm.stack_types[vm.sp];
        vm.sp = vm.fp - 3;
        int fp = (int)vm.fp;
        vm.fp = (int)vm.stack[fp-1];
        vm.ip = (int)vm.stack[fp-2];
        vm_push(rv, rt);
    }
}

static void exec_alloc_op(int op) {
    if (op == OP_ALLOC) {
        int size = vm.bytecode[vm.ip++];
        int struct_id = vm.bytecode[vm.ip++];
        double ptr = heap_alloc(size + 1);
        double* base = vm_resolve_ptr(ptr);
        base[0] = (double)struct_id;
        vm_push(ptr, T_OBJ);
    } else if (op == OP_HSET) {
        int off = vm.bytecode[vm.ip++];
        int expected_id = vm.bytecode[vm.ip++];
        CHECK_STACK(2);
        double v = vm_pop(); int t = vm.stack_types[vm.sp+1];
        double p = vm.stack[vm.sp];
        double* base = vm_resolve_ptr(p);
        int* types = vm_resolve_type(p);
        if((int)base[0] != expected_id) RUNTIME_ERROR("HSET Type mismatch");
        base[1+off] = v;
        types[1+off] = t;
    } else if (op == OP_HGET) {
        int off = vm.bytecode[vm.ip++];
        int expected_id = vm.bytecode[vm.ip++];
        CHECK_STACK(1);
        double p = vm.stack[vm.sp];
        double* base = vm_resolve_ptr(p);
        int* types = vm_resolve_type(p);
        if((int)base[0] != expected_id) RUNTIME_ERROR("HGET Type mismatch");
        vm.stack[vm.sp] = base[1+off];
        vm.stack_types[vm.sp] = types[1+off];
    }
}

static void exec_array_op(int op) {
    if (op == OP_ARR) {
        int count = vm.bytecode[vm.ip++];
        double ptr = heap_alloc(count+2);
        double* base = vm_resolve_ptr(ptr);
        int* types = vm_resolve_type(ptr);
        base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
        base[HEAP_OFFSET_LEN] = (double)count;
        for(int i=count; i>0; i--) {
            base[HEAP_HEADER_ARRAY+i-1] = vm.stack[vm.sp];
            types[HEAP_HEADER_ARRAY+i-1] = vm.stack_types[vm.sp];
            vm.sp--;
        }
        vm_push(ptr, T_OBJ);
    } else if (op == OP_AGET) {
        CHECK_STACK(2);
        double key = vm_pop();
        double ptr = vm_pop();
        double* base = vm_resolve_ptr(ptr);
        int* types = vm_resolve_type(ptr);
        int type = (int)base[HEAP_OFFSET_TYPE];

        if (type <= TYPE_ARRAY && type != TYPE_MAP) {
            int idx = (int)key;
            int len = (int)base[HEAP_OFFSET_LEN];
            if (idx < 0) idx += len;
            if (idx < 0 || idx >= len) RUNTIME_ERROR("Index OOB");

            if (type == TYPE_ARRAY) {
                vm_push(base[HEAP_HEADER_ARRAY+idx], types[HEAP_HEADER_ARRAY+idx]);
            } else {
                char* data = (char*)&base[HEAP_HEADER_ARRAY];
                double res = 0;
                switch(type) {
                    case TYPE_BYTES:      res = (double)((unsigned char*)data)[idx]; break;
                    case TYPE_BOOL_ARRAY: res = (double)((unsigned char*)data)[idx]; break;
                    case TYPE_I16_ARRAY:  res = (double)((short*)data)[idx]; break;
                    case TYPE_I32_ARRAY:  res = (double)((int*)data)[idx]; break;
                    case TYPE_F32_ARRAY:  res = (double)((float*)data)[idx]; break;
                    case TYPE_I64_ARRAY:  res = (double)((long long*)data)[idx]; break;
                }
                vm_push(res, T_NUM);
            }
        } else if (type == TYPE_MAP) {
            int count = (int)base[HEAP_OFFSET_COUNT];
            double* data = vm_resolve_ptr(base[HEAP_OFFSET_DATA]);
            int* data_types = vm_resolve_type(base[HEAP_OFFSET_DATA]);
            bool found = false;
            for(int i=0; i<count; i++) {
                if(data[i*2] == key) {
                    vm_push(data[i*2 + 1], data_types[i*2 + 1]);
                    found = true; break;
                }
            }
            if (!found) {
                int empty = make_string("");
                vm_push((double)empty, T_STR);
            }
        }
    } else if (op == OP_ALEN) {
        CHECK_STACK(1);
        double ptr = vm_pop();
        double* base = vm_resolve_ptr(ptr);
        int type = (int)base[HEAP_OFFSET_TYPE];
        if (type == TYPE_MAP) vm_push(base[HEAP_OFFSET_COUNT], T_NUM);
        else vm_push(base[HEAP_OFFSET_LEN], T_NUM);
    } else if (op == OP_ASET) {
        CHECK_STACK(3);
        double val = vm_pop(); int vt = vm.stack_types[vm.sp+1];
        double key = vm_pop();
        double ptr = vm.stack[vm.sp];
        double* base = vm_resolve_ptr(ptr);
        int* types = vm_resolve_type(ptr);
        int type = (int)base[0];

        vm_pop();

        if (type == TYPE_ARRAY) {
            int idx = (int)key;
            base[2+idx] = val; types[2+idx] = vt;
        } else if (type == TYPE_BYTES) {
            unsigned char* b = (unsigned char*)&base[HEAP_HEADER_ARRAY];
            b[(int)key] = (unsigned char)val;
        } else if (type == TYPE_MAP) {
            int cap = (int)base[1];
            int count = (int)base[2];
            double data_ptr = base[3];
            double* data = vm_resolve_ptr(data_ptr);
            int* data_types = vm_resolve_type(data_ptr);
            bool found = false;
            for(int i=0; i<count; i++) {
                if (data[i*2] == key) {
                    data[i*2+1] = val; data_types[i*2+1] = vt;
                    found = true; break;
                }
            }
            if(!found) {
                if (count >= cap) {
                    int new_cap = cap * 2;
                    double new_data_ptr = heap_alloc(new_cap*2);
                    double* new_data = vm_resolve_ptr(new_data_ptr);
                    int* new_types = vm_resolve_type(new_data_ptr);
                    for(int i=0; i<count*2; i++) {
                         new_data[i] = data[i]; new_types[i] = data_types[i];
                    }
                    base[1] = (double)new_cap;
                    base[3] = new_data_ptr;
                    data = new_data; data_types = new_types;
                }
                data[count*2] = key;
                data_types[count*2] = T_STR;
                data[count*2+1] = val;
                data_types[count*2+1] = vt;
                base[2] = (double)(count+1);
            }
        }
        else {
             int idx = (int)key;
             char* data = (char*)&base[HEAP_HEADER_ARRAY];
             switch(type) {
                case TYPE_I32_ARRAY: ((int*)data)[idx] = (int)val; break;
                case TYPE_F32_ARRAY: ((float*)data)[idx] = (float)val; break;
                case TYPE_I16_ARRAY: ((short*)data)[idx] = (short)val; break;
                case TYPE_I64_ARRAY: ((long long*)data)[idx] = (long long)val; break;
                case TYPE_BOOL_ARRAY: ((unsigned char*)data)[idx] = (unsigned char)val; break;
             }
        }
        vm_push(val, vt);
    } else if (op == OP_MAP) {
        int cap = MAP_INITIAL_CAP;
        double map = heap_alloc(4);
        double data = heap_alloc(cap*2);
        double* base = vm_resolve_ptr(map);
        base[0] = TYPE_MAP;
        base[1] = (double)cap;
        base[2] = 0;
        base[3] = data;
        vm_push(map, T_OBJ);
    } else if (op == OP_MAKE_ARR) {
        int count = vm.bytecode[vm.ip++];
        int type_id = vm.bytecode[vm.ip++];
        int elem_size = get_type_size(type_id);
        int doubles_needed = (count * elem_size + 7) / 8;

        double ptr = heap_alloc(doubles_needed + HEAP_HEADER_ARRAY);
        double* base = vm_resolve_ptr(ptr);
        base[0] = (double)type_id;
        base[1] = (double)count;
        char* data_ptr = (char*)&base[HEAP_HEADER_ARRAY];

        for(int i = count - 1; i >= 0; i--) {
            double val = vm.stack[vm.sp--];
            switch(type_id) {
                case TYPE_BYTES:
                case TYPE_BOOL_ARRAY: data_ptr[i] = (unsigned char)val; break;
                case TYPE_I16_ARRAY: ((short*)data_ptr)[i] = (short)val; break;
                case TYPE_I32_ARRAY: ((int*)data_ptr)[i] = (int)val; break;
                case TYPE_F32_ARRAY: ((float*)data_ptr)[i] = (float)val; break;
                case TYPE_I64_ARRAY: ((long long*)data_ptr)[i] = (long long)val; break;
            }
        }
        vm_push(ptr, T_OBJ);
    }
}

static void exec_slice_op(int op) {
    if (op == OP_SLICE) {
        CHECK_STACK(3);
        double e = vm_pop();
        double s = vm_pop();
        double ptr = vm_pop();
        double* base = vm_resolve_ptr(ptr);
        int* types = vm_resolve_type(ptr);
        int type = (int)base[HEAP_OFFSET_TYPE];
        int len = (int)base[HEAP_OFFSET_LEN];

        int start = (int)s; int end = (int)e;
        if (start < 0) start += len;
        if (end < 0) end += len;
        if (start < 0) start = 0;
        if (end >= len) end = len - 1;
        int newlen = (end >= start) ? (end - start + 1) : 0;
        if (start >= len) newlen = 0;

        if (type == TYPE_BYTES) {
            double newptr = heap_alloc((newlen + 7) / 8 + 2);
            double* newbase = vm_resolve_ptr(newptr);
            newbase[0] = TYPE_BYTES; newbase[1] = (double)newlen;
            char* src = (char*)&base[HEAP_HEADER_ARRAY];
            char* dst = (char*)&newbase[HEAP_HEADER_ARRAY];
            if (newlen > 0) for (int i = 0; i < newlen; i++) dst[i] = src[start + i];
            vm_push(newptr, T_OBJ);
        } else {
            double newptr = heap_alloc(newlen + 2);
            double* newbase = vm_resolve_ptr(newptr);
            int* newtypes = vm_resolve_type(newptr);
            newbase[0] = TYPE_ARRAY; newbase[1] = (double)newlen;
            for (int i = 0; i < newlen; i++) {
                newbase[2 + i] = base[2 + start + i];
                newtypes[2 + i] = types[2 + start + i];
            }
            vm_push(newptr, T_OBJ);
        }
    } else if (op == OP_SLICE_SET) {
        CHECK_STACK(4);
        double val = vm_pop();
        double e = vm_pop();
        double s = vm_pop();
        double ptr = vm.stack[vm.sp]; vm_pop();
        double* base = vm_resolve_ptr(ptr);
        int* types = vm_resolve_type(ptr);
        int type = (int)base[0];
        int len = (int)base[1];

        int start = (int)s; int end = (int)e;
        if (start < 0) start += len; if (end < 0) end += len;
        if (start < 0) start = 0; if (end >= len) end = len - 1;
        int slice_len = (end >= start) ? (end - start + 1) : 0;

        if (type == TYPE_BYTES) {
            double* vbase = vm_resolve_ptr(val);
            if ((int)vbase[0] == TYPE_BYTES) {
                unsigned char* dst = (unsigned char*)&base[HEAP_HEADER_ARRAY];
                unsigned char* src = (unsigned char*)&vbase[HEAP_HEADER_ARRAY];
                int vlen = (int)vbase[1];
                int copy_len = (slice_len < vlen) ? slice_len : vlen;
                for(int i=0; i<copy_len; i++) dst[start+i] = src[i];
            }
        } else if (type == TYPE_ARRAY) {
            double* vbase = vm_resolve_ptr(val);
            int* vtypes = vm_resolve_type(val);
            if ((int)vbase[0] == TYPE_ARRAY) {
                int vlen = (int)vbase[1];
                int copy_len = (slice_len < vlen) ? slice_len : vlen;
                for(int i=0; i<copy_len; i++) {
                    base[2+start+i] = vbase[2+i];
                    types[2+start+i] = vtypes[2+i];
                }
            }
        }
        vm_push(val, T_OBJ);
    }
}

static void exec_monitor() {
    printf("\n================ [ VM MONITOR ] ================\n");
    printf("--- Regions (Arenas) ---\n");
    printf("  ID | Usage (Doubles)     | Status\n");
    printf("  ---|---------------------|-------\n");
    for (int i = 0; i < MAX_ARENAS; i++) {
        if (vm.arenas[i].active) {
            bool is_current = (i == vm.current_arena);
            printf("  %2d | %6d / %-8d | %s%s\n", i, vm.arenas[i].head, vm.arenas[i].capacity, i == 0 ? "Main" : "Region", is_current ? " (Active Ctx)" : "");
        }
    }
    printf("================================================\n");
}

static void exec_cast_op(int target_type, bool is_check_only) {
    if (target_type == TYPE_ANY) return;

    double val = vm.stack[vm.sp];
    int current_type = vm.stack_types[vm.sp];

    if (target_type == TYPE_NUM || target_type == TYPE_I32_ARRAY || target_type == TYPE_I64_ARRAY ||
        target_type == TYPE_I16_ARRAY || target_type == TYPE_F32_ARRAY || target_type == TYPE_BOOL_ARRAY) {
        if (current_type != T_NUM) RUNTIME_ERROR("Type Mismatch: Expected Number");
        if (!is_check_only) {
            if (target_type == TYPE_I32_ARRAY) {
                vm.stack[vm.sp] = floor(val);
            }
        }
    }
    else if (target_type == TYPE_STR) {
        if (current_type != T_STR) RUNTIME_ERROR("Type Mismatch: Expected String");
    }
    else if (target_type >= 0) { // Struct ID
        if (current_type != T_OBJ) RUNTIME_ERROR("Type Mismatch: Expected Object");
        double* base = vm_resolve_ptr(val);
        if (!base || (int)base[0] != target_type) RUNTIME_ERROR("Type Mismatch: Expected Struct ID %d", target_type);
    }
}

// --- VM Execution Loop ---

int vm_step(bool debug_trace) {
    // 1. TUI Breakpoint Check (NEW)
    if (vm.cli_debug_mode) {
         static int last_stop_line = -1;
         int curr = vm.lines[vm.ip];

         bool hit = false;
         for(int i=0; i<dbg_bp_count; i++) {
             if (dbg_breakpoints[i] == curr && curr != last_stop_line) hit = true;
         }

         if (hit) {
             last_stop_line = curr;
             enter_debugger();
         }
         if (curr != last_stop_line) last_stop_line = -1;
    }

    if (vm.ip >= vm.code_size) return -1;

    if (debug_trace) {
        int op = vm.bytecode[vm.ip];
        if (op >= 0 && op <= OP_DEBUGGER) {
            printf("[TRACE] IP:%04d Line:%d SP:%2d OP:%s\n", vm.ip, vm.lines[vm.ip], vm.sp, OP_NAMES[op]);
        }
    }

    int op = vm.bytecode[vm.ip++];

    switch (op) {
        // Stack & Constants
        case OP_PSH_NUM: { int idx = vm.bytecode[vm.ip++]; vm_push(vm.constants[idx], T_NUM); break; }
        case OP_PSH_STR: { int idx = vm.bytecode[vm.ip++]; vm_push((double)idx, T_STR); break; }
        case OP_DUP: CHECK_STACK(1); vm_push(vm.stack[vm.sp], vm.stack_types[vm.sp]); break;
        case OP_POP: vm_pop(); break;

        // Math & Logic
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD: exec_math_op(op); break;
        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE:
        case OP_EQ:
        case OP_NEQ: exec_compare_op(op); break;

        // Variables
        case OP_SET:
        case OP_GET:
        case OP_LVAR:
        case OP_SVAR: exec_var_op(op); break;

        // Flow Control
        case OP_JMP:
        case OP_JZ:
        case OP_JNZ:
        case OP_CALL:
        case OP_RET: exec_flow_op(op); break;
        case OP_HLT: return -1;

        // Memory & Objects
        case OP_ALLOC:
        case OP_HSET:
        case OP_HGET: exec_alloc_op(op); break;

        // Arrays & Maps
        case OP_ARR:
        case OP_AGET:
        case OP_ALEN:
        case OP_ASET:
        case OP_MAP:
        case OP_MAKE_ARR: exec_array_op(op); break;

        // Slices
        case OP_SLICE:
        case OP_SLICE_SET: exec_slice_op(op); break;

        // Misc
        case OP_CAT: {
            CHECK_STACK(2);
            double b = vm_pop(); int bt = vm.stack_types[vm.sp + 1];
            double a = vm.stack[vm.sp]; int at = vm.stack_types[vm.sp];
            char s1[MAX_STRING_LENGTH], s2[MAX_STRING_LENGTH];

            if (at == T_STR) strncpy(s1, vm.string_pool[(int)a], MAX_STRING_LENGTH-1);
            else snprintf(s1, MAX_STRING_LENGTH, "%g", a);
            s1[MAX_STRING_LENGTH-1] = '\0';

            if (bt == T_STR) strncpy(s2, vm.string_pool[(int)b], MAX_STRING_LENGTH-1);
            else snprintf(s2, MAX_STRING_LENGTH, "%g", b);
            s2[MAX_STRING_LENGTH-1] = '\0';

            char res[MAX_STRING_LENGTH * 2];
            snprintf(res, sizeof(res), "%s%s", s1, s2);
            int id = make_string(res);
            vm.stack[vm.sp] = (double)id;
            vm.stack_types[vm.sp] = T_STR;
            break;
        }
        case OP_PRN: {
            CHECK_STACK(1);
            double v = vm.stack[vm.sp]; int t = vm.stack_types[vm.sp]; vm.sp--;
            print_recursive(v, t, 0, -1);
            print_raw("\n");
            break;
        }
        case OP_NATIVE: {
            int id = vm.bytecode[vm.ip++];
            if(natives[id]) natives[id](&vm);
            else RUNTIME_ERROR("Unknown Native ID %d", id);
            break;
        }
        case OP_MK_BYTES: {
            CHECK_STACK(1);
            double v = vm_pop();
            const char* s = vm.string_pool[(int)v];
            int len = strlen(s);
            double ptr = heap_alloc((len+7)/8 + 2);
            double* base = vm_resolve_ptr(ptr);
            base[0] = TYPE_BYTES; base[1] = (double)len;
            char* b = (char*)&base[2];
            memcpy(b, s, len);
            vm_push(ptr, T_OBJ);
            break;
        }
        case OP_EMBED: {
            int len = vm.bytecode[vm.ip++];
            double ptr = heap_alloc((len+7)/8 + 2);
            double* base = vm_resolve_ptr(ptr);
            base[0] = TYPE_BYTES; base[1] = (double)len;
            unsigned char* dst = (unsigned char*)&base[2];
            for(int i=0; i<len; i++) dst[i] = (unsigned char)vm.bytecode[vm.ip++];
            vm_push(ptr, T_OBJ);
            break;
        }

        // Iterators
        case OP_IT_KEY:
        case OP_IT_VAL:
        case OP_IT_DEF: {
            CHECK_STACK(2);
            double idx_val = vm_pop();
            double obj_val = vm_pop();
            int idx = (int)idx_val;

            double* base = vm_resolve_ptr(obj_val);
            int* types = vm_resolve_type(obj_val);
            int type = (int)base[0];

            if (type == TYPE_ARRAY || type == TYPE_BYTES) {
                int len = (int)base[1];
                if (idx < 0 || idx >= len) RUNTIME_ERROR("Iterator OOB");
                if (op == OP_IT_KEY) vm_push((double)idx, T_NUM);
                else {
                    if (type == TYPE_BYTES) vm_push((double)((unsigned char*)&base[2])[idx], T_NUM);
                    else vm_push(base[2+idx], types[2+idx]);
                }
            } else if (type == TYPE_MAP) {
                int count = (int)base[HEAP_OFFSET_COUNT];
                if (idx < 0 || idx >= count) RUNTIME_ERROR("Iterator OOB");
                double* data = vm_resolve_ptr(base[HEAP_OFFSET_DATA]);
                int* data_types = vm_resolve_type(base[HEAP_OFFSET_DATA]);
                if (op == OP_IT_VAL) vm_push(data[idx*2+1], data_types[idx*2+1]);
                else vm_push(data[idx*2], data_types[idx*2]);
            }
            break;
        }

        // Arenas
        case OP_NEW_ARENA: {
            int id = -1;
            for(int i=1; i<MAX_ARENAS; i++) { if(!vm.arenas[i].active) { id = i; break; } }
            if(id == -1) RUNTIME_ERROR("Max Regions Reached");
            init_arena(id);
            vm_push((double)id, T_NUM);
            break;
        }
        case OP_DEL_ARENA: {
            int id = (int)vm_pop();
            if(id <= 0) RUNTIME_ERROR("Cannot clear main region or invalid region");
            free_arena(id);
            break;
        }
        case OP_SET_CTX: {
            int id = (int)vm_pop();
            if (id < 0 || id >= MAX_ARENAS) RUNTIME_ERROR("Invalid Region ID");
            if (!vm.arenas[id].active && id != 0) RUNTIME_ERROR("Region %d is not active", id);
            vm.current_arena = id;
            break;
        }
        case OP_MONITOR: exec_monitor(); break;

        case OP_CAST: {
            int target = vm.bytecode[vm.ip++];
            exec_cast_op(target, false);
            break;
        }
        case OP_CHECK_TYPE: {
            int target = vm.bytecode[vm.ip++];
            exec_cast_op(target, true);
            break;
        }
        // 2. TUI Manual Breakpoint (NEW)
        case OP_DEBUGGER:
            if (vm.cli_debug_mode) {
                printf("Hit debug() statement.\n");
                enter_debugger();
            }
            break;
    }
    return op;
}

void run_vm_from(int start_ip, bool debug_trace) {
    vm.ip = start_ip;
    while (vm.ip < vm.code_size) {
        if (vm_step(debug_trace) == -1) break;
    }
}

void run_vm(bool debug_trace) {
    run_vm_from(0, debug_trace);
}