#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "vm.h"

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
    "IT_DEF"
};

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
    memset(&vm, 0, sizeof(VM));
    vm.sp = -1;
    memset(natives, 0, sizeof(natives));
    ref_next_id = 0;
}

#define RUNTIME_ERROR(fmt, ...) { \
    printf("[Line %d] Runtime Error: " fmt "\n", vm.lines[vm.ip - 1], ##__VA_ARGS__); \
    exit(1); \
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

// --- SINGLE STEP EXECUTION ---
// Returns the opcode executed, or OP_HLT (-1) if finished
int vm_step(bool debug_trace) {
    if (vm.ip >= vm.code_size) return -1;

    if (debug_trace) {
        int op = vm.bytecode[vm.ip];
        if (op >= 0 && op <= OP_MK_BYTES) {
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
            // 2. Array + Array (Concatenation) - Keeps existing logic for now
            else if (typeA == T_OBJ && typeB == T_OBJ) {
                // ... (Existing Concatenation Logic Omitted for Brevity - No Change) ...
                // Note: To be fully safe, you should add checks here to ensure both are TYPE_ARRAY
                // But for now we focus on the Broadcast Vector Math below.

                // COPY PASTE YOUR EXISTING CONCAT LOGIC HERE IF REPLACING WHOLE BLOCK
                // OR Just wrap the broadcast block below.
                int ptrA = (int)valA; int ptrB = (int)valB;
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
            // 3. Array/Bytes + Scalar (Broadcast Addition)
            else if (typeA == T_OBJ || typeB == T_OBJ) {
                double arrVal = (typeA == T_OBJ) ? valA : valB;
                double scalVal = (typeA == T_OBJ) ? valB : valA;
                int scalType = (typeA == T_OBJ) ? typeB : typeA;

                int ptr = (int)arrVal;
                int hType = (int)vm.heap[ptr];

                // FIX: Allow TYPE_BYTES
                if (hType != TYPE_ARRAY && hType != TYPE_BYTES) RUNTIME_ERROR("Invalid object type for addition");

                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                // Result is always TYPE_ARRAY to handle overflow (255+1)
                int newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);
                vm.heap[newPtr] = TYPE_ARRAY;
                vm.heap[newPtr + 1] = (double)len;

                // Pointer to raw bytes if needed
                unsigned char* byteData = (hType == TYPE_BYTES) ? (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY] : NULL;

                for (int i = 0; i < len; i++) {
                    double elVal;
                    int elType;

                    // Read Element
                    if (hType == TYPE_BYTES) {
                        elVal = (double)byteData[i];
                        elType = T_NUM;
                    } else {
                        elVal = vm.heap[ptr + 2 + i];
                        elType = vm.heap_types[ptr + 2 + i];
                    }

                    // Perform Add
                    if (elType == T_NUM && scalType == T_NUM) {
                        vm.heap[newPtr + 2 + i] = elVal + scalVal;
                        vm.heap_types[newPtr + 2 + i] = T_NUM;
                    }
                    else if (scalType == T_STR) {
                        char buf[1024];
                        // If byte/num, convert to string
                        if (elType == T_NUM) sprintf(buf, "%g", elVal);
                        else strcpy(buf, vm.string_pool[(int)elVal]);

                        const char* sScal = vm.string_pool[(int)scalVal];

                        if (typeA == T_OBJ) strcat(buf, sScal);
                        else {
                            char temp[1024]; strcpy(temp, sScal); strcat(temp, buf); strcpy(buf, temp);
                        }
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
                if (hType != TYPE_ARRAY && hType != TYPE_BYTES) RUNTIME_ERROR("Invalid object type for SUB");
                if (typeA == T_OBJ && typeB != T_NUM) RUNTIME_ERROR("Can only subtract number from array");
                if (typeB == T_OBJ && typeA != T_NUM) RUNTIME_ERROR("Can only subtract array from number");

                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                int newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);
                vm.heap[newPtr] = TYPE_ARRAY;
                vm.heap[newPtr + 1] = (double)len;

                unsigned char* byteData = (hType == TYPE_BYTES) ? (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY] : NULL;

                for (int i = 0; i < len; i++) {
                    double elVal;
                    if (hType == TYPE_BYTES) elVal = (double)byteData[i];
                    else {
                        elVal = vm.heap[ptr + 2 + i];
                        if (vm.heap_types[ptr + 2 + i] != T_NUM) RUNTIME_ERROR("Non-number element in SUB");
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
                if (hType != TYPE_ARRAY && hType != TYPE_BYTES) RUNTIME_ERROR("Invalid object type for MUL");

                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                int newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);
                vm.heap[newPtr] = TYPE_ARRAY;
                vm.heap[newPtr + 1] = (double)len;

                unsigned char* byteData = (hType == TYPE_BYTES) ? (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY] : NULL;

                for(int i = 0; i < len; i++) {
                     double elVal;
                     if (hType == TYPE_BYTES) elVal = (double)byteData[i];
                     else {
                         elVal = vm.heap[ptr + 2 + i];
                         if (vm.heap_types[ptr + 2 + i] != T_NUM) RUNTIME_ERROR("Non-number element in MUL");
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
                if (hType != TYPE_ARRAY && hType != TYPE_BYTES) RUNTIME_ERROR("Invalid object type for DIV");

                // Div Zero Check
                if (typeA == T_OBJ && scalar == 0) RUNTIME_ERROR("Div by zero");

                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                int newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);
                vm.heap[newPtr] = TYPE_ARRAY;
                vm.heap[newPtr + 1] = (double)len;

                unsigned char* byteData = (hType == TYPE_BYTES) ? (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY] : NULL;

                for (int i = 0; i < len; i++) {
                    double elVal;
                    if (hType == TYPE_BYTES) elVal = (double)byteData[i];
                    else {
                        elVal = vm.heap[ptr + 2 + i];
                        if (vm.heap_types[ptr + 2 + i] != T_NUM) RUNTIME_ERROR("Non-number element in DIV");
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
                if (hType != TYPE_ARRAY && hType != TYPE_BYTES) RUNTIME_ERROR("Invalid object type for MOD");

                if (typeA == T_OBJ && scalar == 0) RUNTIME_ERROR("Mod by zero");

                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                int newPtr = heap_alloc(len + HEAP_HEADER_ARRAY);
                vm.heap[newPtr] = TYPE_ARRAY;
                vm.heap[newPtr + 1] = (double)len;

                unsigned char* byteData = (hType == TYPE_BYTES) ? (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY] : NULL;

                for (int i = 0; i < len; i++) {
                    double elVal;
                    if (hType == TYPE_BYTES) elVal = (double)byteData[i];
                    else {
                        elVal = vm.heap[ptr + 2 + i];
                        if (vm.heap_types[ptr + 2 + i] != T_NUM) RUNTIME_ERROR("Non-number element in MOD");
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
        // --- UPDATED PRINT OP WITH CALLBACK ---
        case OP_PRN: {
            CHECK_STACK(1);
            double v = vm.stack[vm.sp];
            int t = vm.stack_types[vm.sp];
            vm.sp--;

            char buffer[1024];
            buffer[0] = '\0';

            if (t == T_STR) {
                snprintf(buffer, 1024, "%s", vm.string_pool[(int)v]);
            } else if (t == T_OBJ) {
                int ptr = (int)v;
                int type = (int)vm.heap[ptr + HEAP_OFFSET_TYPE];

                if (type == TYPE_BYTES) {
                    int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                    unsigned char* b = (unsigned char*)&vm.heap[ptr + HEAP_HEADER_ARRAY];
                    // Basic binary output representation
                    int offset = snprintf(buffer, 1024, "b\"");
                    for(int i=0; i<len && offset < 1000; i++) {
                        // Printable ascii check could be added here
                        buffer[offset++] = (char)b[i];
                    }
                    buffer[offset++] = '"';
                    buffer[offset] = '\0';
                } else if (type == TYPE_ARRAY) {
                    snprintf(buffer, 1024, "[Array Ref:%d]", ptr);
                } else if (type == TYPE_MAP) {
                    snprintf(buffer, 1024, "[Map Ref:%d]", ptr);
                } else {
                    snprintf(buffer, 1024, "[Ref: %d]", ptr);
                }
            } else {
                if (v == (int)v) snprintf(buffer, 1024, "%d", (int)v);
                else snprintf(buffer, 1024, "%g", v);
            }

            // --- REDIRECTION LOGIC ---
            if (MyloConfig.debug_mode && MyloConfig.print_callback) {
                // Send to debugger (no newline, debugger handles it)
                MyloConfig.print_callback(buffer);
            } else if (MyloConfig.print_to_memory) {
                // Test harness mode
                vm.output_mem_pos += sprintf(vm.output_char_buffer + vm.output_mem_pos, "%s\n", buffer);
            } else {
                // Standard mode
                printf("%s\n", buffer);
            }
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

            if (type == TYPE_ARRAY || type == TYPE_BYTES) {
                int idx = (int)key;
                int len = (int)vm.heap[ptr+1];
                if (idx < 0) idx += len;
                if (idx < 0 || idx >= len) RUNTIME_ERROR("Index OOB");

                if (type == TYPE_BYTES) {
                    unsigned char* b = (unsigned char*)&vm.heap[ptr+HEAP_HEADER_ARRAY];
                    vm_push((double)b[idx], T_NUM);
                } else {
                    vm_push(vm.heap[ptr+2+idx], vm.heap_types[ptr+2+idx]);
                }
            } else if (type == TYPE_MAP) {
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
                    // +2 accounts for the Heap Header (Type, Len) offset in bytes?
                    // No, &vm.heap[ptr + 2] points to the start of the data payload.
                    // We simply offset the src pointer by 'start' bytes.
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
        case OP_HLT: return -1;
    }
    return op;
}

void run_vm(bool debug_trace) {
    vm.ip = 0;
    while (vm.ip < vm.code_size) {
        if (vm_step(debug_trace) == -1) break;
    }
}