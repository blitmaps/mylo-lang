#define VERSION_INFO "0.1.4.1"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "vm.h"
#include "utils.h"
#include "debug_adapter.h"

// Defined in compiler.c
void parse(char* source);
void compile_to_c_source(const char* output_filename);
void generate_binding_c_source(const char* output_filename);
void mylo_reset();
void disassemble();

// NEW: extern the counters
extern int ffi_count;
extern int bound_ffi_count;

void disassemble() {
    printf("\n--- Disassembly ---\n");
    int i = 0;
    while (i < vm.code_size) {
        int op = vm.bytecode[i];
        if (op < 0 || op > OP_MK_BYTES) { printf("%04d UNKNOWN %d\n", i, op); i++; continue; }

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
            case OP_ARR: { int count = vm.bytecode[i++]; printf("(count: %d)", count); break; }
            default: break;
        }
        printf("\n");
    }
    printf("-------------------\n\n");
}

int main(int argc, char** argv) {

    vm_init();

    if (argc < 2) { printf("Usage: mylo [--run|--build|--bind] <file> [--dump] [--trace] [--debug]\n"); return 1; }

    bool build_mode = false;
    bool bind_mode = false;
    bool dump = false;
    bool trace = false;
    bool debug_mode = false;
    bool version = false;
    char* fn = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--build") == 0) build_mode = true;
        else if (strcmp(argv[i], "--bind") == 0) bind_mode = true;
        else if (strcmp(argv[i], "--run") == 0) build_mode = false;
        else if (strcmp(argv[i], "--dump") == 0) dump = true;
        else if (strcmp(argv[i], "--trace") == 0) trace = true;
        else if (strcmp(argv[i], "--debug") == 0) debug_mode = true;
        else if (strcmp(argv[i], "--version") == 0) version = true;
        else fn = argv[i];
    }

    if (version) {
        printf("Mylo version %s\n", VERSION_INFO);
        return 0;
    }
    // Disable dlopen
    MyloConfig.build_mode = build_mode;
    if(!fn) { printf("No input file provided.\n"); return 1; }

    char* content = read_file(fn);
    if (!content) {
        printf("Error: Cannot read file %s\n", fn);
        return 1;
    }

    // 1. If Debugging, Hand off control immediately (Do NOT parse yet)
    if (debug_mode) { // assuming you kept the debug_mode flag from previous steps
        start_debug_adapter(fn, content);
        free(content);
        return 0;
    }

    mylo_reset();
    parse(content);

    if (bind_mode) {
        // Generate binding C file (e.g. test_lib.mylo -> test_lib.mylo_bind.c)
        char out_name[1024];
        snprintf(out_name, 1024, "%s_bind.c", fn);
        generate_binding_c_source(out_name);
        free(content);
        return 0;
    }

    if (build_mode) {
        compile_to_c_source("out.c");
        free(content);
        return 0;
    }

    // --- INTERPRETER SAFETY CHECK ---
    // If we have C blocks that were NOT bound via native modules, we must stop.
    if ((ffi_count - bound_ffi_count) > 0) {
        printf("Error: This program contains Native C blocks and cannot be interpreted.\n");
        printf("Please compile it using: mylo --build %s\n", fn);
        free(content);
        return 1;
    }

    if (dump) disassemble();

    run_vm(trace);

    free(content);
    return 0;
}