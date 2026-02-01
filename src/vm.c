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
    "MAKE_ARR" // Added missing name
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


// --- VM CORE ---

void vm_init() {

    // 1. Allocate arrays if they haven't been allocated yet
    // This prevents re-allocating (and leaking) when mylo_reset() calls vm_init()
    if (!vm.bytecode) vm.bytecode = (int*)malloc(MAX_CODE * sizeof(int));
    if (!vm.lines)    vm.lines    = (int*)malloc(MAX_CODE * sizeof(int));

    if (!vm.stack)       vm.stack       = (double*)malloc(STACK_SIZE * sizeof(double));
    if (!vm.stack_types) vm.stack_types = (int*)malloc(STACK_SIZE * sizeof(int));

    if (!vm.globals)      vm.globals      = (double*)malloc(MAX_GLOBALS * sizeof(double));
    if (!vm.global_types) vm.global_types = (int*)malloc(MAX_GLOBALS * sizeof(int));

    if (!vm.constants) vm.constants = (double*)malloc(MAX_CONSTANTS * sizeof(double));

    if (!vm.heap)       vm.heap       = (double*)malloc(MAX_HEAP * sizeof(double));
    if (!vm.heap_types) vm.heap_types = (int*)malloc(MAX_HEAP * sizeof(int));

    if (!vm.string_pool) vm.string_pool = malloc(MAX_STRINGS * MAX_STRING_LENGTH);

    // Verify allocation
    if (!vm.bytecode || !vm.heap || !vm.stack) {
        fprintf(stderr, "Critical Error: Failed to allocate VM memory (MAX_CODE=%d)\n", MAX_CODE);
        exit(1);
    }

    // 2. Reset State (Manual reset since we can't memset the whole struct)
    vm.sp = -1;
    vm.ip = 0;
    vm.fp = 0;
    vm.code_size = 0;
    vm.str_count = 0;
    vm.const_count = 0;
    vm.heap_ptr = 0;
    vm.function_count = 0;
    vm.output_mem_pos = 0;

    // Clear Output Buffer
    vm.output_char_buffer[0] = '\0';

    // Clear Natives Table
    memset(natives, 0, sizeof(natives));

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

int heap_alloc(int size) {
    if (vm.heap_ptr + size >= MAX_HEAP) {
        printf("Error: Heap Overflow!\n");
        exit(1);
    }
    int addr = vm.heap_ptr;
    vm.heap_ptr += size;
    return addr;
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
    // Recursion Guard
    if (depth > 10) { print_raw("..."); return; }

    char buf[64];

    if (type == T_NUM) {
        // Integer check for cleaner output (1 instead of 1.0)
        if (val == (int)val) sprintf(buf, "%d", (int)val);
        else sprintf(buf, "%g", val);
        print_raw(buf);
    }
    else if (type == T_STR) {
        // Quote strings if they are inside a collection
        if (depth > 0) print_raw("\"");
        print_raw(vm.string_pool[(int)val]);
        if (depth > 0) print_raw("\"");
    }
    else if (type == T_OBJ) {
        int ptr = (int)val;
        int obj_type = (int)vm.heap[ptr];

        if (obj_type == TYPE_ARRAY) {
            print_raw("[");
            int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
            for (int i = 0; i < len; i++) {
                if (i > 0) print_raw(", ");
                // Recurse
                print_recursive(vm.heap[ptr + 2 + i], vm.heap_types[ptr + 2 + i], depth + 1);
            }
            print_raw("]");
        }
        else if (obj_type == TYPE_MAP) {
            print_raw("[");
            int count = (int)vm.heap[ptr + HEAP_OFFSET_COUNT];
            int data = (int)vm.heap[ptr + HEAP_OFFSET_DATA];

            for (int i = 0; i < count; i++) {
                if (i > 0) print_raw(", ");

                // Key (Assumed String for now)
                double k = vm.heap[data + i * 2];
                print_raw(vm.string_pool[(int)k]);
                print_raw("=");

                // Value (Recurse)
                print_recursive(vm.heap[data + i * 2 + 1], vm.heap_types[data + i * 2 + 1], depth + 1);
            }
            print_raw("]");
        }
        else if (obj_type == TYPE_BYTES) {
            int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
            unsigned char* b = (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];
            print_raw("b\"");
            for (int i = 0; i < len; i++) {
                // Print basic ASCII, hex for others
                if (b[i] >= 32 && b[i] <= 126) sprintf(buf, "%c", b[i]);
                else sprintf(buf, "\\x%02X", b[i]);
                print_raw(buf);
            }
            print_raw("\"");
        }
        else {
            // Structs (No length/name metadata available at runtime yet)
            sprintf(buf, "[Struct Ref:%d]", ptr);
            print_raw(buf);
        }
    }
}

// --- SINGLE STEP EXECUTION ---
// Returns the opcode executed, or OP_HLT (-1) if finished
int vm_step(bool debug_trace) {
    if (vm.ip >= vm.code_size) return -1;

    if (debug_trace) {
        int op = vm.bytecode[vm.ip];
        if (op >= 0 && op <= OP_MAKE_ARR) {
            printf("[TRACE] IP:%04d Line:%d SP:%2d OP:%s\n", vm.ip, vm.lines[vm.ip], vm.sp, OP_NAMES[op]);
        }
    }

    int op = vm.bytecode[vm.ip++];

    switch (op) {
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
                int ptrA = (int)valA;
                int ptrB = (int)valB;
                int htA = (int)vm.heap[ptrA];
                int htB = (int)vm.heap[ptrB];

                // Handle Byte String Concatenation (b"A" + b"B")
                if (htA == TYPE_BYTES && htB == TYPE_BYTES) {
                    int lenA = (int)vm.heap[ptrA + HEAP_OFFSET_LEN];
                    int lenB = (int)vm.heap[ptrB + HEAP_OFFSET_LEN];
                    int newLen = lenA + lenB;

                    int d_needed = (newLen + 7) / 8;
                    int newPtr = heap_alloc(d_needed + HEAP_HEADER_ARRAY);

                    vm.heap[newPtr] = TYPE_BYTES;
                    vm.heap[newPtr + HEAP_OFFSET_LEN] = (double)newLen;

                    char* dst = (char*)&vm.heap[newPtr + HEAP_HEADER_ARRAY];
                    char* srcA = (char*)&vm.heap[ptrA + HEAP_HEADER_ARRAY];
                    memcpy(dst, srcA, lenA);

                    char* srcB = (char*)&vm.heap[ptrB + HEAP_HEADER_ARRAY];
                    memcpy(dst + lenA, srcB, lenB);

                    vm.stack[vm.sp] = (double)newPtr;
                }
                // Standard Array Concatenation
                else if (htA == TYPE_ARRAY && htB == TYPE_ARRAY) {
                    int lenA = (int)vm.heap[ptrA + HEAP_OFFSET_LEN];
                    int lenB = (int)vm.heap[ptrB + HEAP_OFFSET_LEN];
                    int newPtr = heap_alloc(lenA + lenB + HEAP_HEADER_ARRAY);
                    vm.heap[newPtr] = TYPE_ARRAY;
                    vm.heap[newPtr + 1] = (double)(lenA + lenB);

                    for(int i=0; i<lenA; i++) {
                        vm.heap[newPtr+2+i] = vm.heap[ptrA+2+i];
                        vm.heap_types[newPtr+2+i] = vm.heap_types[ptrA+2+i];
                    }
                    for(int i=0; i<lenB; i++) {
                        vm.heap[newPtr+2+lenA+i] = vm.heap[ptrB+2+i];
                        vm.heap_types[newPtr+2+lenA+i] = vm.heap_types[ptrB+2+i];
                    }
                    vm.stack[vm.sp] = (double)newPtr;
                }
                else {
                    RUNTIME_ERROR("Cannot add these object types");
                }
            }
            // 4. Array + Scalar (Broadcast Addition)
            else if (typeA == T_OBJ || typeB == T_OBJ) {
                double arrVal = (typeA == T_OBJ) ? valA : valB;
                double scalVal = (typeA == T_OBJ) ? valB : valA;
                int scalType = (typeA == T_OBJ) ? typeB : typeA;

                int ptr = (int)arrVal;
                int hType = (int)vm.heap[ptr];

                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                int newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);

                // Result is always a generic array (TYPE_ARRAY) for simplicity
                vm.heap[newPtr] = TYPE_ARRAY;
                vm.heap[newPtr + 1] = (double)len;

                unsigned char* byteData = (hType == TYPE_BYTES) ? (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY] : NULL;

                for (int i = 0; i < len; i++) {
                    double elVal;
                    int elType;

                    if (hType == TYPE_BYTES) {
                        elVal = (double)byteData[i];
                        elType = T_NUM;
                    } else if (hType == TYPE_ARRAY) {
                        elVal = vm.heap[ptr + 2 + i];
                        elType = vm.heap_types[ptr + 2 + i];
                    } else {
                        // Handle packed types (Simple fallback to get double from packed)
                         char* data = (char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];
                         if(hType == TYPE_I32_ARRAY) elVal = (double)((int*)data)[i];
                         else if(hType == TYPE_F32_ARRAY) elVal = (double)((float*)data)[i];
                         else if(hType == TYPE_I16_ARRAY) elVal = (double)((short*)data)[i];
                         else elVal = 0;
                         elType = T_NUM;
                    }

                    if (elType == T_NUM && scalType == T_NUM) {
                        vm.heap[newPtr + 2 + i] = elVal + scalVal;
                        vm.heap_types[newPtr + 2 + i] = T_NUM;
                    }
                    else if (scalType == T_STR) {
                         char buf[1024];
                         if (elType == T_NUM) sprintf(buf, "%g", elVal);
                         else strcpy(buf, vm.string_pool[(int)elVal]);
                         const char* sScal = vm.string_pool[(int)scalVal];

                         if (typeA == T_OBJ) strcat(buf, sScal);
                         else { char temp[1024]; strcpy(temp, sScal); strcat(temp, buf); strcpy(buf, temp); }

                         vm.heap[newPtr + 2 + i] = (double)make_string(buf);
                         vm.heap_types[newPtr + 2 + i] = T_STR;
                    }
                    else {
                        RUNTIME_ERROR("Invalid types for array broadcast");
                    }
                }
                vm.stack[vm.sp] = (double)newPtr;
                vm.stack_types[vm.sp] = T_OBJ;
            }
            else {
                RUNTIME_ERROR("Invalid types for ADD");
            }
            break;
        }
        case OP_SUB: {
            CHECK_STACK(2);
            int typeB = vm.stack_types[vm.sp]; double valB = vm_pop();
            int typeA = vm.stack_types[vm.sp]; double valA = vm.stack[vm.sp];

            if (typeA == T_NUM && typeB == T_NUM) {
                vm.stack[vm.sp] = valA - valB;
                vm.stack_types[vm.sp] = T_NUM;
            }
            else if (typeA == T_OBJ || typeB == T_OBJ) {
                int ptr = (typeA == T_OBJ) ? (int)valA : (int)valB;
                double scalar = (typeA == T_OBJ) ? valB : valA;

                int hType = (int)vm.heap[ptr];
                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                int newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);
                vm.heap[newPtr] = TYPE_ARRAY;
                vm.heap[newPtr + 1] = (double)len;

                unsigned char* byteData = (hType == TYPE_BYTES) ? (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY] : NULL;

                for (int i = 0; i < len; i++) {
                    double elVal;
                    if (hType == TYPE_BYTES) elVal = (double)byteData[i];
                    else if (hType == TYPE_ARRAY) {
                        elVal = vm.heap[ptr + 2 + i];
                        if (vm.heap_types[ptr + 2 + i] != T_NUM) RUNTIME_ERROR("Non-number element in SUB");
                    } else {
                         // Typed Array Fallback
                         char* data = (char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];
                         if(hType == TYPE_I32_ARRAY) elVal = (double)((int*)data)[i];
                         else if(hType == TYPE_F32_ARRAY) elVal = (double)((float*)data)[i];
                         else elVal = 0;
                    }

                    if (typeA == T_OBJ) vm.heap[newPtr + 2 + i] = elVal - scalar;
                    else vm.heap[newPtr + 2 + i] = scalar - elVal;

                    vm.heap_types[newPtr + 2 + i] = T_NUM;
                }
                vm.stack[vm.sp] = (double)newPtr;
                vm.stack_types[vm.sp] = T_OBJ;
            }
            else RUNTIME_ERROR("Invalid types for SUB");
            break;
        }

        case OP_MUL: {
            CHECK_STACK(2);
            int typeB = vm.stack_types[vm.sp]; double valB = vm_pop();
            int typeA = vm.stack_types[vm.sp]; double valA = vm.stack[vm.sp];

            if (typeA == T_NUM && typeB == T_NUM) {
                vm.stack[vm.sp] *= valB;
            }
            else if ((typeA == T_OBJ && typeB == T_NUM) || (typeA == T_NUM && typeB == T_OBJ)) {
                double arrVal = (typeA == T_OBJ) ? valA : valB;
                double scalVal = (typeA == T_OBJ) ? valB : valA;

                int ptr = (int)arrVal;
                int hType = (int)vm.heap[ptr];
                // Removed strict check here to allow typed arrays

                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                int newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);
                vm.heap[newPtr] = TYPE_ARRAY;
                vm.heap[newPtr + 1] = (double)len;

                unsigned char* byteData = (hType == TYPE_BYTES) ? (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY] : NULL;

                for(int i = 0; i < len; i++) {
                     double elVal;
                     if (hType == TYPE_BYTES) elVal = (double)byteData[i];
                     else if (hType == TYPE_ARRAY) {
                         elVal = vm.heap[ptr + 2 + i];
                         if (vm.heap_types[ptr + 2 + i] != T_NUM) RUNTIME_ERROR("Non-number element in MUL");
                     } else {
                         char* data = (char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];
                         if(hType == TYPE_I32_ARRAY) elVal = (double)((int*)data)[i];
                         else if(hType == TYPE_F32_ARRAY) elVal = (double)((float*)data)[i];
                         else elVal = 0;
                     }

                     vm.heap[newPtr + 2 + i] = elVal * scalVal;
                     vm.heap_types[newPtr + 2 + i] = T_NUM;
                }
                vm.stack[vm.sp] = (double)newPtr;
                vm.stack_types[vm.sp] = T_OBJ;
            }
            else RUNTIME_ERROR("Invalid types for MUL");
            break;
        }

        case OP_DIV: {
            CHECK_STACK(2);
            int typeB = vm.stack_types[vm.sp]; double valB = vm_pop();
            int typeA = vm.stack_types[vm.sp]; double valA = vm.stack[vm.sp];

            if (typeA == T_NUM && typeB == T_NUM) {
                if (valB == 0) RUNTIME_ERROR("Div by zero");
                vm.stack[vm.sp] = valA / valB;
                vm.stack_types[vm.sp] = T_NUM;
            }
            else if (typeA == T_OBJ || typeB == T_OBJ) {
                int ptr = (typeA == T_OBJ) ? (int)valA : (int)valB;
                double scalar = (typeA == T_OBJ) ? valB : valA;

                int hType = (int)vm.heap[ptr];

                if (typeA == T_OBJ && scalar == 0) RUNTIME_ERROR("Div by zero");

                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                int newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);
                vm.heap[newPtr] = TYPE_ARRAY;
                vm.heap[newPtr + 1] = (double)len;

                unsigned char* byteData = (hType == TYPE_BYTES) ? (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY] : NULL;

                for (int i = 0; i < len; i++) {
                    double elVal;
                    if (hType == TYPE_BYTES) elVal = (double)byteData[i];
                    else if (hType == TYPE_ARRAY) {
                        elVal = vm.heap[ptr + 2 + i];
                        if (vm.heap_types[ptr + 2 + i] != T_NUM) RUNTIME_ERROR("Non-number element in DIV");
                    } else {
                         char* data = (char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];
                         if(hType == TYPE_I32_ARRAY) elVal = (double)((int*)data)[i];
                         else if(hType == TYPE_F32_ARRAY) elVal = (double)((float*)data)[i];
                         else elVal = 0;
                    }

                    if (typeA == T_OBJ) vm.heap[newPtr + 2 + i] = elVal / scalar;
                    else {
                        if (elVal == 0) RUNTIME_ERROR("Div by zero in array");
                        vm.heap[newPtr + 2 + i] = scalar / elVal;
                    }
                    vm.heap_types[newPtr + 2 + i] = T_NUM;
                }
                vm.stack[vm.sp] = (double)newPtr;
                vm.stack_types[vm.sp] = T_OBJ;
            }
            else RUNTIME_ERROR("Invalid types for DIV");
            break;
        }

        case OP_MOD: {
            CHECK_STACK(2);
            int typeB = vm.stack_types[vm.sp]; double valB = vm_pop();
            int typeA = vm.stack_types[vm.sp]; double valA = vm.stack[vm.sp];

            if (typeA == T_NUM && typeB == T_NUM) {
                if (valB == 0) RUNTIME_ERROR("Mod by zero");
                vm.stack[vm.sp] = fmod(valA, valB);
                vm.stack_types[vm.sp] = T_NUM;
            }
            else if (typeA == T_OBJ || typeB == T_OBJ) {
                int ptr = (typeA == T_OBJ) ? (int)valA : (int)valB;
                double scalar = (typeA == T_OBJ) ? valB : valA;

                int hType = (int)vm.heap[ptr];

                if (typeA == T_OBJ && scalar == 0) RUNTIME_ERROR("Mod by zero");

                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                int newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);
                vm.heap[newPtr] = TYPE_ARRAY;
                vm.heap[newPtr + 1] = (double)len;

                unsigned char* byteData = (hType == TYPE_BYTES) ? (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY] : NULL;

                for (int i = 0; i < len; i++) {
                    double elVal;
                    if (hType == TYPE_BYTES) elVal = (double)byteData[i];
                    else if (hType == TYPE_ARRAY) {
                        elVal = vm.heap[ptr + 2 + i];
                        if (vm.heap_types[ptr + 2 + i] != T_NUM) RUNTIME_ERROR("Non-number element in MOD");
                    } else {
                         char* data = (char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];
                         if(hType == TYPE_I32_ARRAY) elVal = (double)((int*)data)[i];
                         else if(hType == TYPE_F32_ARRAY) elVal = (double)((float*)data)[i];
                         else elVal = 0;
                    }

                    if (typeA == T_OBJ) vm.heap[newPtr + 2 + i] = fmod(elVal, scalar);
                    else {
                        if (elVal == 0) RUNTIME_ERROR("Mod by zero in array");
                        vm.heap[newPtr + 2 + i] = fmod(scalar, elVal);
                    }
                    vm.heap_types[newPtr + 2 + i] = T_NUM;
                }
                vm.stack[vm.sp] = (double)newPtr;
                vm.stack_types[vm.sp] = T_OBJ;
            }
            else RUNTIME_ERROR("Invalid types for MOD");
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

            // Use the recursive printer
            print_recursive(v, t, 0);

            // Print Newline
            print_raw("\n");
            break;
        }
        case OP_CAT: {
            CHECK_STACK(2);
            double b = vm_pop(); int bt = vm.stack_types[vm.sp + 1];
            double a = vm.stack[vm.sp]; int at = vm.stack_types[vm.sp];
            char s1[512], s2[512];

            if (at == T_STR) strcpy(s1, vm.string_pool[(int)a]);
            else sprintf(s1, "%g", a);

            if (bt == T_STR) strcpy(s2, vm.string_pool[(int)b]);
            else sprintf(s2, "%g", b);

            char res[1024];
            sprintf(res, "%s%s", s1, s2);
            vm.stack[vm.sp] = (double)make_string(res);
            vm.stack_types[vm.sp] = T_STR;
            break;
        }
        case OP_ALLOC: {
            int s = vm.bytecode[vm.ip++];
            int id = vm.bytecode[vm.ip++];
            int ptr = heap_alloc(s + 1);
            vm.heap[ptr] = (double)id;
            vm_push((double)ptr, T_OBJ);
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
            int p = (int)vm.stack[vm.sp];
            if((int)vm.heap[p] != eid) RUNTIME_ERROR("HSET Type mismatch");
            vm.heap[p+1+off] = v;
            vm.heap_types[p+1+off] = t;
            break;
        }
        case OP_HGET: {
            int off = vm.bytecode[vm.ip++];
            int eid = vm.bytecode[vm.ip++];
            CHECK_STACK(1);
            int p = (int)vm.stack[vm.sp];
            if((int)vm.heap[p] != eid) RUNTIME_ERROR("HGET Type mismatch");
            vm.stack[vm.sp] = vm.heap[p+1+off];
            vm.stack_types[vm.sp] = vm.heap_types[p+1+off];
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
            int ptr = heap_alloc(c+2);
            vm.heap[ptr] = TYPE_ARRAY;
            vm.heap[ptr+1] = (double)c;
            for(int i=c; i>0; i--) {
                vm.heap[ptr+1+i] = vm.stack[vm.sp];
                vm.heap_types[ptr+1+i] = vm.stack_types[vm.sp];
                vm.sp--;
            }
            vm_push((double)ptr, T_OBJ);
            break;
        }
        case OP_AGET: {
            CHECK_STACK(2);
            double key = vm_pop(); int kt = vm.stack_types[vm.sp+1];
            int ptr = (int)vm_pop();
            int type = (int)vm.heap[ptr];

            if (type <= TYPE_ARRAY && type != TYPE_MAP) { // It's an array/buffer
                int idx = (int)key;
                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                if (idx < 0) idx += len;
                if (idx < 0 || idx >= len) RUNTIME_ERROR("Index OOB");

                if (type == TYPE_ARRAY) {
                    vm_push(vm.heap[ptr+2+idx], vm.heap_types[ptr+2+idx]);
                } else {
                    // Packed Array Logic
                    char* data = (char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];
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
                int count = (int)vm.heap[ptr+2];
                int data = (int)vm.heap[ptr+3];
                bool found = false;
                for(int i=0; i<count; i++) {
                    // Simple search
                    if(vm.heap[data + i*2] == key) { // Note: String equality needs ptr compare or string compare!
                        // For string keys (pointer equality is used here for simplicity/performance in interpreter loop)
                        // Ideally strictly compare strings if key is string.
                        vm_push(vm.heap[data + i*2 + 1], vm.heap_types[data + i*2 + 1]);
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
            int ptr = (int)vm_pop();
            // Array, Bytes, Map location for Size is offset 1 (Len or Cap) or specific?
            // Array: [TYPE, LEN, ...]
            // Bytes: [TYPE, LEN, ...]
            // Map:   [TYPE, CAP, COUNT, ...] -> Count is offset 2
            int type = (int)vm.heap[ptr];
            if (type == TYPE_MAP) vm_push(vm.heap[ptr+2], T_NUM);
            else vm_push(vm.heap[ptr+1], T_NUM);
            break;
        }
        case OP_SLICE: {
            CHECK_STACK(3);
            double e = vm_pop();
            double s = vm_pop();
            int ptr = (int)vm_pop();

            // FIX 1: Retrieve the specific type of the object being sliced
            int type = (int)vm.heap[ptr];

            // Get length based on type (Array/Bytes stores len at offset 1)
            int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];

            int start = (int)s;
            int end = (int)e;

            // Handle negative indices
            if (start < 0) start += len;
            if (end < 0) end += len;

            // Clamp bounds
            if (start < 0) start = 0;
            // Note: Mylo slices appear inclusive based on existing logic (end-start+1)
            // Logic: if end is beyond length, cap it at last index
            if (end >= len) end = len - 1;

            int newlen = (end >= start) ? (end - start + 1) : 0;

            if (type == TYPE_BYTES) {
                // FIX 2: Handle Byte Array Slicing
                // 1. Calculate doubles needed for packed bytes
                int d_needed = (newlen + 7) / 8;
                int newptr = heap_alloc(2 + d_needed);

                // 2. Set correct type
                vm.heap[newptr] = TYPE_BYTES;
                vm.heap[newptr + 1] = (double)newlen;

                // 3. Perform Byte-wise copy
                char* src = (char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];
                char* dst = (char*)&vm.heap[newptr + HEAP_HEADER_ARRAY];

                if (newlen > 0) {
                    for (int i = 0; i < newlen; i++) {
                        dst[i] = src[start + i];
                    }
                }
                vm_push((double)newptr, T_OBJ);
            } else {
                // FIX 3: Existing Logic for Standard Arrays
                int newptr = heap_alloc(newlen + 2);
                vm.heap[newptr] = TYPE_ARRAY;
                vm.heap[newptr + 1] = (double)newlen;
                for (int i = 0; i < newlen; i++) {
                    vm.heap[newptr + 2 + i] = vm.heap[ptr + 2 + start + i];
                    vm.heap_types[newptr + 2 + i] = vm.heap_types[ptr + 2 + start + i];
                }
                vm_push((double)newptr, T_OBJ);
            }
            break;
        }
        case OP_MAP: {
            int cap = MAP_INITIAL_CAP;
            int map = heap_alloc(4);
            int data = heap_alloc(cap*2);
            vm.heap[map] = TYPE_MAP;
            vm.heap[map+1] = (double)cap;
            vm.heap[map+2] = 0;
            vm.heap[map+3] = (double)data;
            vm_push((double)map, T_OBJ);
            break;
        }
        case OP_ASET: {
            CHECK_STACK(3);
            double val = vm_pop(); int vt = vm.stack_types[vm.sp+1];
            double key = vm_pop();
            int ptr = (int)vm.stack[vm.sp]; vm_pop(); // Pop array ptr
            int type = (int)vm.heap[ptr];

            if (type == TYPE_ARRAY) {
                int idx = (int)key;
                vm.heap[ptr+2+idx] = val;
                vm.heap_types[ptr+2+idx] = vt;
            } else if (type == TYPE_BYTES) {
                unsigned char* b = (unsigned char*)&vm.heap[ptr+HEAP_HEADER_ARRAY];
                b[(int)key] = (unsigned char)val;
            } else if (type == TYPE_MAP) {
                // Simple Map Insert
                int cap = (int)vm.heap[ptr+1];
                int count = (int)vm.heap[ptr+2];
                int data = (int)vm.heap[ptr+3];
                // Check exist
                bool found = false;
                for(int i=0; i<count; i++) {
                    if (vm.heap[data+i*2] == key) {
                        vm.heap[data+i*2+1] = val;
                        vm.heap_types[data+i*2+1] = vt;
                        found = true; break;
                    }
                }
                if(!found) {
                    if (count >= cap) {
                        // Grow (naive)
                        int new_cap = cap * 2;
                        int new_data = heap_alloc(new_cap*2);
                        for(int i=0; i<count*2; i++) {
                            vm.heap[new_data+i] = vm.heap[data+i];
                            vm.heap_types[new_data+i] = vm.heap_types[data+i];
                        }
                        vm.heap[ptr+1] = (double)new_cap;
                        vm.heap[ptr+3] = (double)new_data;
                        data = new_data;
                    }
                    vm.heap[data + count*2] = key;
                    vm.heap_types[data + count*2] = T_STR; // Key type assumption
                    vm.heap[data + count*2 + 1] = val;
                    vm.heap_types[data + count*2 + 1] = vt;
                    vm.heap[ptr+2] = (double)(count+1);
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
            int ptr = heap_alloc(2+d_needed);
            vm.heap[ptr] = TYPE_BYTES;
            vm.heap[ptr+1] = (double)len;
            char* b = (char*)&vm.heap[ptr+2];
            memcpy(b, s, len);
            vm_push((double)ptr, T_OBJ);
            break;
        }
        case OP_SLICE_SET: {
            CHECK_STACK(4);
            // Stack: [Ptr, Start, End, Value]

            double val = vm_pop(); int vt = vm.stack_types[vm.sp + 1];
            double e = vm_pop();
            double s = vm_pop();
            int ptr = (int)vm_pop(); // Destination Array/Bytes pointer

            int type = (int)vm.heap[ptr];
            int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];

            int start = (int)s;
            int end = (int)e;

            // Normalize Bounds
            if (start < 0) start += len;
            if (end < 0) end += len;
            if (start < 0) start = 0;
            if (end >= len) end = len - 1;

            int slice_len = (end >= start) ? (end - start + 1) : 0;

            if (type == TYPE_BYTES) {
                // Runtime Checks
                if (vt != T_OBJ) RUNTIME_ERROR("Slice assignment requires byte string");
                int vptr = (int)val;
                if ((int)vm.heap[vptr] != TYPE_BYTES) RUNTIME_ERROR("Slice assignment requires byte string");

                int vlen = (int)vm.heap[vptr + HEAP_OFFSET_LEN];
                if (vlen != slice_len) RUNTIME_ERROR("Slice assignment length mismatch");

                // Perform Byte Copy
                char* dst = (char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];
                char* src = (char*)&vm.heap[vptr + HEAP_HEADER_ARRAY];

                for(int i = 0; i < slice_len; i++) {
                    dst[start + i] = src[i];
                }
            } else if (type == TYPE_ARRAY) {
                // Array support (optional, but good for consistency)
                 if (vt != T_OBJ) RUNTIME_ERROR("Slice assignment requires array");
                 int vptr = (int)val;
                 // (Additional type checks omitted for brevity)
                 int vlen = (int)vm.heap[vptr + HEAP_OFFSET_LEN];
                 if (vlen != slice_len) RUNTIME_ERROR("Slice assignment length mismatch");

                 for(int i = 0; i < slice_len; i++) {
                     vm.heap[ptr + 2 + start + i] = vm.heap[vptr + 2 + i];
                     vm.heap_types[ptr + 2 + start + i] = vm.heap_types[vptr + 2 + i];
                 }
            }

            vm_push(val, vt); // Assignment evaluates to the value assigned
            break;
        }
        case OP_IT_KEY:
        case OP_IT_VAL:
        case OP_IT_DEF: {
            CHECK_STACK(2);
            double idx_val = vm_pop();
            double obj_val = vm_pop();
            int idx = (int)idx_val;
            int ptr = (int)obj_val;
            int type = (int)vm.heap[ptr];

            if (type == TYPE_ARRAY || type == TYPE_BYTES) {
                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                if (idx < 0 || idx >= len) RUNTIME_ERROR("Iterator OOB");

                if (op == OP_IT_KEY) {
                    // For arrays, Key is the Index
                    vm_push((double)idx, T_NUM);
                } else {
                    // OP_IT_VAL or OP_IT_DEF (Array default is Value)
                    if (type == TYPE_BYTES) {
                         unsigned char* b = (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];
                         vm_push((double)b[idx], T_NUM);
                    } else {
                         vm_push(vm.heap[ptr + 2 + idx], vm.heap_types[ptr + 2 + idx]);
                    }
                }
            }
            else if (type == TYPE_MAP) {
                int count = (int)vm.heap[ptr + HEAP_OFFSET_COUNT];
                if (idx < 0 || idx >= count) RUNTIME_ERROR("Iterator OOB");

                int data = (int)vm.heap[ptr + HEAP_OFFSET_DATA];

                // Map Memory Layout: [Key, Value, Key, Value...]
                // Key is at 2*idx, Value is at 2*idx + 1

                if (op == OP_IT_VAL) {
                    // Explicit Value Request
                    vm_push(vm.heap[data + idx * 2 + 1], vm.heap_types[data + idx * 2 + 1]);
                } else if (op == OP_IT_KEY) {
                    // Explicit Key Request
                    vm_push(vm.heap[data + idx * 2], vm.heap_types[data + idx * 2]);
                } else {
                    // OP_IT_DEF: Default for 'for(x in map)' is Key
                    vm_push(vm.heap[data + idx * 2], vm.heap_types[data + idx * 2]);
                }
            }
            else {
                RUNTIME_ERROR("Type not iterable");
            }
            break;
        }
        case OP_EMBED: {
            // Structure in bytecode: [OP_EMBED] [LENGTH] [BYTE 1] [BYTE 2] ...

            // 1. Read Length
            int len = vm.bytecode[vm.ip++];

            // 2. Allocate Heap Object (TYPE_BYTES)
            // Calculate how many doubles we need to store 'len' bytes
            int d_needed = (len + 7) / 8;
            int ptr = heap_alloc(d_needed + HEAP_HEADER_ARRAY);

            vm.heap[ptr] = TYPE_BYTES;
            vm.heap[ptr + HEAP_OFFSET_LEN] = (double)len;

            // 3. Read Data Stream from Bytecode
            unsigned char* dst = (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];

            for(int i=0; i<len; i++) {
                // We emitted them as individual ints in the bytecode array
                int b = vm.bytecode[vm.ip++];
                dst[i] = (unsigned char)b;
            }

            // 4. Push Result
            vm_push((double)ptr, T_OBJ);
            break;
        }
        case OP_MAKE_ARR: {
            int count = vm.bytecode[vm.ip++];
            int type_id = vm.bytecode[vm.ip++];

            // 1. Calculate Size
            int elem_size = get_type_size(type_id);
            int total_bytes = count * elem_size;
            int doubles_needed = (total_bytes + 7) / 8;

            // 2. Alloc
            int ptr = heap_alloc(doubles_needed + HEAP_HEADER_ARRAY);
            vm.heap[ptr] = (double)type_id;
            vm.heap[ptr + HEAP_OFFSET_LEN] = (double)count;

            // 3. Fill (Pop from stack backwards)
            // Access raw byte memory of the heap payload
            char* data_ptr = (char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];

            for(int i = count - 1; i >= 0; i--) {
                double val = vm.stack[vm.sp];
                vm.sp--; // Pop manually

                // Pack Value based on type
                switch(type_id) {
                    case TYPE_BYTES:
                    case TYPE_BOOL_ARRAY:
                        data_ptr[i] = (unsigned char)val;
                        break;
                    case TYPE_I16_ARRAY:
                        ((short*)data_ptr)[i] = (short)val;
                        break;
                    case TYPE_I32_ARRAY:
                        ((int*)data_ptr)[i] = (int)val;
                        break;
                    case TYPE_F32_ARRAY:
                        ((float*)data_ptr)[i] = (float)val;
                        break;
                    case TYPE_I64_ARRAY:
                        ((long long*)data_ptr)[i] = (long long)val;
                        break;
                    default:
                        // Should use OP_ARR for generic lists, but safety fallback:
                        break;
                }
            }
            vm_push((double)ptr, T_OBJ);
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