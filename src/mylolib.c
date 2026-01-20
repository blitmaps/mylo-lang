#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mylolib.h"
#include "vm.h"
#include "defines.h"

// --- Helpers ---

static const char* get_str(VM* vm, double val) {
    int id = (int)val;
    if (id < 0 || id >= vm->str_count) return "";
    return vm->string_pool[id];
}

void trim_newline(char* str) {
    int len = strlen(str);
    if (len > 0 && str[len-1] == '\n') str[len-1] = '\0';
    if (len > 1 && str[len-2] == '\r') str[len-2] = '\0';
}

// --- Standard Library Functions ---

void std_sqrt(VM* vm) {
    double val = vm_pop();
    vm_push(sqrt(val), T_NUM);
}

void std_len(VM* vm) {
    double val = vm_pop();
    int ptr = (int)val;

    // Check if Array
    if (vm->stack_types[vm->sp + 1] == T_OBJ &&
        (int)vm->heap[ptr + HEAP_OFFSET_TYPE] == TYPE_ARRAY) {
        double length = vm->heap[ptr + HEAP_OFFSET_LEN];
        vm_push(length, T_NUM);
    } else if (vm->stack_types[vm->sp + 1] == T_STR) {
        // Optional: Support string length
        const char* s = get_str(vm, val);
        vm_push((double)strlen(s), T_NUM);
    } else {
        printf("Runtime Error: len() expects array or string.\n");
        exit(1);
    }
}

void std_contains(VM* vm) {
    // Pop args (reverse order)
    double needle_val = vm_pop();
    int needle_type = vm->stack_types[vm->sp + 1];

    double haystack_val = vm_pop();
    int haystack_type = vm->stack_types[vm->sp + 1];

    // CASE 1: String contains Substring
    if (haystack_type == T_STR) {
        if (needle_type != T_STR) {
            printf("Runtime Error: contains() on string requires string needle.\n");
            exit(1);
        }
        const char* hay = get_str(vm, haystack_val);
        const char* ned = get_str(vm, needle_val);

        if (strstr(hay, ned) != NULL) vm_push(1.0, T_NUM);
        else vm_push(0.0, T_NUM);
        return;
    }

    // CASE 2: Array contains Element
    if (haystack_type == T_OBJ) {
        int ptr = (int)haystack_val;
        if ((int)vm->heap[ptr + HEAP_OFFSET_TYPE] == TYPE_ARRAY) {
            int len = (int)vm->heap[ptr + HEAP_OFFSET_LEN];
            int found = 0;

            for(int i=0; i<len; i++) {
                double el_val = vm->heap[ptr + HEAP_HEADER_ARRAY + i];
                int el_type = vm->heap_types[ptr + HEAP_HEADER_ARRAY + i];

                // Match Type AND Value
                if (el_type == needle_type && el_val == needle_val) {
                    found = 1;
                    break;
                }
            }
            vm_push(found ? 1.0 : 0.0, T_NUM);
            return;
        }
    }

    printf("Runtime Error: contains() expects string or array.\n");
    exit(1);
}

void std_read_lines(VM* vm) {
    double path_id = vm_pop();
    const char* path = get_str(vm, path_id);

    FILE* f = fopen(path, "r");
    if (!f) {
        int addr = heap_alloc(HEAP_HEADER_ARRAY);
        vm->heap[addr + HEAP_OFFSET_TYPE] = TYPE_ARRAY;
        vm->heap[addr + HEAP_OFFSET_LEN] = 0;
        vm_push((double)addr, T_OBJ);
        return;
    }

    int lines = 0;
    int ch;
    int last_char = '\n';

    while ((ch = fgetc(f)) != EOF) {
        if (ch == '\n') lines++;
        last_char = ch;
    }
    if (last_char != '\n' && ftell(f) > 0) lines++;

    int arr_addr = heap_alloc(lines + HEAP_HEADER_ARRAY);
    vm->heap[arr_addr + HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    vm->heap[arr_addr + HEAP_OFFSET_LEN] = (double)lines;

    rewind(f);
    char buffer[4096];
    int i = 0;
    while (i < lines && fgets(buffer, sizeof(buffer), f)) {
        trim_newline(buffer);
        int str_id = make_string(buffer);
        vm->heap[arr_addr + HEAP_HEADER_ARRAY + i] = (double)str_id;
        vm->heap_types[arr_addr + HEAP_HEADER_ARRAY + i] = T_STR;
        i++;
    }
    fclose(f);

    vm_push((double)arr_addr, T_OBJ);
}

void std_write_file(VM* vm) {
    double mode_id = vm_pop();
    double content_id = vm_pop();
    double path_id = vm_pop();

    const char* mode = get_str(vm, mode_id);
    const char* content = get_str(vm, content_id);
    const char* path = get_str(vm, path_id);

    if (strcmp(mode, "w") != 0 && strcmp(mode, "a") != 0) {
        printf("Runtime Error: write_file mode must be 'w' or 'a'\n");
        exit(1);
    }

    FILE* f = fopen(path, mode);
    if (!f) { vm_push(0.0, T_NUM); return; }

    fprintf(f, "%s", content);
    fclose(f);
    vm_push(1.0, T_NUM);
}

void std_read_bytes(VM* vm) {
    double stride_val = vm_pop();
    double path_id = vm_pop();
    const char* path = get_str(vm, path_id);
    int stride = (int)stride_val;

    if (stride != 1 && stride != 4) {
        printf("Runtime Error: read_bytes stride must be 1 (byte) or 4 (int)\n"); exit(1);
    }

    FILE* f = fopen(path, "rb");
    if (!f) {
        int addr = heap_alloc(HEAP_HEADER_ARRAY);
        vm->heap[addr + HEAP_OFFSET_TYPE] = TYPE_ARRAY;
        vm->heap[addr + HEAP_OFFSET_LEN] = 0;
        vm_push((double)addr, T_OBJ); return;
    }

    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char* buf = (unsigned char*)malloc(file_len);
    fread(buf, 1, file_len, f);
    fclose(f);

    int element_count = file_len / stride;
    int addr = heap_alloc(element_count + HEAP_HEADER_ARRAY);
    vm->heap[addr + HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    vm->heap[addr + HEAP_OFFSET_LEN] = (double)element_count;

    for (int i = 0; i < element_count; i++) {
        if (stride == 1) {
            vm->heap[addr + HEAP_HEADER_ARRAY + i] = (double)buf[i];
        } else {
            unsigned int val = buf[i*4] | (buf[i*4+1]<<8) | (buf[i*4+2]<<16) | (buf[i*4+3]<<24);
            vm->heap[addr + HEAP_HEADER_ARRAY + i] = (double)val;
        }
        vm->heap_types[addr + HEAP_HEADER_ARRAY + i] = T_NUM;
    }

    free(buf);
    vm_push((double)addr, T_OBJ);
}

void std_write_bytes(VM* vm) {
    double arr_ref = vm_pop();
    double path_id = vm_pop();

    int ptr = (int)arr_ref;
    if ((int)vm->heap[ptr + HEAP_OFFSET_TYPE] != TYPE_ARRAY) {
        printf("Runtime Error: write_bytes expects array\n"); exit(1);
    }

    const char* path = get_str(vm, path_id);
    int len = (int)vm->heap[ptr + HEAP_OFFSET_LEN];

    FILE* f = fopen(path, "wb");
    if (!f) { vm_push(0.0, T_NUM); return; }

    for(int i=0; i<len; i++) {
        unsigned char b = (unsigned char)vm->heap[ptr + HEAP_HEADER_ARRAY + i];
        fputc(b, f);
    }

    fclose(f);
    vm_push(1.0, T_NUM);
}