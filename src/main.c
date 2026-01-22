#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "vm.h"
#include "utils.h"

// Defined in compiler.c
void parse(char* source);
void compile_to_c_source(const char* output_filename);
void mylo_reset();
void disassemble();

void disassemble() {
    printf("\n--- Disassembly ---\n");
    int i = 0;
    while (i < vm.code_size) {
        int op = vm.bytecode[i];
        if (op < 0 || op > OP_NATIVE) { printf("%04d UNKNOWN %d\n", i, op); i++; continue; }

        printf("%04d %-10s ", i, OP_NAMES[op]);
        i++;

        switch (op) {
            case OP_PSH_NUM: { int idx = vm.bytecode[i++]; printf("[%d] (%g)", idx, vm.constants[idx]); break; }
            case OP_PSH_STR: { int idx = vm.bytecode[i++]; printf("[%d] (\"%s\")", idx, vm.string_pool[idx]); break; }
            case OP_JMP:
            case OP_JZ:
            case OP_JNZ: { int addr = vm.bytecode[i++]; printf("-> %04d", addr); break; }
            case OP_SET:
            case OP_GET: { int idx = vm.bytecode[i++]; printf("G[%d]", idx); break; }
            case OP_LVAR:
            case OP_SVAR: { int off = vm.bytecode[i++]; printf("FP[%d]", off); break; }
            case OP_CALL: { int target = vm.bytecode[i++]; int args = vm.bytecode[i++]; printf("-> %04d (%d args)", target, args); break; }
            case OP_ALLOC: { int size = vm.bytecode[i++]; printf("(size: %d)", size); break; }
            case OP_HSET:
            case OP_HGET: { int off = vm.bytecode[i++]; printf("Offset: %d", off); break; }
            case OP_NATIVE: { int id = vm.bytecode[i++]; printf("Native[%d]", id); break; }
            default: break;
        }
        printf("\n");
    }
    printf("-------------------\n\n");
}
extern int ffi_count; // Access the counter from compiler.c

int main(int argc, char** argv) {
    vm_init();

    if (argc < 2) { printf("Usage: mylo [--run|--build] <file> [--dump] [--trace]\n"); return 1; }

    bool build_mode = false;
    bool dump = false;
    bool trace = false;
    char* fn = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--build") == 0) build_mode = true;
        else if (strcmp(argv[i], "--run") == 0) build_mode = false;
        else if (strcmp(argv[i], "--dump") == 0) dump = true;
        else if (strcmp(argv[i], "--trace") == 0) trace = true;
        else fn = argv[i];
    }

    if(!fn) { printf("No input file provided.\n"); return 1; }

    char* content = read_file(fn);
    if (!content) {
        printf("Error: Cannot read file %s\n", fn);
        return 1;
    }

    mylo_reset();
    parse(content);

    // If we are NOT in build mode, but we found C blocks, stop.
    if (!build_mode && ffi_count > 0) {
        printf("Error: This program contains Native C blocks and cannot be interpreted.\n");
        printf("Please compile it using: mylo --build %s\n", fn);
        free(content);
        return 1;
    }

    if (dump) disassemble();

    if (build_mode) {
        compile_to_c_source("out.c");
    } else {
        run_vm(trace);
    }

    free(content);
    return 0;
}