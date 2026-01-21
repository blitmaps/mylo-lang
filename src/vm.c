#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "vm.h"

// Global Instance
VM vm;
NativeFunc natives[MAX_NATIVES];
MyloConfigType MyloConfig = {false};

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
    "MAP", "ASET"
};

void vm_init() {
    memset(&vm, 0, sizeof(VM));
    vm.sp = -1;
    memset(natives, 0, sizeof(natives));
}

// --- ERROR HELPER ---
#define RUNTIME_ERROR(fmt, ...) { \
    printf("[Line %d] Runtime Error: " fmt "\n", vm.lines[vm.ip - 1], ##__VA_ARGS__); \
    exit(1); \
}

#define CHECK_STACK(count) if (vm.sp < (count) - 1) RUNTIME_ERROR("Stack Underflow")
#define CHECK_OBJ(depth) if (vm.stack_types[vm.sp - (depth)] != T_OBJ) RUNTIME_ERROR("Expected Object/Array, got %s", vm.stack_types[vm.sp - (depth)] == T_NUM ? "Number" : "String")

void vm_push(double val, int type) {
    if (vm.sp >= STACK_SIZE - 1) {
        printf("Error: Stack Overflow\n");
        // Overflow isn't tied to a specific line usually, but could use vm.lines[vm.ip]
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
    int i = 0;
    for (; i < MAX_STRING_LENGTH - 1 && s[i] != '\0'; i++) vm.string_pool[vm.str_count][i] = s[i];
    vm.string_pool[vm.str_count][i] = '\0';
    return vm.str_count++;
}

int make_const(double val) {
    for (int i = 0; i < vm.const_count; i++) {
        if (vm.constants[i] == val) return i;
    }
    if (vm.const_count >= MAX_CONSTANTS) {
        printf("Error: Constant Pool Overflow\n");
        exit(1);
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

void run_vm(bool debug_trace) {
    vm.ip = 0;
    bool running = true;
    setvbuf(stdout, NULL, _IONBF, 0);

    while (running && vm.ip < vm.code_size) {
        if (debug_trace) {
            int op = vm.bytecode[vm.ip];
            if (op >= 0 && op <= OP_ASET) {
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
                if (typeA == T_NUM && typeB == T_NUM) {
                    vm.stack[vm.sp] = valA + valB;
                    vm.stack_types[vm.sp] = T_NUM;
                } else if (typeA == T_OBJ && typeB == T_OBJ) {
                    int ptrA = (int) valA;
                    int ptrB = (int) valB;
                    if ((int) vm.heap[ptrA] != TYPE_ARRAY || (int) vm.heap[ptrB] != TYPE_ARRAY) {
                        RUNTIME_ERROR("Concatenation invalid (not arrays)");
                    }
                    int lenA = (int) vm.heap[ptrA + HEAP_OFFSET_LEN];
                    int lenB = (int) vm.heap[ptrB + HEAP_OFFSET_LEN];
                    int newLen = lenA + lenB;
                    int newPtr = heap_alloc(newLen + HEAP_HEADER_ARRAY);
                    vm.heap[newPtr] = (double) TYPE_ARRAY;
                    vm.heap[newPtr + 1] = (double) newLen;
                    for (int i = 0; i < lenA; i++) {
                        vm.heap[newPtr + 2 + i] = vm.heap[ptrA + 2 + i];
                        vm.heap_types[newPtr + 2 + i] = vm.heap_types[ptrA + 2 + i];
                    }
                    for (int i = 0; i < lenB; i++) {
                        vm.heap[newPtr + 2 + lenA + i] = vm.heap[ptrB + 2 + i];
                        vm.heap_types[newPtr + 2 + lenA + i] = vm.heap_types[ptrB + 2 + i];
                    }
                    vm.stack[vm.sp] = (double) newPtr;
                    vm.stack_types[vm.sp] = T_OBJ;
                } else {
                    RUNTIME_ERROR("Invalid types for ADD (Operands must be NUM or ARRAY)");
                }
                break;
            }
            case OP_SUB: {
                CHECK_STACK(2);
                double b = vm_pop();
                double a = vm.stack[vm.sp];
                vm.stack[vm.sp] = a - b;
                vm.stack_types[vm.sp] = T_NUM;
                break;
            }
            case OP_MUL: {
                CHECK_STACK(2);
                double b = vm_pop();
                double a = vm.stack[vm.sp];
                vm.stack[vm.sp] = a * b;
                vm.stack_types[vm.sp] = T_NUM;
                break;
            }
            case OP_DIV: {
                CHECK_STACK(2);
                double b = vm_pop();
                double a = vm.stack[vm.sp];
                if (b == 0) RUNTIME_ERROR("Division by zero");
                vm.stack[vm.sp] = a / b;
                vm.stack_types[vm.sp] = T_NUM;
                break;
            }
            case OP_MOD: {
                CHECK_STACK(2);
                double b = vm_pop();
                double a = vm.stack[vm.sp];
                if (b == 0) RUNTIME_ERROR("Modulo by zero");
                vm.stack[vm.sp] = fmod(a, b);
                vm.stack_types[vm.sp] = T_NUM;
                break;
            }

            case OP_LT: {
                CHECK_STACK(2);
                double b = vm_pop();
                vm.stack[vm.sp] = (vm.stack[vm.sp] < b) ? 1.0 : 0.0;
                vm.stack_types[vm.sp] = T_NUM;
                break;
            }
            case OP_EQ: {
                CHECK_STACK(2);
                double b = vm_pop();
                vm.stack[vm.sp] = (vm.stack[vm.sp] == b) ? 1.0 : 0.0;
                vm.stack_types[vm.sp] = T_NUM;
                break;
            }
            case OP_GT: {
                CHECK_STACK(2);
                double b = vm_pop();
                vm.stack[vm.sp] = (vm.stack[vm.sp] > b) ? 1.0 : 0.0;
                vm.stack_types[vm.sp] = T_NUM;
                break;
            }
            case OP_GE: {
                CHECK_STACK(2);
                double b = vm_pop();
                vm.stack[vm.sp] = (vm.stack[vm.sp] >= b) ? 1.0 : 0.0;
                vm.stack_types[vm.sp] = T_NUM;
                break;
            }
            case OP_LE: {
                CHECK_STACK(2);
                double b = vm_pop();
                vm.stack[vm.sp] = (vm.stack[vm.sp] <= b) ? 1.0 : 0.0;
                vm.stack_types[vm.sp] = T_NUM;
                break;
            }
            case OP_NEQ: {
                CHECK_STACK(2);
                double b = vm_pop();
                vm.stack[vm.sp] = (vm.stack[vm.sp] != b) ? 1.0 : 0.0;
                vm.stack_types[vm.sp] = T_NUM;
                break;
            }

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
                int fp = (int) vm.fp;
                vm_push(vm.stack[fp + off], vm.stack_types[fp + off]);
                break;
            }
            case OP_SVAR: {
                int off = vm.bytecode[vm.ip++];
                int fp = (int) vm.fp;
                CHECK_STACK(1);
                vm.stack[fp + off] = vm.stack[vm.sp];
                vm.stack_types[fp + off] = vm.stack_types[vm.sp];
                vm.sp--;
                break;
            }
            case OP_JMP: vm.ip = vm.bytecode[vm.ip];
                break;
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
                for (int i = 0; i < n; i++) {
                    vm.stack[vm.sp + 2 - i] = vm.stack[vm.sp - i];
                    vm.stack_types[vm.sp + 2 - i] = vm.stack_types[vm.sp - i];
                }
                vm.stack[as] = (double) vm.ip;
                vm.stack_types[as] = T_NUM;
                vm.stack[as + 1] = (double) vm.fp;
                vm.stack_types[as + 1] = T_NUM;
                vm.sp += 2;
                vm.fp = as + 2;
                vm.ip = t;
                break;
            }
            case OP_RET: {
                CHECK_STACK(1);
                double rv = vm.stack[vm.sp];
                int rt = vm.stack_types[vm.sp];
                vm.sp--;
                vm.sp = vm.fp;
                vm.sp--;
                int fp = (int) vm.fp;
                vm.fp = (int) vm.stack[fp - 1];
                vm.ip = (int) vm.stack[fp - 2];
                vm.sp -= 2;
                vm_push(rv, rt);
                break;
            }

            case OP_PRN: {
                CHECK_STACK(1);
                double v = vm.stack[vm.sp];
                int t = vm.stack_types[vm.sp];
                vm.sp--;
                if (t == T_STR) {
                    if (!MyloConfig.print_to_memory) printf("%s\n", vm.string_pool[(int) v]);
                    else
                        vm.output_mem_pos += sprintf(vm.output_char_buffer + vm.output_mem_pos, "%s\n",
                                                     vm.string_pool[(int) v]);
                } else if (t == T_OBJ) {
                    if (!MyloConfig.print_to_memory) printf("[Ref: %d]\n", (int) v);
                    else
                        vm.output_mem_pos +=
                                sprintf(vm.output_char_buffer + vm.output_mem_pos, "[Ref: %d]\n", (int) v);
                } else {
                    if (!MyloConfig.print_to_memory) {
                        if (v == (int) v) printf("%d\n", (int) v);
                        else printf("%g\n", v);
                    } else {
                        if (v == (int) v)
                            vm.output_mem_pos += sprintf(vm.output_char_buffer + vm.output_mem_pos,
                                                         "%d\n", (int) v);
                        else vm.output_mem_pos += sprintf(vm.output_char_buffer + vm.output_mem_pos, "%g\n", v);
                    }
                }
                break;
            }
            case OP_CAT: {
                CHECK_STACK(2);
                double b = vm_pop();
                int at = vm.stack_types[vm.sp];
                double a = vm.stack[vm.sp];
                char ba[1024], bb[1024];
                if (at == T_STR) strcpy(ba, vm.string_pool[(int) a]);
                else sprintf(ba, "%g", a);
                if (vm.stack_types[vm.sp + 1] == T_STR) strcpy(bb, vm.string_pool[(int) b]);
                else sprintf(bb, "%g", b);
                char r[2048];
                sprintf(r, "%s%s", ba, bb);
                vm.stack[vm.sp] = (double) make_string(r);
                vm.stack_types[vm.sp] = T_STR;
                break;
            }

            case OP_ALLOC: {
                int s = vm.bytecode[vm.ip++];
                int id = vm.bytecode[vm.ip++];
                int a = heap_alloc(s + 1);
                vm.heap[a] = (double) id;
                vm_push((double) a, T_OBJ);
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
                double v = vm_pop();
                int t = vm.stack_types[vm.sp + 1];
                int p = (int) vm.stack[vm.sp];
                if ((int) vm.heap[p] != eid) RUNTIME_ERROR("Type Mismatch in HSET (expected %d, got %d)", eid,
                                                           (int)vm.heap[p]);
                vm.heap[p + 1 + off] = v;
                vm.heap_types[p + 1 + off] = t;
                break;
            }
            case OP_HGET: {
                int off = vm.bytecode[vm.ip++];
                int eid = vm.bytecode[vm.ip++];
                CHECK_STACK(1);
                int p = (int) vm.stack[vm.sp];
                if ((int) vm.heap[p] != eid) RUNTIME_ERROR("Type Mismatch in HGET (expected %d, got %d)", eid,
                                                           (int)vm.heap[p]);
                vm.stack[vm.sp] = vm.heap[p + 1 + off];
                vm.stack_types[vm.sp] = vm.heap_types[p + 1 + off];
                break;
            }
            case OP_POP: vm_pop();
                break;
            case OP_NATIVE: {
                int id = vm.bytecode[vm.ip++];
                if (natives[id]) natives[id](&vm);
                else RUNTIME_ERROR("Unknown Native Function ID: %d", id);
                break;
            }
            case OP_ARR: {
                int c = vm.bytecode[vm.ip++];
                int a = heap_alloc(c + 2);
                vm.heap[a] = TYPE_ARRAY;
                vm.heap[a + 1] = (double) c;
                for (int i = c; i > 0; i--) {
                    vm.heap[a + 1 + i] = vm.stack[vm.sp];
                    vm.heap_types[a + 1 + i] = vm.stack_types[vm.sp];
                    vm.sp--;
                }
                vm_push((double) a, T_OBJ);
                break;
            }

            case OP_AGET: {
                CHECK_STACK(2);
                double keyVal = vm_pop();
                int keyType = vm.stack_types[vm.sp + 1];
                CHECK_OBJ(0);
                int ptr = (int) vm_pop();
                int objType = (int) vm.heap[ptr + HEAP_OFFSET_TYPE];
                if (objType == TYPE_ARRAY) {
                    if (keyType != T_NUM) RUNTIME_ERROR("Array index must be a number");
                    int idx = (int) keyVal;
                    int len = (int) vm.heap[ptr + HEAP_OFFSET_LEN];
                    if (idx < 0) idx += len;
                    if (idx < 0 || idx >= len) RUNTIME_ERROR("Array Index out of bounds: %d (len: %d)", idx, len);
                    vm_push(vm.heap[ptr + HEAP_HEADER_ARRAY + idx], vm.heap_types[ptr + HEAP_HEADER_ARRAY + idx]);
                } else if (objType == TYPE_MAP) {
                    if (keyType != T_STR) RUNTIME_ERROR("Map key must be a string");
                    int count = (int) vm.heap[ptr + HEAP_OFFSET_COUNT];
                    int dataPtr = (int) vm.heap[ptr + HEAP_OFFSET_DATA];
                    bool found = false;
                    for (int i = 0; i < count; i++) {
                        if (vm.heap[dataPtr + i * 2] == keyVal) {
                            vm_push(vm.heap[dataPtr + i * 2 + 1], vm.heap_types[dataPtr + i * 2 + 1]);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        int emptyId = make_string("");
                        vm_push((double) emptyId, T_STR);
                    }
                } else {
                    RUNTIME_ERROR("Expected Array or Map for indexing");
                }
                break;
            }
            case OP_ALEN: {
                CHECK_STACK(1);
                CHECK_OBJ(0);
                int p = (int) vm_pop();
                vm_push(vm.heap[p + 1], T_NUM);
                break;
            }
            case OP_SLICE: {
                CHECK_STACK(3);
                double e = vm_pop();
                double s = vm_pop();
                int p = (int) vm_pop();
                int len = (int) vm.heap[p + 1];
                int start = (int) s;
                int end = (int) e;
                if (start < 0) start += len;
                if (end < 0) end += len;
                if (start < 0) start = 0;
                if (end >= len) end = len - 1;
                int nlen = (end >= start) ? (end - start + 1) : 0;
                int np = heap_alloc(nlen + 2);
                vm.heap[np] = TYPE_ARRAY;
                vm.heap[np + 1] = (double) nlen;
                for (int i = 0; i < nlen; i++) {
                    vm.heap[np + 2 + i] = vm.heap[p + 2 + start + i];
                    vm.heap_types[np + 2 + i] = vm.heap_types[p + 2 + start + i];
                }
                vm_push((double) np, T_OBJ);
                break;
            }
            case OP_MAP: {
                int capacity = MAP_INITIAL_CAP;
                int mapAddr = heap_alloc(HEAP_HEADER_MAP);
                int dataAddr = heap_alloc(capacity * 2);
                vm.heap[mapAddr + HEAP_OFFSET_TYPE] = (double) TYPE_MAP;
                vm.heap[mapAddr + HEAP_OFFSET_CAP] = (double) capacity;
                vm.heap[mapAddr + HEAP_OFFSET_COUNT] = 0.0;
                vm.heap[mapAddr + HEAP_OFFSET_DATA] = (double) dataAddr;
                vm_push((double) mapAddr, T_OBJ);
                break;
            }
            case OP_ASET: {
                CHECK_STACK(3);
                double val = vm_pop();
                int valType = vm.stack_types[vm.sp + 1];
                double key = vm_pop();
                int keyType = vm.stack_types[vm.sp + 1];
                CHECK_OBJ(0);
                int ptr = (int) vm.stack[vm.sp];
                vm_pop();
                int objType = (int) vm.heap[ptr + HEAP_OFFSET_TYPE];
                if (objType == TYPE_ARRAY) {
                    if (keyType != T_NUM) RUNTIME_ERROR("Array index must be a number");
                    int idx = (int) key;
                    int len = (int) vm.heap[ptr + HEAP_OFFSET_LEN];
                    if (idx < 0) idx += len;
                    if (idx < 0 || idx >= len) RUNTIME_ERROR("Index out of bounds");
                    vm.heap[ptr + HEAP_HEADER_ARRAY + idx] = val;
                    vm.heap_types[ptr + HEAP_HEADER_ARRAY + idx] = valType;
                } else if (objType == TYPE_MAP) {
                    if (keyType != T_STR) RUNTIME_ERROR("Map key must be a string");
                    int cap = (int) vm.heap[ptr + HEAP_OFFSET_CAP];
                    int count = (int) vm.heap[ptr + HEAP_OFFSET_COUNT];
                    int dataPtr = (int) vm.heap[ptr + HEAP_OFFSET_DATA];
                    bool found = false;
                    for (int i = 0; i < count; i++) {
                        if (vm.heap[dataPtr + i * 2] == key) {
                            vm.heap[dataPtr + i * 2 + 1] = val;
                            vm.heap_types[dataPtr + i * 2 + 1] = valType;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        if (count >= cap) {
                            int newCap = cap * 2;
                            int newData = heap_alloc(newCap * 2);
                            for (int i = 0; i < count * 2; i++) {
                                vm.heap[newData + i] = vm.heap[dataPtr + i];
                                vm.heap_types[newData + i] = vm.heap_types[dataPtr + i];
                            }
                            vm.heap[ptr + HEAP_OFFSET_CAP] = (double) newCap;
                            vm.heap[ptr + HEAP_OFFSET_DATA] = (double) newData;
                            dataPtr = newData;
                        }
                        vm.heap[dataPtr + count * 2] = key;
                        vm.heap_types[dataPtr + count * 2] = T_STR;
                        vm.heap[dataPtr + count * 2 + 1] = val;
                        vm.heap_types[dataPtr + count * 2 + 1] = valType;
                        vm.heap[ptr + HEAP_OFFSET_COUNT] = (double) (count + 1);
                    }
                } else RUNTIME_ERROR("Assignment expected Array or Map");
                vm_push(val, valType);
                break;
            }
            case OP_HLT: running = false;
                break;
        }
    }
}
