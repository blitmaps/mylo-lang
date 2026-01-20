#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "vm.h"

// Global Instance
VM vm;
NativeFunc natives[MAX_NATIVES];
MyloConfigType MyloConfig = { false };

const char* OP_NAMES[] = {
    "PSH_NUM", "PSH_STR",
    "ADD", "SUB", "MUL", "LT", "EQ", "GT", "GE", "LE", "NEQ",
    "SET", "GET", "LVAR", "SVAR",
    "JMP", "JZ", "JNZ",
    "CALL", "RET",
    "PRN", "HLT", "CAT",
    "ALLOC", "HSET", "HGET", "DUP",
    "POP",
    "NATIVE",
    "ARR", "AGET", "ALEN", "SLICE"
};

void vm_init() {
    memset(&vm, 0, sizeof(VM));
    vm.sp = -1;
    memset(natives, 0, sizeof(natives));
}

#define CHECK_STACK(count) if (vm.sp < (count) - 1) { printf("Error: Stack Underflow at IP %d\n", vm.ip); exit(1); }
#define CHECK_OBJ(depth) if (vm.stack_types[vm.sp - (depth)] != T_OBJ) { printf("Runtime Error: Expected Object/Array, got %s\n", vm.stack_types[vm.sp - (depth)] == T_NUM ? "Number" : "String"); exit(1); }

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

// FIXED: String Interning (Check for duplicates)
int make_string(const char* s) {
    // 1. Search existing pool
    for (int j = 0; j < vm.str_count; j++) {
        if (strcmp(vm.string_pool[j], s) == 0) {
            return j;
        }
    }

    // 2. Add new if not found
    if (vm.str_count >= MAX_STRINGS) { printf("Error: String Pool Overflow\n"); exit(1); }

    int i = 0;
    for (; i < MAX_STRING_LENGTH - 1 && s[i] != '\0'; i++) {
        vm.string_pool[vm.str_count][i] = s[i];
    }
    vm.string_pool[vm.str_count][i] = '\0';
    return vm.str_count++;
}

int make_const(double val) {
    for (int i = 0; i < vm.const_count; i++) {
        if (vm.constants[i] == val) return i;
    }
    if (vm.const_count >= MAX_CONSTANTS) { printf("Error: Constant Pool Overflow\n"); exit(1); }
    vm.constants[vm.const_count] = val;
    return vm.const_count++;
}

int heap_alloc(int size) {
    if (vm.heap_ptr + size >= MAX_HEAP) { printf("Error: Heap Overflow!\n"); exit(1); }
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
            if (op >= 0 && op <= OP_SLICE) {
                printf("[TRACE] IP:%04d SP:%2d OP:%s\n", vm.ip, vm.sp, OP_NAMES[op]);
            }
        }

        int op = vm.bytecode[vm.ip++];
        switch (op) {
            case OP_PSH_NUM: { int index = vm.bytecode[vm.ip++]; vm_push(vm.constants[index], T_NUM); break; }
            case OP_PSH_STR: { int index = vm.bytecode[vm.ip++]; vm_push((double)index, T_STR); break; }

            case OP_ADD: {
                CHECK_STACK(2);
                int typeB = vm.stack_types[vm.sp];
                double valB = vm_pop();
                int typeA = vm.stack_types[vm.sp];
                double valA = vm.stack[vm.sp];

                if (typeA == T_NUM && typeB == T_NUM) {
                    vm.stack[vm.sp] = valA + valB;
                    vm.stack_types[vm.sp]=T_NUM;
                }
                else if (typeA == T_OBJ && typeB == T_OBJ) {
                    int ptrA = (int)valA;
                    int ptrB = (int)valB;

                    if ((int)vm.heap[ptrA] != TYPE_ARRAY || (int)vm.heap[ptrB] != TYPE_ARRAY) {
                        printf("Runtime Error: Can only concatenate Arrays\n"); exit(1);
                    }

                    int lenA = (int)vm.heap[ptrA + HEAP_OFFSET_LEN];
                    int lenB = (int)vm.heap[ptrB + HEAP_OFFSET_LEN];
                    int newLen = lenA + lenB;
                    int newPtr = heap_alloc(newLen + HEAP_HEADER_ARRAY);

                    vm.heap[newPtr + HEAP_OFFSET_TYPE] = (double)TYPE_ARRAY;
                    vm.heap[newPtr + HEAP_OFFSET_LEN] = (double)newLen;

                    for(int i=0; i<lenA; i++) {
                        vm.heap[newPtr + HEAP_HEADER_ARRAY + i] = vm.heap[ptrA + HEAP_HEADER_ARRAY + i];
                        vm.heap_types[newPtr + HEAP_HEADER_ARRAY + i] = vm.heap_types[ptrA + HEAP_HEADER_ARRAY + i];
                    }
                    for(int i=0; i<lenB; i++) {
                        vm.heap[newPtr + HEAP_HEADER_ARRAY + lenA + i] = vm.heap[ptrB + HEAP_HEADER_ARRAY + i];
                        vm.heap_types[newPtr + HEAP_HEADER_ARRAY + lenA + i] = vm.heap_types[ptrB + HEAP_HEADER_ARRAY + i];
                    }
                    vm.stack[vm.sp] = (double)newPtr;
                    vm.stack_types[vm.sp] = T_OBJ;
                }
                else {
                    printf("Runtime Error: Invalid types for ADD\n"); exit(1);
                }
                break;
            }
            case OP_SUB: { CHECK_STACK(2); double b=vm_pop(); double a=vm.stack[vm.sp]; vm.stack[vm.sp] = a - b; vm.stack_types[vm.sp]=T_NUM; break; }
            case OP_MUL: { CHECK_STACK(2); double b=vm_pop(); double a=vm.stack[vm.sp]; vm.stack[vm.sp] = a * b; vm.stack_types[vm.sp]=T_NUM; break; }

            case OP_LT:  { CHECK_STACK(2); double b=vm_pop(); vm.stack[vm.sp]=(vm.stack[vm.sp]<b)?1.0:0.0; vm.stack_types[vm.sp]=T_NUM; break; }
            case OP_EQ:  { CHECK_STACK(2); double b=vm_pop(); vm.stack[vm.sp]=(vm.stack[vm.sp]==b)?1.0:0.0; vm.stack_types[vm.sp]=T_NUM; break; }
            case OP_GT:  { CHECK_STACK(2); double b=vm_pop(); vm.stack[vm.sp]=(vm.stack[vm.sp]>b)?1.0:0.0; vm.stack_types[vm.sp]=T_NUM; break; }
            case OP_GE:  { CHECK_STACK(2); double b=vm_pop(); vm.stack[vm.sp]=(vm.stack[vm.sp]>=b)?1.0:0.0; vm.stack_types[vm.sp]=T_NUM; break; }
            case OP_LE:  { CHECK_STACK(2); double b=vm_pop(); vm.stack[vm.sp]=(vm.stack[vm.sp]<=b)?1.0:0.0; vm.stack_types[vm.sp]=T_NUM; break; }
            case OP_NEQ: { CHECK_STACK(2); double b=vm_pop(); vm.stack[vm.sp]=(vm.stack[vm.sp]!=b)?1.0:0.0; vm.stack_types[vm.sp]=T_NUM; break; }

            case OP_SET: { int addr=vm.bytecode[vm.ip++]; CHECK_STACK(1); vm.globals[addr]=vm.stack[vm.sp]; vm.global_types[addr]=vm.stack_types[vm.sp]; vm.sp--; break; }
            case OP_GET: { int addr=vm.bytecode[vm.ip++]; vm_push(vm.globals[addr], vm.global_types[addr]); break; }
            case OP_LVAR:{ int off=vm.bytecode[vm.ip++]; int fp=(int)vm.fp; vm_push(vm.stack[fp+off], vm.stack_types[fp+off]); break; }
            case OP_SVAR:{ int off=vm.bytecode[vm.ip++]; int fp=(int)vm.fp; CHECK_STACK(1); vm.stack[fp+off]=vm.stack[vm.sp]; vm.stack_types[fp+off]=vm.stack_types[vm.sp]; vm.sp--; break; }
            case OP_JMP: vm.ip = vm.bytecode[vm.ip]; break;
            case OP_JZ:  { int t=vm.bytecode[vm.ip++]; if(vm_pop()==0.0) vm.ip=t; break; }
            case OP_JNZ: { int t=vm.bytecode[vm.ip++]; if(vm_pop()!=0.0) vm.ip=t; break; }

            case OP_CALL:{
                int target=vm.bytecode[vm.ip++]; int num_args=vm.bytecode[vm.ip++];
                CHECK_STACK(num_args);
                int args_start=vm.sp-num_args+1;
                for(int i=0; i<num_args; i++) {
                    vm.stack[vm.sp+2-i]=vm.stack[vm.sp-i];
                    vm.stack_types[vm.sp+2-i]=vm.stack_types[vm.sp-i];
                }
                vm.stack[args_start]=(double)vm.ip; vm.stack_types[args_start]=T_NUM;
                vm.stack[args_start+1]=(double)vm.fp; vm.stack_types[args_start+1]=T_NUM;
                vm.sp+=2; vm.fp=args_start+2; vm.ip=target;
                break;
            }
            case OP_RET: {
                CHECK_STACK(1);
                double rv=vm.stack[vm.sp]; int rt=vm.stack_types[vm.sp];
                vm.sp--; vm.sp=vm.fp; vm.sp--;
                int fp=(int)vm.fp; vm.fp=(int)vm.stack[fp-1]; vm.ip=(int)vm.stack[fp-2];
                vm.sp-=2; vm_push(rv, rt);
                break;
            }
            case OP_PRN: {
                CHECK_STACK(1);
                double val=vm.stack[vm.sp]; int type=vm.stack_types[vm.sp]; vm.sp--;
                if (type==T_STR) {
                    int id = (int)val;
                    if (id >= 0 && id < vm.str_count) {
                        if (!MyloConfig.print_to_memory) printf("%s\n", vm.string_pool[id]);
                        else vm.output_mem_pos += snprintf(vm.output_char_buffer+vm.output_mem_pos, sizeof(vm.output_char_buffer)-vm.output_mem_pos, "%s\n", vm.string_pool[id]);
                    }
                } else {
                    if (!MyloConfig.print_to_memory) {
                        if(type==T_OBJ) printf("[Ref: %d]\n", (int)val);
                        else if(val==(int)val) printf("%d\n", (int)val);
                        else printf("%g\n", val);
                    } else {
                        if(type==T_OBJ) vm.output_mem_pos += snprintf(vm.output_char_buffer+vm.output_mem_pos, sizeof(vm.output_char_buffer)-vm.output_mem_pos, "[Ref: %d]\n", (int)val);
                        else if(val==(int)val) vm.output_mem_pos += snprintf(vm.output_char_buffer+vm.output_mem_pos, sizeof(vm.output_char_buffer)-vm.output_mem_pos, "%d\n", (int)val);
                        else vm.output_mem_pos += snprintf(vm.output_char_buffer+vm.output_mem_pos, sizeof(vm.output_char_buffer)-vm.output_mem_pos, "%g\n", val);
                    }
                }
                break;
            }
            case OP_CAT: {
                 CHECK_STACK(2); double b=vm_pop(); int bt=vm.stack_types[vm.sp+1];
                 double a=vm.stack[vm.sp]; int at=vm.stack_types[vm.sp];
                 char bufA[MAX_STRING_LENGTH], bufB[MAX_STRING_LENGTH];
                 if(at==T_STR) strcpy(bufA, vm.string_pool[(int)a]); else sprintf(bufA, "%g", a);
                 if(bt==T_STR) strcpy(bufB, vm.string_pool[(int)b]); else sprintf(bufB, "%g", b);
                 char res[MAX_STRING_LENGTH]; snprintf(res, MAX_STRING_LENGTH, "%s%s", bufA, bufB);
                 vm.stack[vm.sp] = (double)make_string(res); vm.stack_types[vm.sp] = T_STR;
                 break;
            }

            case OP_ALLOC: {
                int size = vm.bytecode[vm.ip++];
                int struct_id = vm.bytecode[vm.ip++];
                int addr = heap_alloc(size + HEAP_HEADER_STRUCT);
                vm.heap[addr + HEAP_OFFSET_TYPE] = (double)struct_id;
                vm_push((double)addr, T_OBJ);
                break;
            }

            case OP_DUP: { CHECK_STACK(1); vm_push(vm.stack[vm.sp], vm.stack_types[vm.sp]); break; }

            case OP_HSET: {
                int offset = vm.bytecode[vm.ip++];
                int expected_id = vm.bytecode[vm.ip++];
                CHECK_STACK(2);
                CHECK_OBJ(1);

                double val = vm_pop();
                int type = vm.stack_types[vm.sp+1];
                int ptr = (int)vm.stack[vm.sp];

                if ((int)vm.heap[ptr + HEAP_OFFSET_TYPE] != expected_id) {
                    printf("Runtime Error: Type Mismatch. Expected struct type %d, got %d\n", expected_id, (int)vm.heap[ptr]);
                    exit(1);
                }

                vm.heap[ptr + HEAP_HEADER_STRUCT + offset] = val;
                vm.heap_types[ptr + HEAP_HEADER_STRUCT + offset] = type;
                break;
            }

            case OP_HGET: {
                int offset = vm.bytecode[vm.ip++];
                int expected_id = vm.bytecode[vm.ip++];
                CHECK_STACK(1);
                CHECK_OBJ(0);
                int ptr = (int)vm.stack[vm.sp];

                if ((int)vm.heap[ptr + HEAP_OFFSET_TYPE] != expected_id) {
                    printf("Runtime Error: Type Mismatch. Expected struct type %d, got %d\n", expected_id, (int)vm.heap[ptr]);
                    exit(1);
                }

                vm.stack[vm.sp] = vm.heap[ptr + HEAP_HEADER_STRUCT + offset];
                vm.stack_types[vm.sp] = vm.heap_types[ptr + HEAP_HEADER_STRUCT + offset];
                break;
            }

            case OP_POP: { vm_pop(); break; }
            case OP_NATIVE: {
                int id = vm.bytecode[vm.ip++];
                if (natives[id]) {
                    natives[id](&vm);
                } else {
                    printf("\nRuntime Error: Encountered Inline C Block during interpretation.\n");
                    printf("Inline C requires compilation. Use: mylo --build %s\n", "your_file.mylo");
                    exit(1);
                }
                break;
            }
            case OP_ARR: {
                int count = vm.bytecode[vm.ip++];
                int addr = heap_alloc(count + HEAP_HEADER_ARRAY);
                vm.heap[addr + HEAP_OFFSET_TYPE] = (double)TYPE_ARRAY;
                vm.heap[addr + HEAP_OFFSET_LEN] = (double)count;

                for(int i = count; i > 0; i--) {
                    vm.heap[addr + HEAP_HEADER_ARRAY + (i - 1)] = vm.stack[vm.sp];
                    vm.heap_types[addr + HEAP_HEADER_ARRAY + (i - 1)] = vm.stack_types[vm.sp];
                    vm.sp--;
                }
                vm_push((double)addr, T_OBJ);
                break;
            }
            case OP_AGET: {
                CHECK_STACK(2);
                double idx = vm_pop();
                CHECK_OBJ(0);
                double ref = vm_pop();
                int ptr = (int)ref;

                if ((int)vm.heap[ptr + HEAP_OFFSET_TYPE] != TYPE_ARRAY) {
                    printf("Runtime Error: Expected Array, got Type %d\n", (int)vm.heap[ptr]); exit(1);
                }

                int index = (int)idx;
                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];

                if (index < 0) index += len;
                if(index < 0 || index >= len) { printf("Runtime Error: Array Index Out of Bounds %d\n", index); exit(1); }
                vm_push(vm.heap[ptr + HEAP_HEADER_ARRAY + index], vm.heap_types[ptr + HEAP_HEADER_ARRAY + index]);
                break;
            }
            case OP_ALEN: {
                CHECK_STACK(1);
                CHECK_OBJ(0);
                int ptr = (int)vm_pop();
                if ((int)vm.heap[ptr + HEAP_OFFSET_TYPE] != TYPE_ARRAY) { printf("Runtime Error: len() expects array\n"); exit(1); }
                vm_push(vm.heap[ptr + HEAP_OFFSET_LEN], T_NUM);
                break;
            }
            case OP_SLICE: {
                CHECK_STACK(3);
                double endVal = vm_pop();
                double startVal = vm_pop();
                CHECK_OBJ(0);
                double refVal = vm_pop();
                int ptr = (int)refVal;

                if ((int)vm.heap[ptr + HEAP_OFFSET_TYPE] != TYPE_ARRAY) { printf("Runtime Error: Slice expects array\n"); exit(1); }

                int len = (int)vm.heap[ptr + HEAP_OFFSET_LEN];
                int start = (int)startVal;
                int end = (int)endVal;

                if (start < 0) start += len;
                if (end < 0) end += len;
                if (start < 0) start = 0;
                if (end >= len) end = len - 1;

                int newLen = (end >= start) ? (end - start + 1) : 0;
                int newPtr = heap_alloc(newLen + HEAP_HEADER_ARRAY);
                vm.heap[newPtr + HEAP_OFFSET_TYPE] = (double)TYPE_ARRAY;
                vm.heap[newPtr + HEAP_OFFSET_LEN] = (double)newLen;

                for (int i = 0; i < newLen; i++) {
                    vm.heap[newPtr + HEAP_HEADER_ARRAY + i] = vm.heap[ptr + HEAP_HEADER_ARRAY + start + i];
                    vm.heap_types[newPtr + HEAP_HEADER_ARRAY + i] = vm.heap_types[ptr + HEAP_HEADER_ARRAY + start + i];
                }
                vm_push((double)newPtr, T_OBJ);
                break;
            }
            case OP_HLT: running = false; break;
        }
    }
}