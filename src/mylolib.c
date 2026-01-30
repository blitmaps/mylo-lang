#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mylolib.h"
#include "vm.h"
#include "defines.h"

// --- Helpers ---

static const char *get_str(VM *vm, double val) {
    int id = (int) val;
    if (id < 0 || id >= vm->str_count) return "";
    return vm->string_pool[id];
}

void trim_newline(char *str) {
    int len = strlen(str);
    if (len > 0 && str[len - 1] == '\n') str[len - 1] = '\0';
    if (len > 1 && str[len - 2] == '\r') str[len - 2] = '\0';
}

// --- Standard Library Functions ---

void std_sqrt(VM *vm) {
    double val = vm_pop();
    vm_push(sqrt(val), T_NUM);
}

void std_sin(VM *vm) {
    double val = vm_pop();
    vm_push(sin(val), T_NUM);
}

void std_cos(VM *vm) {
    double val = vm_pop();
    vm_push(cos(val), T_NUM);
}

void std_tan(VM *vm) {
    double val = vm_pop();
    vm_push(tan(val), T_NUM);
}

void std_floor(VM *vm) {
    double val = vm_pop();
    vm_push(floor(val), T_NUM);
}

void std_ceil(VM *vm) {
    double val = vm_pop();
    vm_push(ceil(val), T_NUM);
}

void std_len(VM *vm) {
    double val = vm_pop();
    int ptr = (int) val;

    // Check if Array
    if (vm->stack_types[vm->sp + 1] == T_OBJ &&
        (int) vm->heap[ptr + HEAP_OFFSET_TYPE] == TYPE_ARRAY) {
        double length = vm->heap[ptr + HEAP_OFFSET_LEN];
        vm_push(length, T_NUM);
    } else if (vm->stack_types[vm->sp + 1] == T_STR) {
        const char *s = get_str(vm, val);
        vm_push((double) strlen(s), T_NUM);
    } else {
        printf("Runtime Error: len() expects array or string.\n");
        exit(1);
    }
}

void std_contains(VM *vm) {
    double needle_val = vm_pop();
    int needle_type = vm->stack_types[vm->sp + 1];
    double haystack_val = vm_pop();
    int haystack_type = vm->stack_types[vm->sp + 1];

    if (haystack_type == T_STR) {
        if (needle_type != T_STR) {
            printf("Runtime Error: contains() string needle\n");
            exit(1);
        }
        if (strstr(get_str(vm, haystack_val), get_str(vm, needle_val)) != NULL) vm_push(1.0, T_NUM);
        else vm_push(0.0, T_NUM);
        return;
    }

    if (haystack_type == T_OBJ) {
        int ptr = (int) haystack_val;
        int objType = (int) vm->heap[ptr + HEAP_OFFSET_TYPE];

        if (objType == TYPE_ARRAY) {
            int len = (int) vm->heap[ptr + HEAP_OFFSET_LEN];
            int found = 0;
            for (int i = 0; i < len; i++) {
                if (vm->heap_types[ptr + HEAP_HEADER_ARRAY + i] == needle_type && vm->heap[ptr + HEAP_HEADER_ARRAY + i]
                    == needle_val) {
                    found = 1;
                    break;
                }
            }
            vm_push(found ? 1.0 : 0.0, T_NUM);
            return;
        } else if (objType == TYPE_MAP) {
            if (needle_type != T_STR) {
                printf("Runtime Error: contains() map check requires string key\n");
                exit(1);
            }
            int count = (int) vm->heap[ptr + HEAP_OFFSET_COUNT];
            int dataPtr = (int) vm->heap[ptr + HEAP_OFFSET_DATA];
            int found = 0;
            for (int i = 0; i < count; i++) {
                if (vm->heap[dataPtr + i * 2] == needle_val) {
                    found = 1;
                    break;
                }
            }
            vm_push(found ? 1.0 : 0.0, T_NUM);
            return;
        }
    }
    printf("Runtime Error: contains() type\n");
    exit(1);
}

void std_to_string(VM *vm) {
    double val = vm_pop();
    int type = vm->stack_types[vm->sp + 1];

    if (type == T_NUM) {
        char buf[64];
        if (val == (int) val) snprintf(buf, 64, "%d", (int) val);
        else snprintf(buf, 64, "%g", val);

        int str_id = make_string(buf);
        vm_push((double) str_id, T_STR);
    } else if (type == T_STR) {
        vm_push(val, T_STR);
    } else if (type == T_OBJ) {
        char buf[64];
        snprintf(buf, 64, "[Ref: %d]", (int) val);
        int str_id = make_string(buf);
        vm_push((double) str_id, T_STR);
    } else {
        int str_id = make_string("");
        vm_push((double) str_id, T_STR);
    }
}

void std_to_num(VM *vm) {
    double val = vm_pop();
    int type = vm->stack_types[vm->sp + 1];

    if (type == T_NUM) {
        vm_push(val, T_NUM);
    } else if (type == T_STR) {
        const char *s = get_str(vm, val);
        char *end;
        double d = strtod(s, &end);
        vm_push(d, T_NUM);
    } else {
        vm_push(0.0, T_NUM);
    }
}

void std_read_lines(VM *vm) {
    double path_id = vm_pop();
    const char *path = get_str(vm, path_id);

    FILE *f = fopen(path, "r");
    if (!f) {
        int addr = heap_alloc(HEAP_HEADER_ARRAY);
        vm->heap[addr + HEAP_OFFSET_TYPE] = TYPE_ARRAY;
        vm->heap[addr + HEAP_OFFSET_LEN] = 0;
        vm_push((double) addr, T_OBJ);
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
    vm->heap[arr_addr + HEAP_OFFSET_LEN] = (double) lines;

    rewind(f);
    char buffer[4096];
    int i = 0;
    while (i < lines && fgets(buffer, sizeof(buffer), f)) {
        trim_newline(buffer);
        int str_id = make_string(buffer);
        vm->heap[arr_addr + HEAP_HEADER_ARRAY + i] = (double) str_id;
        vm->heap_types[arr_addr + HEAP_HEADER_ARRAY + i] = T_STR;
        i++;
    }
    fclose(f);

    vm_push((double) arr_addr, T_OBJ);
}

// Fixed: 3 Arguments (Mode, Content, Path)
void std_write_file(VM *vm) {
    double mode_id = vm_pop();
    double content_id = vm_pop();
    double path_id = vm_pop();

    const char *mode = get_str(vm, mode_id);
    const char *content = get_str(vm, content_id);
    const char *path = get_str(vm, path_id);

    if (strcmp(mode, "w") != 0 && strcmp(mode, "a") != 0) {
        printf("Runtime Error: write_file mode must be 'w' or 'a'\n");
        exit(1);
    }

    FILE *f = fopen(path, mode);
    if (!f) {
        vm_push(0.0, T_NUM);
        return;
    }

    fprintf(f, "%s", content);
    fclose(f);
    vm_push(1.0, T_NUM);
}

// Fixed: 2 Arguments (Stride, Path)
void std_read_bytes(VM *vm) {
    double stride_val = vm_pop();
    double path_id = vm_pop();
    const char *path = get_str(vm, path_id);
    int stride = (int) stride_val;

    if (stride != 1 && stride != 4) {
        printf("Runtime Error: read_bytes stride must be 1 (byte) or 4 (int)\n");
        exit(1);
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        int addr = heap_alloc(HEAP_HEADER_ARRAY);
        vm->heap[addr + HEAP_OFFSET_TYPE] = TYPE_ARRAY;
        vm->heap[addr + HEAP_OFFSET_LEN] = 0;
        vm_push((double) addr, T_OBJ);
        return;
    }

    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char *buf = (unsigned char *) malloc(file_len);
    fread(buf, 1, file_len, f);
    fclose(f);

    int element_count = file_len;
    int doubles_needed = (element_count + 7) / 8;
    int addr = heap_alloc(doubles_needed + HEAP_HEADER_ARRAY);

    vm->heap[addr + HEAP_OFFSET_TYPE] = TYPE_BYTES;
    vm->heap[addr + HEAP_OFFSET_LEN] = (double) element_count;

    char* heap_bytes = (char*)&vm->heap[addr + HEAP_HEADER_ARRAY];
    memcpy(heap_bytes, buf, element_count);

    free(buf);
    vm_push((double) addr, T_OBJ);
}

void std_write_bytes(VM *vm) {
    double arr_ref = vm_pop();
    double path_id = vm_pop();

    int ptr = (int) arr_ref;
    if ((int) vm->heap[ptr + HEAP_OFFSET_TYPE] != TYPE_ARRAY) {
        printf("Runtime Error: write_bytes expects array\n");
        exit(1);
    }

    const char *path = get_str(vm, path_id);
    int len = (int) vm->heap[ptr + HEAP_OFFSET_LEN];

    FILE *f = fopen(path, "wb");
    if (!f) {
        vm_push(0.0, T_NUM);
        return;
    }

    for (int i = 0; i < len; i++) {
        unsigned char b = (unsigned char) vm->heap[ptr + HEAP_HEADER_ARRAY + i];
        fputc(b, f);
    }

    fclose(f);
    vm_push(1.0, T_NUM);
}

// --- New Array/List Functions ---

// Usage: var x = list(100)
void std_list(VM *vm) {
    double size_val = vm_pop();
    int size = (int)size_val;

    if (size < 0) {
        printf("Runtime Error: list() size must be positive\n");
        exit(1);
    }

    int ptr = heap_alloc(size + HEAP_HEADER_ARRAY);
    vm->heap[ptr + HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    vm->heap[ptr + HEAP_OFFSET_LEN] = (double)size;

    // Initialize with 0.0
    for(int i = 0; i < size; i++) {
        vm->heap[ptr + HEAP_HEADER_ARRAY + i] = 0.0;
        vm->heap_types[ptr + HEAP_HEADER_ARRAY + i] = T_NUM;
    }

    vm_push((double)ptr, T_OBJ);
}

// Usage: remove(arr, index) or remove(map, key)
// Usage: remove(arr, index) or remove(map, key)
void std_remove(VM *vm) {
    double key_val = vm_pop();
    int key_type = vm->stack_types[vm->sp + 1];
    double obj_val = vm_pop();

    // Safety checks
    if (vm->stack_types[vm->sp + 1] != T_OBJ) {
        printf("Runtime Error: remove() expects an object\n");
        exit(1);
    }

    int ptr = (int)obj_val;
    int type = (int)vm->heap[ptr + HEAP_OFFSET_TYPE];

    if (type == TYPE_ARRAY) {
        int index = (int)key_val;
        int len = (int)vm->heap[ptr + HEAP_OFFSET_LEN];

        // Handle negative indexing
        if (index < 0) index += len;

        if (index >= 0 && index < len) {
            // Shift elements down from index+1 to end
            for (int i = index; i < len - 1; i++) {
                vm->heap[ptr + HEAP_HEADER_ARRAY + i] = vm->heap[ptr + HEAP_HEADER_ARRAY + i + 1];
                vm->heap_types[ptr + HEAP_HEADER_ARRAY + i] = vm->heap_types[ptr + HEAP_HEADER_ARRAY + i + 1];
            }
            // Decrement length
            vm->heap[ptr + HEAP_OFFSET_LEN] = (double)(len - 1);
        }
    }
    else if (type == TYPE_MAP) {
        int count = (int)vm->heap[ptr + HEAP_OFFSET_COUNT];
        int data = (int)vm->heap[ptr + HEAP_OFFSET_DATA];

        for (int i = 0; i < count; i++) {
            // Find key
            double k = vm->heap[data + i * 2];
            int kt = vm->heap_types[data + i * 2];

            // Note: String keys are compared by pointer ID here for simplicity
            if (k == key_val && kt == key_type) {
                // Shift remaining pairs down
                int pairs_to_move = count - 1 - i;
                if (pairs_to_move > 0) {
                    for(int j = 0; j < pairs_to_move * 2; j++) {
                        vm->heap[data + i * 2 + j] = vm->heap[data + (i + 1) * 2 + j];
                        vm->heap_types[data + i * 2 + j] = vm->heap_types[data + (i + 1) * 2 + j];
                    }
                }
                // Decrement count
                vm->heap[ptr + HEAP_OFFSET_COUNT] = (double)(count - 1);
                break;
            }
        }
    }

    // FIX: Return the object pointer so 'arr = remove(arr, 0)' works
    vm_push(obj_val, T_OBJ);
}
// Usage: weights = add(weights, 0, 56)
// Note: Returns a NEW array because standard arrays are fixed-size in heap.
void std_add(VM *vm) {
    double val = vm_pop();
    int val_type = vm->stack_types[vm->sp + 1];

    double idx_val = vm_pop();
    int idx = (int)idx_val;

    double arr_val = vm_pop();
    int ptr = (int)arr_val;

    if (vm->stack_types[vm->sp + 1] != T_OBJ || (int)vm->heap[ptr] != TYPE_ARRAY) {
        printf("Runtime Error: add() expects an array\n");
        exit(1);
    }

    int len = (int)vm->heap[ptr + HEAP_OFFSET_LEN];

    // Normalize index
    if (idx < 0) idx += len;
    if (idx < 0) idx = 0;
    if (idx > len) idx = len; // Append

    // Allocate new array with size + 1
    int new_ptr = heap_alloc(len + 1 + HEAP_HEADER_ARRAY);
    vm->heap[new_ptr + HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    vm->heap[new_ptr + HEAP_OFFSET_LEN] = (double)(len + 1);

    int src_base = ptr + HEAP_HEADER_ARRAY;
    int dst_base = new_ptr + HEAP_HEADER_ARRAY;

    // Copy 0 to index
    for(int i = 0; i < idx; i++) {
        vm->heap[dst_base + i] = vm->heap[src_base + i];
        vm->heap_types[dst_base + i] = vm->heap_types[src_base + i];
    }

    // Insert new value
    vm->heap[dst_base + idx] = val;
    vm->heap_types[dst_base + idx] = val_type;

    // Copy index to end
    for(int i = idx; i < len; i++) {
        vm->heap[dst_base + i + 1] = vm->heap[src_base + i];
        vm->heap_types[dst_base + i + 1] = vm->heap_types[src_base + i];
    }

    vm_push((double)new_ptr, T_OBJ);
}

// Usage: var parts = split("a,b,c", ",")
void std_split(VM* vm) {
    double del_val = vm_pop();
    double str_val = vm_pop();

    int del_type = vm->stack_types[vm->sp + 2]; // Peek types derived from pop order
    int str_type = vm->stack_types[vm->sp + 1];

    // Since we already popped, we rely on the internal knowledge that 
    // arguments are pushed: [STR, DELIM] -> Pop DELIM, Pop STR.
    // Safety check:
    const char* str = get_str(vm, str_val);
    const char* del = get_str(vm, del_val);
    int del_len = strlen(del);

    if (del_len == 0) {
        // Edge case: Empty delimiter. Return array of individual chars?
        // For simplicity, let's return [str] or error. 
        // Standard behavior often splits every char. Let's do that.
        int len = strlen(str);
        int ptr = heap_alloc(len + HEAP_HEADER_ARRAY);
        vm->heap[ptr] = TYPE_ARRAY;
        vm->heap[ptr + 1] = (double)len;

        for (int i = 0; i < len; i++) {
            char tmp[2] = { str[i], '\0' };
            int id = make_string(tmp);
            vm->heap[ptr + 2 + i] = (double)id;
            vm->heap_types[ptr + 2 + i] = T_STR;
        }
        vm_push((double)ptr, T_OBJ);
        return;
    }

    // 1. Count tokens
    int count = 1;
    const char* p = str;
    while ((p = strstr(p, del)) != NULL) {
        count++;
        p += del_len;
    }

    // 2. Allocate Array
    int ptr = heap_alloc(count + HEAP_HEADER_ARRAY);
    vm->heap[ptr] = TYPE_ARRAY;
    vm->heap[ptr + 1] = (double)count;

    // 3. Fill Tokens
    int idx = 0;
    p = str;
    const char* next;
    while ((next = strstr(p, del)) != NULL) {
        int token_len = next - p;
        char* buf = malloc(token_len + 1);
        strncpy(buf, p, token_len);
        buf[token_len] = '\0';

        int id = make_string(buf);
        free(buf);

        vm->heap[ptr + 2 + idx] = (double)id;
        vm->heap_types[ptr + 2 + idx] = T_STR;
        idx++;
        p = next + del_len;
    }

    // Last token
    int id = make_string(p);
    vm->heap[ptr + 2 + idx] = (double)id;
    vm->heap_types[ptr + 2 + idx] = T_STR;

    vm_push((double)ptr, T_OBJ);
}

// Usage: var idx = where(collection, item)
void std_where(VM* vm) {
    double item_val = vm_pop();
    int item_type = vm->stack_types[vm->sp + 1]; // Actually +1 because we already popped 1
    // But to be safe on types, let's look at what we just popped.
    // vm_pop decrements SP. So types are at vm->stack_types[vm->sp + 1]

    double col_val = vm_pop();
    int col_type = vm->stack_types[vm->sp + 1];

    if (col_type == T_STR) {
        // String Search
        if (item_type != T_STR) {
            vm_push(-1.0, T_NUM);
            return;
        }
        const char* haystack = get_str(vm, col_val);
        const char* needle = get_str(vm, item_val);

        char* found = strstr(haystack, needle);
        if (found) {
            vm_push((double)(found - haystack), T_NUM);
        }
        else {
            vm_push(-1.0, T_NUM);
        }
    }
    else if (col_type == T_OBJ) {
        // Array Search
        int ptr = (int)col_val;
        int type = (int)vm->heap[ptr];

        if (type == TYPE_ARRAY) {
            int len = (int)vm->heap[ptr + 1];
            for (int i = 0; i < len; i++) {
                double el = vm->heap[ptr + 2 + i];
                int et = vm->heap_types[ptr + 2 + i];

                // Strict equality check
                if (el == item_val && et == item_type) {
                    vm_push((double)i, T_NUM);
                    return;
                }
            }
        }
        else if (type == TYPE_BYTES) {
            // Byte Search
            int len = (int)vm->heap[ptr + 1];
            unsigned char* b = (unsigned char*)&vm->heap[ptr + HEAP_HEADER_ARRAY];
            for (int i = 0; i < len; i++) {
                if ((double)b[i] == item_val) {
                    vm_push((double)i, T_NUM);
                    return;
                }
            }
        }
        // Not found in array/bytes
        vm_push(-1.0, T_NUM);
    }
    else {
        vm_push(-1.0, T_NUM);
    }
}

// --- Registry Definition ---
// Moved from header to here
const StdLibDef std_library[] = {
    {"len", std_len, "num", 1, {"any"}},
    {"contains", std_contains, "num", 2, {"any", "any"}},
    {"to_string", std_to_string, "str", 1, {"any"}},
    {"to_num", std_to_num, "num", 1, {"any"}},
    {"sqrt", std_sqrt, "num", 1, {"num"}},
    {"sin", std_sin, "num", 1, {"num"}},
    {"cos", std_cos, "num", 1, {"num"}},
    {"tan", std_tan, "num", 1, {"num"}},
    {"floor", std_floor, "num", 1, {"num"}},
    {"ceil", std_ceil, "num", 1, {"num"}},
    {"read_lines", std_read_lines, "arr", 1, {"str"}},
    // Updated arg counts to match your tests
    {"write_file", std_write_file, "num", 3, {"str", "str", "str"}},
    {"read_bytes", std_read_bytes, "arr", 2, {"str", "num"}},
    {"write_bytes", std_write_bytes, "num", 2, {"str", "arr"}},
    {"list", std_list, "arr", 1, {"num"}},
    {"remove", std_remove, "void", 2, {"any", "any"}},
    {"add", std_add, "arr", 3, {"arr", "num", "any"}},
    {"split", std_split, "arr", 2, {"str", "str"}},
    {"where", std_where, "num", 2, {"any", "any"}},
    {NULL, NULL, NULL, 0, {NULL}}
};