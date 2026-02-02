#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "vm.h"
#include <setjmp.h>
#include <stdarg.h> 


void mylo_runtime_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    // Print Error
    printf("[Line %d] Runtime Error: ", vm.lines[vm.ip - 1]);
    vprintf(fmt, args);
    printf("\n");
    
    va_end(args);

    // RECOVERY: If we are in REPL mode, jump back!
    if (MyloConfig.repl_jmp_buf) {
        longjmp(*(jmp_buf*)MyloConfig.repl_jmp_buf, 1);
    } else {
        // Standard mode: Crash
        exit(1);
    }
}

// Update the Macro to call our new function
#define RUNTIME_ERROR(fmt, ...) mylo_runtime_error(fmt, ##__VA_ARGS__)

// Global Instance
VM vm;
NativeFunc natives[MAX_NATIVES];
MyloConfigType MyloConfig = {false, false, NULL}; // Default: No debug, no callback

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
    "IT_KEY",
    "IT_VAL",
    "IT_DEF",
    "EMBED",
    "MAKE_ARR",
    "NEW_REG",
    "DEL_REG",
    "SET_CTX"
};

// Helper to get element size in bytes
int get_type_size(int heap_type) {
    switch(heap_type) {
        case TYPE_BYTES:
        case TYPE_BOOL_ARRAY: return 1;
        case TYPE_I16_ARRAY:
        case TYPE_F16_ARRAY:  return 2; // Treat as i16 storage for now
        case TYPE_I32_ARRAY:
        case TYPE_F32_ARRAY:  return 4;
        case TYPE_I64_ARRAY:
        default: return 8; // TYPE_ARRAY (doubles)
    }
}

// --- REFERENCE MANAGEMENT ---
// This allows C-Blocks to store generic pointers/structs in the VM
typedef struct {
    void* ptr;
    unsigned long type_hash;
    bool is_copy;
} RefEntry;

#define MAX_REFS 1024
static RefEntry ref_store[MAX_REFS];
static int ref_next_id = 0;

// Simple Hash (djb2)
unsigned long vm_hash_str(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

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


// --- VM CORE & ARENA MANAGEMENT ---

void init_arena(int id) {
    if (id < 0 || id >= MAX_ARENAS) return;
    if (vm.arenas[id].memory != NULL) return; // Already init

    vm.arenas[id].capacity = MAX_HEAP; // Default size
    vm.arenas[id].memory = (double*)malloc(MAX_HEAP * sizeof(double));
    vm.arenas[id].types = (int*)malloc(MAX_HEAP * sizeof(int));
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
    // 1. Free Core Arrays
    if (vm.bytecode) { free(vm.bytecode); vm.bytecode = NULL; }
    if (vm.lines) { free(vm.lines); vm.lines = NULL; }
    if (vm.stack) { free(vm.stack); vm.stack = NULL; }
    if (vm.stack_types) { free(vm.stack_types); vm.stack_types = NULL; }
    if (vm.globals) { free(vm.globals); vm.globals = NULL; }
    if (vm.global_types) { free(vm.global_types); vm.global_types = NULL; }
    if (vm.constants) { free(vm.constants); vm.constants = NULL; }

    // 2. Free Arenas
    for (int i = 0; i < MAX_ARENAS; i++) {
        free_arena(i);
    }

    // 3. Free String Pool
    if (vm.string_pool) { free(vm.string_pool); vm.string_pool = NULL; }

    // 4. Clean up Reference Store
    for (int i = 0; i < ref_next_id; i++) {
        if (ref_store[i].is_copy && ref_store[i].ptr) {
            free(ref_store[i].ptr);
        }
        ref_store[i].ptr = NULL;
    }
    ref_next_id = 0;

    vm.sp = -1;
    vm.ip = 0;
    vm.str_count = 0;
}

void vm_init() {
    if (vm.bytecode) {
        vm_cleanup();
    }
    // 1. Allocate arrays
    vm.bytecode = (int*)malloc(MAX_CODE * sizeof(int));
    vm.lines    = (int*)malloc(MAX_CODE * sizeof(int));
    vm.stack       = (double*)malloc(STACK_SIZE * sizeof(double));
    vm.stack_types = (int*)malloc(STACK_SIZE * sizeof(int));
    vm.globals      = (double*)malloc(MAX_GLOBALS * sizeof(double));
    vm.global_types = (int*)malloc(MAX_GLOBALS * sizeof(int));
    vm.constants = (double*)malloc(MAX_CONSTANTS * sizeof(double));
    vm.string_pool = malloc(MAX_STRINGS * MAX_STRING_LENGTH);

    // 2. Init Main Arena (0)
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
    memset(natives, 0, sizeof(natives));
}

// --- POINTER RESOLUTION ---

double* vm_resolve_ptr(double ptr_val) {
    int id = UNPACK_ARENA(ptr_val);
    int offset = UNPACK_OFFSET(ptr_val);

    if (id < 0 || id >= MAX_ARENAS || !vm.arenas[id].active) {
        RUNTIME_ERROR("Access violation: Region %d is not active or freed", id);
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
    if (id < 0 || id >= MAX_ARENAS || !vm.arenas[id].active) return NULL;
    return &vm.arenas[id].types[offset];
}

// Unified Allocator returning packed pointer
double heap_alloc(int size) {
    int id = vm.current_arena;
    if (!vm.arenas[id].active) init_arena(id);

    if (vm.arenas[id].head + size >= vm.arenas[id].capacity) {
        printf("Error: Heap Overflow in Region %d!\n", id);
        exit(1);
    }
    int offset = vm.arenas[id].head;
    vm.arenas[id].head += size;
    return PACK_PTR(id, offset);
}

void vm_register_function(VM* vm, const char* name, int addr) {
    if (vm->function_count >= MAX_VM_FUNCTIONS) {
        printf("VM Limit: Too many functions registered.\n");
        exit(1);
    }
    VMFunction* f = &vm->functions[vm->function_count++];
    strncpy(f->name, name, 63);
    f->name[63] = '\0';
    f->addr = addr;
}

int vm_find_function(VM* vm, const char* name) {
    for (int i = 0; i < vm->function_count; i++) {
        if (strcmp(vm->functions[i].name, name) == 0) {
            return vm->functions[i].addr;
        }
    }
    return -1;
}

#define CHECK_STACK(count) if (vm.sp < (count) - 1) RUNTIME_ERROR("Stack Underflow")
#define CHECK_OBJ(depth) if (vm.stack_types[vm.sp - (depth)] != T_OBJ) RUNTIME_ERROR("Expected Object/Array")

void vm_push(double val, int type) {
    if (vm.sp >= STACK_SIZE - 1) {
        printf("Error: Stack Overflow\n");
        exit(1);
    }
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
    if (vm.str_count >= MAX_STRINGS) {
        printf("Error: String Pool Overflow\n");
        exit(1);
    }
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

// --- PRINTER HELPERS ---

void print_raw(const char* str) {
    if (MyloConfig.debug_mode && MyloConfig.print_callback) {
        MyloConfig.print_callback(str);
    }
    else if (MyloConfig.print_to_memory) {
        vm.output_mem_pos += sprintf(vm.output_char_buffer + vm.output_mem_pos, "%s", str);
    }
    else {
        printf("%s", str);
    }
}

void print_recursive(double val, int type, int depth) {
    if (depth > 10) { print_raw("..."); return; }
    char buf[64];

    if (type == T_NUM) {
        if (val == (int)val) sprintf(buf, "%d", (int)val);
        else sprintf(buf, "%g", val);
        print_raw(buf);
    }
    else if (type == T_STR) {
        if (depth > 0) print_raw("\"");
        print_raw(vm.string_pool[(int)val]);
        if (depth > 0) print_raw("\"");
    }
    else if (type == T_OBJ) {
        double ptr_val = val;
        double* base = vm_resolve_ptr(ptr_val);
        int* types = vm_resolve_type(ptr_val);
        if (!base) return;

        int obj_type = (int)base[HEAP_OFFSET_TYPE];

        if (obj_type == TYPE_ARRAY) {
            print_raw("[");
            int len = (int)base[HEAP_OFFSET_LEN];
            for (int i = 0; i < len; i++) {
                if (i > 0) print_raw(", ");
                print_recursive(base[HEAP_HEADER_ARRAY + i], types[HEAP_HEADER_ARRAY + i], depth + 1);
            }
            print_raw("]");
        }
        else if (obj_type == TYPE_MAP) {
            print_raw("["); // Reverted to brackets to match tests
            int count = (int)base[HEAP_OFFSET_COUNT];
            double data_ptr_val = base[HEAP_OFFSET_DATA];

            double* data = vm_resolve_ptr(data_ptr_val);
            int* data_types = vm_resolve_type(data_ptr_val);

            if(data) {
                for (int i = 0; i < count; i++) {
                    if (i > 0) print_raw(", ");
                    double k = data[i * 2];
                    print_raw(vm.string_pool[(int)k]);
                    print_raw("="); // Matches tests
                    print_recursive(data[i * 2 + 1], data_types[i * 2 + 1], depth + 1);
                }
            }
            print_raw("]");
        }
        else if (obj_type == TYPE_BYTES) {
            int len = (int)base[HEAP_OFFSET_LEN];
            unsigned char* b = (unsigned char*)&base[HEAP_HEADER_ARRAY];
            print_raw("b\"");
            for (int i = 0; i < len; i++) {
                if (b[i] >= 32 && b[i] <= 126) sprintf(buf, "%c", b[i]);
                else sprintf(buf, "\\x%02X", b[i]);
                print_raw(buf);
            }
            print_raw("\"");
        }
        else if (obj_type <= TYPE_I16_ARRAY && obj_type >= TYPE_BOOL_ARRAY) {
            print_raw("[");
            int len = (int)base[HEAP_OFFSET_LEN];
            char* data = (char*)&base[HEAP_HEADER_ARRAY];
            for (int i = 0; i < len; i++) {
                if (i > 0) print_raw(", ");
                if (obj_type == TYPE_I32_ARRAY) sprintf(buf, "%d", ((int*)data)[i]);
                else if (obj_type == TYPE_F32_ARRAY) sprintf(buf, "%g", ((float*)data)[i]);
                else if (obj_type == TYPE_I16_ARRAY) sprintf(buf, "%d", ((short*)data)[i]);
                else if (obj_type == TYPE_I64_ARRAY) sprintf(buf, "%lld", ((long long*)data)[i]);
                else if (obj_type == TYPE_BOOL_ARRAY) { print_raw(((unsigned char*)data)[i] ? "true" : "false"); continue; }
                else sprintf(buf, "?");
                print_raw(buf);
            }
            print_raw("]");
        }
        else {
            int offset = UNPACK_OFFSET(ptr_val);
            sprintf(buf, "[Struct Ref:%d]", offset);
            print_raw(buf);
        }
    }
}

// --- VM EXECUTION ---

int vm_step(bool debug_trace) {
    if (vm.ip >= vm.code_size) return -1;

    if (debug_trace) {
        int op = vm.bytecode[vm.ip];
        if (op >= 0 && op <= OP_SET_CTX) {
            printf("[TRACE] IP:%04d Line:%d SP:%2d OP:%s\n", vm.ip, vm.lines[vm.ip], vm.sp, OP_NAMES[op]);
        }
    }

    int op = vm.bytecode[vm.ip++];

    switch (op) {
        case OP_NEW_ARENA: {
            int id = -1;
            for(int i=1; i<MAX_ARENAS; i++) { // 0 is main
                if(!vm.arenas[i].active) {
                    id = i;
                    break;
                }
            }
            if(id == -1) RUNTIME_ERROR("Max Regions Reached");
            init_arena(id);
            vm_push((double)id, T_NUM);
            break;
        }
        case OP_DEL_ARENA: {
            double val = vm_pop();
            int id = (int)val;
            if(id <= 0) RUNTIME_ERROR("Cannot clear main region or invalid region");
            free_arena(id);
            break;
        }
        case OP_SET_CTX: {
            double val = vm_pop();
            int id = (int)val;
            if (id < 0 || id >= MAX_ARENAS) RUNTIME_ERROR("Invalid Region ID");
            if (!vm.arenas[id].active && id != 0) RUNTIME_ERROR("Region %d is not active", id);
            vm.current_arena = id;
            break;
        }
        case OP_PSH_NUM: {
            int index = vm.bytecode[vm.ip++];
            vm_push(vm.constants[index], T_NUM);
            break;
        }
        case OP_PSH_STR: {
            int index = vm.bytecode[vm.ip++];
            vm_push((double) index, T_STR);
            break;
        }
        case OP_CAT: {
            CHECK_STACK(2);
            double b = vm_pop(); int bt = vm.stack_types[vm.sp + 1];
            double a = vm.stack[vm.sp]; int at = vm.stack_types[vm.sp];

            char s1[MAX_STRING_LENGTH];
            char s2[MAX_STRING_LENGTH];

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
        case OP_ADD: {
            CHECK_STACK(2);
            int typeB = vm.stack_types[vm.sp];
            double valB = vm_pop();
            int typeA = vm.stack_types[vm.sp];
            double valA = vm.stack[vm.sp];

            // 1. Number + Number
            if (typeA == T_NUM && typeB == T_NUM) {
                vm.stack[vm.sp] = valA + valB;
                vm.stack_types[vm.sp] = T_NUM;
            }
            // 2. String + String (Concatenation)
            else if (typeA == T_STR && typeB == T_STR) {
                const char* s1 = vm.string_pool[(int)valA];
                const char* s2 = vm.string_pool[(int)valB];
                char buf[MAX_STRING_LENGTH * 2];
                snprintf(buf, sizeof(buf), "%s%s", s1, s2);
                int id = make_string(buf);
                vm.stack[vm.sp] = (double)id;
                vm.stack_types[vm.sp] = T_STR;
            }
            // 3. Object + Object (Concatenation)
            else if (typeA == T_OBJ && typeB == T_OBJ) {
                double ptrA = valA;
                double ptrB = valB;

                double* baseA = vm_resolve_ptr(ptrA);
                double* baseB = vm_resolve_ptr(ptrB);
                int* typesA = vm_resolve_type(ptrA);
                int* typesB = vm_resolve_type(ptrB);

                int htA = (int)baseA[HEAP_OFFSET_TYPE];
                int htB = (int)baseB[HEAP_OFFSET_TYPE];

                // Handle Byte String Concatenation (b"A" + b"B")
                if (htA == TYPE_BYTES && htB == TYPE_BYTES) {
                    int lenA = (int)baseA[HEAP_OFFSET_LEN];
                    int lenB = (int)baseB[HEAP_OFFSET_LEN];
                    int newLen = lenA + lenB;

                    int d_needed = (newLen + 7) / 8;
                    double newPtr = heap_alloc(d_needed + HEAP_HEADER_ARRAY);
                    double* baseNew = vm_resolve_ptr(newPtr);

                    baseNew[HEAP_OFFSET_TYPE] = TYPE_BYTES;
                    baseNew[HEAP_OFFSET_LEN] = (double)newLen;

                    char* dst = (char*)&baseNew[HEAP_HEADER_ARRAY];
                    char* srcA = (char*)&baseA[HEAP_HEADER_ARRAY];
                    memcpy(dst, srcA, lenA);

                    char* srcB = (char*)&baseB[HEAP_HEADER_ARRAY];
                    memcpy(dst + lenA, srcB, lenB);

                    vm.stack[vm.sp] = newPtr;
                }
                // Standard Array Concatenation
                else if (htA == TYPE_ARRAY && htB == TYPE_ARRAY) {
                    int lenA = (int)baseA[HEAP_OFFSET_LEN];
                    int lenB = (int)baseB[HEAP_OFFSET_LEN];

                    double newPtr = heap_alloc(lenA + lenB + HEAP_HEADER_ARRAY);
                    double* baseNew = vm_resolve_ptr(newPtr);
                    int* typesNew = vm_resolve_type(newPtr);

                    baseNew[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
                    baseNew[HEAP_OFFSET_LEN] = (double)(lenA + lenB);

                    for(int i=0; i<lenA; i++) {
                        baseNew[HEAP_HEADER_ARRAY+i] = baseA[HEAP_HEADER_ARRAY+i];
                        typesNew[HEAP_HEADER_ARRAY+i] = typesA[HEAP_HEADER_ARRAY+i];
                    }
                    for(int i=0; i<lenB; i++) {
                        baseNew[HEAP_HEADER_ARRAY+lenA+i] = baseB[HEAP_HEADER_ARRAY+i];
                        typesNew[HEAP_HEADER_ARRAY+lenA+i] = typesB[HEAP_HEADER_ARRAY+i];
                    }
                    vm.stack[vm.sp] = newPtr;
                }
                else {
                    RUNTIME_ERROR("Cannot add these object types");
                }
            }
            // 4. Array + Scalar (Broadcast Addition)
            else if (typeA == T_OBJ || typeB == T_OBJ) {
                // One is array, one is scalar
                double arrVal = (typeA == T_OBJ) ? valA : valB;
                double scalVal = (typeA == T_OBJ) ? valB : valA;
                int scalType = (typeA == T_OBJ) ? typeB : typeA;

                double* base = vm_resolve_ptr(arrVal);
                int* types = vm_resolve_type(arrVal);
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
                    }
                    else if (hType == TYPE_I32_ARRAY) el = (double)((int*)&base[HEAP_HEADER_ARRAY])[i];
                    else if (hType == TYPE_F32_ARRAY) el = (double)((float*)&base[HEAP_HEADER_ARRAY])[i];
                    // ... others ...

                    if(scalType == T_NUM) {
                        if (elType == T_NUM) {
                            newBase[HEAP_HEADER_ARRAY+i] = el + scalVal;
                            newTypes[HEAP_HEADER_ARRAY+i] = T_NUM;
                        } else {
                            // Element is not a number, but we tried adding a number.
                            // Defaulting to 0 for safety as per previous behaviour logic inference
                            newBase[HEAP_HEADER_ARRAY+i] = 0;
                            newTypes[HEAP_HEADER_ARRAY+i] = T_NUM;
                        }
                    } else if (scalType == T_STR) {
                        // String broadcast concat
                        char valStr[128];
                        if (elType == T_STR) {
                            // Fix: Use the element value (ID) to look up string
                            strncpy(valStr, vm.string_pool[(int)el], 127);
                            valStr[127] = '\0';
                        } else {
                            snprintf(valStr, 128, "%g", el);
                        }

                        const char* sScal = vm.string_pool[(int)scalVal];
                        char buf[256];

                        if (typeA == T_OBJ) {
                            // Array + Scalar
                            snprintf(buf, 256, "%s%s", valStr, sScal);
                        } else {
                            // Scalar + Array
                            snprintf(buf, 256, "%s%s", sScal, valStr);
                        }
                        newBase[HEAP_HEADER_ARRAY+i] = (double)make_string(buf);
                        newTypes[HEAP_HEADER_ARRAY+i] = T_STR;
                    }
                }
                vm.stack[vm.sp] = newPtr;
                vm.stack_types[vm.sp] = T_OBJ;
            }
            else {
                RUNTIME_ERROR("Invalid types for ADD");
            }
            break;
        }
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD: {
            CHECK_STACK(2);
            double b = vm_pop();
            double a = vm.stack[vm.sp];
            if (vm.stack_types[vm.sp] == T_NUM && vm.stack_types[vm.sp+1] == T_NUM) {
                if (op == OP_SUB) vm.stack[vm.sp] = a - b;
                else if (op == OP_MUL) vm.stack[vm.sp] = a * b;
                else if (op == OP_DIV) vm.stack[vm.sp] = a / b;
                else if (op == OP_MOD) vm.stack[vm.sp] = fmod(a, b);
            }
            else if (vm.stack_types[vm.sp] == T_OBJ || vm.stack_types[vm.sp+1] == T_OBJ) {
                 double arrVal = (vm.stack_types[vm.sp] == T_OBJ) ? a : b;
                 double scalVal = (vm.stack_types[vm.sp] == T_OBJ) ? b : a;
                 double* base = vm_resolve_ptr(arrVal);
                 int len = (int)base[1];
                 double newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);
                 double* newBase = vm_resolve_ptr(newPtr);
                 int* newTypes = vm_resolve_type(newPtr);
                 newBase[0] = TYPE_ARRAY;
                 newBase[1] = (double)len;

                 for(int i=0; i<len; i++) {
                     double el = base[HEAP_HEADER_ARRAY+i];
                     double res = 0;
                     if(op == OP_MUL) res = el * scalVal;
                     else if (op == OP_SUB) res = el - scalVal;
                     else if (op == OP_DIV) res = el / scalVal;

                     newBase[HEAP_HEADER_ARRAY+i] = res;
                     newTypes[HEAP_HEADER_ARRAY+i] = T_NUM;
                 }
                 vm.stack[vm.sp] = newPtr;
                 vm.stack_types[vm.sp] = T_OBJ;
            }
            else {
                RUNTIME_ERROR("Math op requires numbers");
            }
            break;
        }
        case OP_LT: { CHECK_STACK(2); double b = vm_pop(); vm.stack[vm.sp] = (vm.stack[vm.sp] < b); break; }
        case OP_GT: { CHECK_STACK(2); double b = vm_pop(); vm.stack[vm.sp] = (vm.stack[vm.sp] > b); break; }
        case OP_LE: { CHECK_STACK(2); double b = vm_pop(); vm.stack[vm.sp] = (vm.stack[vm.sp] <= b); break; }
        case OP_GE: { CHECK_STACK(2); double b = vm_pop(); vm.stack[vm.sp] = (vm.stack[vm.sp] >= b); break; }
        case OP_EQ: { CHECK_STACK(2); double b = vm_pop(); vm.stack[vm.sp] = (vm.stack[vm.sp] == b); break; }
        case OP_NEQ: { CHECK_STACK(2); double b = vm_pop(); vm.stack[vm.sp] = (vm.stack[vm.sp] != b); break; }

        case OP_SET: {
            int addr = vm.bytecode[vm.ip++];
            CHECK_STACK(1);
            vm.globals[addr] = vm.stack[vm.sp];
            vm.global_types[addr] = vm.stack_types[vm.sp];
            vm.sp--;
            break;
        }
        case OP_GET: {
            int addr = vm.bytecode[vm.ip++];
            vm_push(vm.globals[addr], vm.global_types[addr]);
            break;
        }
        case OP_LVAR: {
            int off = vm.bytecode[vm.ip++];
            int fp = (int)vm.fp;
            vm_push(vm.stack[fp + off], vm.stack_types[fp + off]);
            break;
        }
        case OP_SVAR: {
            int off = vm.bytecode[vm.ip++];
            int fp = (int)vm.fp;
            CHECK_STACK(1);
            vm.stack[fp + off] = vm.stack[vm.sp];
            vm.stack_types[fp + off] = vm.stack_types[vm.sp];
            vm.sp--;
            break;
        }
        case OP_JMP: vm.ip = vm.bytecode[vm.ip]; break;
        case OP_JZ: {
            int t = vm.bytecode[vm.ip++];
            if (vm_pop() == 0.0) vm.ip = t;
            break;
        }
        case OP_JNZ: {
            int t = vm.bytecode[vm.ip++];
            if (vm_pop() != 0.0) vm.ip = t;
            break;
        }
        case OP_CALL: {
            int t = vm.bytecode[vm.ip++];
            int n = vm.bytecode[vm.ip++];
            CHECK_STACK(n);
            int as = vm.sp - n + 1;
            for(int i=0; i<n; i++) {
                vm.stack[vm.sp + 2 - i] = vm.stack[vm.sp - i];
                vm.stack_types[vm.sp + 2 - i] = vm.stack_types[vm.sp - i];
            }
            vm.stack[as] = (double)vm.ip;
            vm.stack[as+1] = (double)vm.fp;
            vm.stack_types[as] = T_NUM;
            vm.stack_types[as+1] = T_NUM;
            vm.sp += 2;
            vm.fp = as + 2;
            vm.ip = t;
            break;
        }
        case OP_RET: {
            CHECK_STACK(1);
            double rv = vm.stack[vm.sp];
            int rt = vm.stack_types[vm.sp];
            vm.sp = vm.fp - 3; // Pop locals, FP, IP
            int fp = (int)vm.fp;
            vm.fp = (int)vm.stack[fp-1];
            vm.ip = (int)vm.stack[fp-2];
            vm_push(rv, rt);
            break;
        }
        case OP_PRN: {
            CHECK_STACK(1);
            double v = vm.stack[vm.sp];
            int t = vm.stack_types[vm.sp];
            vm.sp--;
            print_recursive(v, t, 0);
            print_raw("\n");
            break;
        }
        case OP_ALLOC: {
            int s = vm.bytecode[vm.ip++];
            int id = vm.bytecode[vm.ip++];
            double ptr = heap_alloc(s + 1);
            double* base = vm_resolve_ptr(ptr);
            base[0] = (double)id;
            vm_push(ptr, T_OBJ);
            break;
        }
        case OP_DUP: {
            CHECK_STACK(1);
            vm_push(vm.stack[vm.sp], vm.stack_types[vm.sp]);
            break;
        }
        case OP_HSET: {
            int off = vm.bytecode[vm.ip++];
            int eid = vm.bytecode[vm.ip++];
            CHECK_STACK(2);
            double v = vm_pop(); int t = vm.stack_types[vm.sp+1];
            double p = vm.stack[vm.sp];
            double* base = vm_resolve_ptr(p);
            int* types = vm_resolve_type(p);

            if((int)base[0] != eid) RUNTIME_ERROR("HSET Type mismatch");
            base[1+off] = v;
            types[1+off] = t;
            break;
        }
        case OP_HGET: {
            int off = vm.bytecode[vm.ip++];
            int eid = vm.bytecode[vm.ip++];
            CHECK_STACK(1);
            double p = vm.stack[vm.sp];
            double* base = vm_resolve_ptr(p);
            int* types = vm_resolve_type(p);

            if((int)base[0] != eid) RUNTIME_ERROR("HGET Type mismatch");
            vm.stack[vm.sp] = base[1+off];
            vm.stack_types[vm.sp] = types[1+off];
            break;
        }
        case OP_POP: vm_pop(); break;
        case OP_NATIVE: {
            int id = vm.bytecode[vm.ip++];
            if(natives[id]) natives[id](&vm);
            else RUNTIME_ERROR("Unknown Native ID %d", id);
            break;
        }
        case OP_ARR: {
            int c = vm.bytecode[vm.ip++];
            double ptr = heap_alloc(c+2);
            double* base = vm_resolve_ptr(ptr);
            int* types = vm_resolve_type(ptr);

            base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
            base[HEAP_OFFSET_LEN] = (double)c;
            for(int i=c; i>0; i--) {
                base[HEAP_HEADER_ARRAY+i-1] = vm.stack[vm.sp];
                types[HEAP_HEADER_ARRAY+i-1] = vm.stack_types[vm.sp];
                vm.sp--;
            }
            vm_push(ptr, T_OBJ);
            break;
        }
        case OP_AGET: {
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
            }
            else if (type == TYPE_MAP) {
                int count = (int)base[HEAP_OFFSET_COUNT];
                double data_ptr_val = base[HEAP_OFFSET_DATA];
                double* data = vm_resolve_ptr(data_ptr_val);
                int* data_types = vm_resolve_type(data_ptr_val);

                bool found = false;
                for(int i=0; i<count; i++) {
                    if(data[i*2] == key) {
                        vm_push(data[i*2 + 1], data_types[i*2 + 1]);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    int empty = make_string("");
                    vm_push((double)empty, T_STR);
                }
            }
            break;
        }
        case OP_ALEN: {
            CHECK_STACK(1);
            double ptr = vm_pop();
            double* base = vm_resolve_ptr(ptr);
            int type = (int)base[HEAP_OFFSET_TYPE];
            if (type == TYPE_MAP) vm_push(base[HEAP_OFFSET_COUNT], T_NUM);
            else vm_push(base[HEAP_OFFSET_LEN], T_NUM);
            break;
        }
        case OP_SLICE: {
            CHECK_STACK(3);
            double e = vm_pop();
            double s = vm_pop();
            double ptr = vm_pop();
            double* base = vm_resolve_ptr(ptr);
            int* types = vm_resolve_type(ptr);

            int type = (int)base[HEAP_OFFSET_TYPE];
            int len = (int)base[HEAP_OFFSET_LEN];

            int start = (int)s;
            int end = (int)e;
            if (start < 0) start += len;
            if (end < 0) end += len;
            if (start < 0) start = 0;
            if (end >= len) end = len - 1;

            int newlen = (end >= start) ? (end - start + 1) : 0;
            if (start >= len) newlen = 0;

            if (type == TYPE_BYTES) {
                int d_needed = (newlen + 7) / 8;
                double newptr = heap_alloc(2 + d_needed);
                double* newbase = vm_resolve_ptr(newptr);
                newbase[0] = TYPE_BYTES;
                newbase[1] = (double)newlen;
                char* src = (char*)&base[HEAP_HEADER_ARRAY];
                char* dst = (char*)&newbase[HEAP_HEADER_ARRAY];
                if (newlen > 0) {
                    for (int i = 0; i < newlen; i++) dst[i] = src[start + i];
                }
                vm_push(newptr, T_OBJ);
            } else {
                double newptr = heap_alloc(newlen + 2);
                double* newbase = vm_resolve_ptr(newptr);
                int* newtypes = vm_resolve_type(newptr);
                newbase[0] = TYPE_ARRAY;
                newbase[1] = (double)newlen;
                for (int i = 0; i < newlen; i++) {
                    newbase[2 + i] = base[2 + start + i];
                    newtypes[2 + i] = types[2 + start + i];
                }
                vm_push(newptr, T_OBJ);
            }
            break;
        }
        case OP_MAP: {
            int cap = MAP_INITIAL_CAP;
            double map = heap_alloc(4);
            double data = heap_alloc(cap*2);
            double* base = vm_resolve_ptr(map);
            base[0] = TYPE_MAP;
            base[1] = (double)cap;
            base[2] = 0;
            base[3] = data;
            vm_push(map, T_OBJ);
            break;
        }
        case OP_ASET: {
            CHECK_STACK(3);
            double val = vm_pop(); int vt = vm.stack_types[vm.sp+1];
            double key = vm_pop();
            double ptr = vm.stack[vm.sp]; vm_pop();

            double* base = vm_resolve_ptr(ptr);
            int* types = vm_resolve_type(ptr);
            int type = (int)base[0];

            if (type == TYPE_ARRAY) {
                int idx = (int)key;
                base[2+idx] = val;
                types[2+idx] = vt;
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
                        data[i*2+1] = val;
                        data_types[i*2+1] = vt;
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
                            new_data[i] = data[i];
                            new_types[i] = data_types[i];
                        }
                        base[1] = (double)new_cap;
                        base[3] = new_data_ptr;
                        data = new_data;
                        data_types = new_types;
                    }
                    data[count*2] = key;
                    data_types[count*2] = T_STR;
                    data[count*2+1] = val;
                    data_types[count*2+1] = vt;
                    base[2] = (double)(count+1);
                }
            }
            vm_push(val, vt);
            break;
        }
        case OP_MK_BYTES: {
            CHECK_STACK(1);
            double v = vm_pop();
            const char* s = vm.string_pool[(int)v];
            int len = strlen(s);
            int d_needed = (len+7)/8;
            double ptr = heap_alloc(2+d_needed);
            double* base = vm_resolve_ptr(ptr);
            base[0] = TYPE_BYTES;
            base[1] = (double)len;
            char* b = (char*)&base[2];
            memcpy(b, s, len);
            vm_push(ptr, T_OBJ);
            break;
        }
        case OP_SLICE_SET: {
            CHECK_STACK(4);
            double val = vm_pop();
            double e = vm_pop();
            double s = vm_pop();
            double ptr = vm.stack[vm.sp]; vm_pop(); // Pop Ptr

            double* base = vm_resolve_ptr(ptr);
            int* types = vm_resolve_type(ptr);
            int type = (int)base[0];
            int len = (int)base[1];

            int start = (int)s;
            int end = (int)e;
            if (start < 0) start += len;
            if (end < 0) end += len;
            if (start < 0) start = 0;
            if (end >= len) end = len - 1;
            int slice_len = (end >= start) ? (end - start + 1) : 0;

            if (type == TYPE_BYTES) {
                // val should be a Byte String (TYPE_BYTES)
                double* vbase = vm_resolve_ptr(val);
                if ((int)vbase[0] == TYPE_BYTES) {
                    unsigned char* dst = (unsigned char*)&base[HEAP_HEADER_ARRAY];
                    unsigned char* src = (unsigned char*)&vbase[HEAP_HEADER_ARRAY];
                    int vlen = (int)vbase[1];
                    int copy_len = (slice_len < vlen) ? slice_len : vlen;
                    for(int i=0; i<copy_len; i++) dst[start+i] = src[i];
                }
            } else if (type == TYPE_ARRAY) {
                // val should be an Array
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
            vm_push(val, T_OBJ); // Assignment returns value
            break;
        }
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

                if (op == OP_IT_KEY) {
                    vm_push((double)idx, T_NUM);
                } else {
                    if (type == TYPE_BYTES) {
                         unsigned char* b = (unsigned char*)&base[HEAP_HEADER_ARRAY];
                         vm_push((double)b[idx], T_NUM);
                    } else {
                         vm_push(base[2+idx], types[2+idx]);
                    }
                }
            }
            else if (type == TYPE_MAP) {
                int count = (int)base[HEAP_OFFSET_COUNT];
                if (idx < 0 || idx >= count) RUNTIME_ERROR("Iterator OOB");
                double data_ptr = base[HEAP_OFFSET_DATA];
                double* data = vm_resolve_ptr(data_ptr);
                int* data_types = vm_resolve_type(data_ptr);

                if (op == OP_IT_VAL) {
                    vm_push(data[idx*2+1], data_types[idx*2+1]);
                } else {
                    vm_push(data[idx*2], data_types[idx*2]);
                }
            }
            break;
        }
        case OP_EMBED: {
            int len = vm.bytecode[vm.ip++];
            int d_needed = (len + 7) / 8;
            double ptr = heap_alloc(d_needed + HEAP_HEADER_ARRAY);
            double* base = vm_resolve_ptr(ptr);

            base[0] = TYPE_BYTES;
            base[1] = (double)len;

            unsigned char* dst = (unsigned char*)&base[HEAP_HEADER_ARRAY];
            for(int i=0; i<len; i++) {
                int b = vm.bytecode[vm.ip++];
                dst[i] = (unsigned char)b;
            }
            vm_push(ptr, T_OBJ);
            break;
        }
        case OP_MAKE_ARR: {
            int count = vm.bytecode[vm.ip++];
            int type_id = vm.bytecode[vm.ip++];
            int elem_size = get_type_size(type_id);
            int total_bytes = count * elem_size;
            int doubles_needed = (total_bytes + 7) / 8;

            double ptr = heap_alloc(doubles_needed + HEAP_HEADER_ARRAY);
            double* base = vm_resolve_ptr(ptr);
            base[0] = (double)type_id;
            base[1] = (double)count;

            char* data_ptr = (char*)&base[HEAP_HEADER_ARRAY];

            for(int i = count - 1; i >= 0; i--) {
                double val = vm.stack[vm.sp];
                vm.sp--;
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
            break;
        }
        case OP_HLT: return -1;
    }
    return op;
}

// Executes VM starting from a specific instruction
void run_vm_from(int start_ip, bool debug_trace) {
    vm.ip = start_ip;
    while (vm.ip < vm.code_size) {
        if (vm_step(debug_trace) == -1) break; // Stop on OP_HLT
    }
}

// Update existing run_vm to use the new helper
void run_vm(bool debug_trace) {
    run_vm_from(0, debug_trace);
}