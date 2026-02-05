#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mylolib.h"
#include "vm.h"
#include "defines.h"
#include "os_job.h"

#ifdef _WIN32
    #include <io.h>
    #include <windows.h>
#else
    #include <dirent.h>
#endif
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

void std_sqrt(VM *vm) { vm_push(sqrt(vm_pop()), T_NUM); }
void std_sin(VM *vm) { vm_push(sin(vm_pop()), T_NUM); }
void std_cos(VM *vm) { vm_push(cos(vm_pop()), T_NUM); }
void std_tan(VM *vm) { vm_push(tan(vm_pop()), T_NUM); }
void std_floor(VM *vm) { vm_push(floor(vm_pop()), T_NUM); }
void std_ceil(VM *vm) { vm_push(ceil(vm_pop()), T_NUM); }

void std_len(VM *vm) {
    double val = vm_pop();

    if (vm->stack_types[vm->sp + 1] == T_OBJ) {
        double* base = vm_resolve_ptr(val);
        if(!base) { vm_push(0, T_NUM); return; }

        int type = (int)base[HEAP_OFFSET_TYPE];

        // Support Generic Arrays, Byte Arrays, and Typed Arrays (i32[], f32[], etc)
        if(type == TYPE_ARRAY || type == TYPE_BYTES || (type <= TYPE_I16_ARRAY && type >= TYPE_BOOL_ARRAY)) {
            vm_push(base[HEAP_OFFSET_LEN], T_NUM);
        }
        // Support Maps (Count is at offset 2)
        else if (type == TYPE_MAP) {
            vm_push(base[HEAP_OFFSET_COUNT], T_NUM);
        }
        else {
            printf("Runtime Error: len() expects array, string, map, or bytes.\n");
            exit(1);
        }
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
        double* base = vm_resolve_ptr(haystack_val);
        int* types = vm_resolve_type(haystack_val);
        if (!base) { vm_push(0.0, T_NUM); return; }

        int objType = (int)base[HEAP_OFFSET_TYPE];

        if (objType == TYPE_ARRAY) {
            int len = (int)base[HEAP_OFFSET_LEN];
            int found = 0;
            for (int i = 0; i < len; i++) {
                if (types[HEAP_HEADER_ARRAY + i] == needle_type && base[HEAP_HEADER_ARRAY + i] == needle_val) {
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
            int count = (int)base[HEAP_OFFSET_COUNT];
            double dataPtrVal = base[HEAP_OFFSET_DATA];
            double* data = vm_resolve_ptr(dataPtrVal);

            int found = 0;
            if(data) {
                for (int i = 0; i < count; i++) {
                    if (data[i * 2] == needle_val) {
                        found = 1;
                        break;
                    }
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
        // Can't print address effectively with packed pointer, just print type indicator
        char buf[64];
        snprintf(buf, 64, "[Object]");
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
        double addr = heap_alloc(HEAP_HEADER_ARRAY);
        double* base = vm_resolve_ptr(addr);
        base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
        base[HEAP_OFFSET_LEN] = 0;
        vm_push(addr, T_OBJ);
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

    double arr_addr = heap_alloc(lines + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(arr_addr);
    int* types = vm_resolve_type(arr_addr);

    base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    base[HEAP_OFFSET_LEN] = (double) lines;

    rewind(f);
    char buffer[4096];
    int i = 0;
    while (i < lines && fgets(buffer, sizeof(buffer), f)) {
        trim_newline(buffer);
        int str_id = make_string(buffer);
        base[HEAP_HEADER_ARRAY + i] = (double) str_id;
        types[HEAP_HEADER_ARRAY + i] = T_STR;
        i++;
    }
    fclose(f);

    vm_push(arr_addr, T_OBJ);
}

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
        double addr = heap_alloc(HEAP_HEADER_ARRAY);
        double* base = vm_resolve_ptr(addr);
        base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
        base[HEAP_OFFSET_LEN] = 0;
        vm_push(addr, T_OBJ);
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
    double addr = heap_alloc(doubles_needed + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(addr);

    base[HEAP_OFFSET_TYPE] = TYPE_BYTES;
    base[HEAP_OFFSET_LEN] = (double) element_count;

    char* heap_bytes = (char*)&base[HEAP_HEADER_ARRAY];
    memcpy(heap_bytes, buf, element_count);

    free(buf);
    vm_push(addr, T_OBJ);
}

void std_write_bytes(VM *vm) {
    double arr_ref = vm_pop();
    double path_id = vm_pop();

    double* base = vm_resolve_ptr(arr_ref);
    if (!base || (int) base[HEAP_OFFSET_TYPE] != TYPE_ARRAY) {
        printf("Runtime Error: write_bytes expects array\n");
        exit(1);
    }

    const char *path = get_str(vm, path_id);
    int len = (int) base[HEAP_OFFSET_LEN];

    FILE *f = fopen(path, "wb");
    if (!f) {
        vm_push(0.0, T_NUM);
        return;
    }

    for (int i = 0; i < len; i++) {
        unsigned char b = (unsigned char) base[HEAP_HEADER_ARRAY + i];
        fputc(b, f);
    }

    fclose(f);
    vm_push(1.0, T_NUM);
}

// Usage: var x = list(100)
void std_list(VM *vm) {
    double size_val = vm_pop();
    int size = (int)size_val;

    if (size < 0) {
        printf("Runtime Error: list() size must be positive\n");
        exit(1);
    }

    double ptr = heap_alloc(size + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(ptr);
    int* types = vm_resolve_type(ptr);

    base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    base[HEAP_OFFSET_LEN] = (double)size;

    for(int i = 0; i < size; i++) {
        base[HEAP_HEADER_ARRAY + i] = 0.0;
        types[HEAP_HEADER_ARRAY + i] = T_NUM;
    }

    vm_push(ptr, T_OBJ);
}

void std_remove(VM *vm) {
    double key_val = vm_pop();
    int key_type = vm->stack_types[vm->sp + 1];
    double obj_val = vm_pop();

    if (vm->stack_types[vm->sp + 1] != T_OBJ) {
        printf("Runtime Error: remove() expects an object\n");
        exit(1);
    }

    double* base = vm_resolve_ptr(obj_val);
    int* types = vm_resolve_type(obj_val);
    int type = (int)base[HEAP_OFFSET_TYPE];

    if (type == TYPE_ARRAY) {
        int index = (int)key_val;
        int len = (int)base[HEAP_OFFSET_LEN];

        if (index < 0) index += len;

        if (index >= 0 && index < len) {
            for (int i = index; i < len - 1; i++) {
                base[HEAP_HEADER_ARRAY + i] = base[HEAP_HEADER_ARRAY + i + 1];
                types[HEAP_HEADER_ARRAY + i] = types[HEAP_HEADER_ARRAY + i + 1];
            }
            base[HEAP_OFFSET_LEN] = (double)(len - 1);
        }
    }
    else if (type == TYPE_MAP) {
        int count = (int)base[HEAP_OFFSET_COUNT];
        double data_ptr_val = base[HEAP_OFFSET_DATA];
        double* data = vm_resolve_ptr(data_ptr_val);
        int* data_types = vm_resolve_type(data_ptr_val);

        if(data) {
            for (int i = 0; i < count; i++) {
                double k = data[i * 2];
                int kt = data_types[i * 2];

                if (k == key_val && kt == key_type) {
                    int pairs_to_move = count - 1 - i;
                    if (pairs_to_move > 0) {
                        for(int j = 0; j < pairs_to_move * 2; j++) {
                            data[i * 2 + j] = data[(i + 1) * 2 + j];
                            data_types[i * 2 + j] = data_types[(i + 1) * 2 + j];
                        }
                    }
                    base[HEAP_OFFSET_COUNT] = (double)(count - 1);
                    break;
                }
            }
        }
    }
    vm_push(obj_val, T_OBJ);
}

void std_add(VM *vm) {
    double val = vm_pop();
    int val_type = vm->stack_types[vm->sp + 1];

    double idx_val = vm_pop();
    int idx = (int)idx_val;

    double arr_val = vm_pop();

    double* base = vm_resolve_ptr(arr_val);
    int* types = vm_resolve_type(arr_val);

    if (vm->stack_types[vm->sp + 1] != T_OBJ || (int)base[0] != TYPE_ARRAY) {
        printf("Runtime Error: add() expects an array\n");
        exit(1);
    }

    int len = (int)base[HEAP_OFFSET_LEN];

    if (idx < 0) idx += len;
    if (idx < 0) idx = 0;
    if (idx > len) idx = len;

    double new_ptr = heap_alloc(len + 1 + HEAP_HEADER_ARRAY);
    double* new_base = vm_resolve_ptr(new_ptr);
    int* new_types = vm_resolve_type(new_ptr);

    new_base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    new_base[HEAP_OFFSET_LEN] = (double)(len + 1);

    int src_base_idx = HEAP_HEADER_ARRAY;
    int dst_base_idx = HEAP_HEADER_ARRAY;

    for(int i = 0; i < idx; i++) {
        new_base[dst_base_idx + i] = base[src_base_idx + i];
        new_types[dst_base_idx + i] = types[src_base_idx + i];
    }

    new_base[dst_base_idx + idx] = val;
    new_types[dst_base_idx + idx] = val_type;

    for(int i = idx; i < len; i++) {
        new_base[dst_base_idx + i + 1] = base[src_base_idx + i];
        new_types[dst_base_idx + i + 1] = types[src_base_idx + i];
    }

    vm_push(new_ptr, T_OBJ);
}

void std_split(VM* vm) {
    double del_val = vm_pop();
    double str_val = vm_pop();

    const char* str = get_str(vm, str_val);
    const char* del = get_str(vm, del_val);
    int del_len = strlen(del);

    if (del_len == 0) {
        int len = strlen(str);
        double ptr = heap_alloc(len + HEAP_HEADER_ARRAY);
        double* base = vm_resolve_ptr(ptr);
        int* types = vm_resolve_type(ptr);

        base[0] = TYPE_ARRAY;
        base[1] = (double)len;

        for (int i = 0; i < len; i++) {
            char tmp[2] = { str[i], '\0' };
            int id = make_string(tmp);
            base[2 + i] = (double)id;
            types[2 + i] = T_STR;
        }
        vm_push(ptr, T_OBJ);
        return;
    }

    int count = 1;
    const char* p = str;
    while ((p = strstr(p, del)) != NULL) {
        count++;
        p += del_len;
    }

    double ptr = heap_alloc(count + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(ptr);
    int* types = vm_resolve_type(ptr);

    base[0] = TYPE_ARRAY;
    base[1] = (double)count;

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

        base[2 + idx] = (double)id;
        types[2 + idx] = T_STR;
        idx++;
        p = next + del_len;
    }

    int id = make_string(p);
    base[2 + idx] = (double)id;
    types[2 + idx] = T_STR;

    vm_push(ptr, T_OBJ);
}

void std_where(VM* vm) {
    double item_val = vm_pop();
    int item_type = vm->stack_types[vm->sp + 1];
    double col_val = vm_pop();
    int col_type = vm->stack_types[vm->sp + 1];

    if (col_type == T_STR) {
        if (item_type != T_STR) { vm_push(-1.0, T_NUM); return; }
        const char* haystack = get_str(vm, col_val);
        const char* needle = get_str(vm, item_val);
        char* found = strstr(haystack, needle);
        if (found) vm_push((double)(found - haystack), T_NUM);
        else vm_push(-1.0, T_NUM);
    }
    else if (col_type == T_OBJ) {
        double* base = vm_resolve_ptr(col_val);
        int* types = vm_resolve_type(col_val);
        if(!base) { vm_push(-1.0, T_NUM); return; }

        int type = (int)base[0];

        if (type == TYPE_ARRAY) {
            int len = (int)base[1];
            for (int i = 0; i < len; i++) {
                double el = base[2 + i];
                int et = types[2 + i];
                if (el == item_val && et == item_type) {
                    vm_push((double)i, T_NUM);
                    return;
                }
            }
        }
        else if (type == TYPE_BYTES) {
            int len = (int)base[1];
            unsigned char* b = (unsigned char*)&base[HEAP_HEADER_ARRAY];
            for (int i = 0; i < len; i++) {
                if ((double)b[i] == item_val) {
                    vm_push((double)i, T_NUM);
                    return;
                }
            }
        }
        vm_push(-1.0, T_NUM);
    }
    else {
        vm_push(-1.0, T_NUM);
    }
}

void std_range(VM *vm) {
    double stop_val = vm_pop();
    double step_val = vm_pop();
    double start_val = vm_pop();

    if (step_val == 0) {
        printf("Runtime Error: range() step cannot be 0\n");
        exit(1);
    }

    double abs_step = fabs(step_val);
    if (abs_step == 0) abs_step = 1.0;

    double diff = fabs(stop_val - start_val);
    int count = (int)((diff / abs_step) + 1.00000001);
    if (count < 0) count = 0;

    double ptr = heap_alloc(count + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(ptr);
    int* types = vm_resolve_type(ptr);

    base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    base[HEAP_OFFSET_LEN] = (double)count;

    double current = start_val;
    bool ascending = (start_val <= stop_val);

    for (int i = 0; i < count; i++) {
        base[HEAP_HEADER_ARRAY + i] = current;
        types[HEAP_HEADER_ARRAY + i] = T_NUM;
        if (ascending) current += abs_step;
        else current -= abs_step;
    }

    vm_push(ptr, T_OBJ);
}

void std_for_list(VM *vm) {
    double list_ref = vm_pop();
    double func_val = vm_pop();

    double* base = vm_resolve_ptr(list_ref);
    int* types = vm_resolve_type(list_ref);

    if (vm->stack_types[vm->sp + 2] != T_OBJ || (int)base[0] != TYPE_ARRAY) {
        printf("Runtime Error: for_list expects an array.\n");
        exit(1);
    }
    if (vm->stack_types[vm->sp + 1] != T_STR) {
        printf("Runtime Error: for_list expects a function name.\n");
        exit(1);
    }

    const char* func_name = get_str(vm, func_val);
    int len = (int)base[HEAP_OFFSET_LEN];

    NativeFunc native_target = NULL;
    for(int i=0; std_library[i].name != NULL; i++) {
        if(strcmp(std_library[i].name, func_name) == 0) {
            native_target = std_library[i].func;
            break;
        }
    }

    int user_func_addr = -1;
    if (!native_target) {
        user_func_addr = vm_find_function(vm, func_name);
    }

    if (!native_target && user_func_addr == -1) {
        printf("Runtime Error: for_list could not find function '%s'\n", func_name);
        exit(1);
    }

    int saved_ip = vm->ip;

    double res_ptr = heap_alloc(len + HEAP_HEADER_ARRAY);
    double* res_base = vm_resolve_ptr(res_ptr);
    int* res_types = vm_resolve_type(res_ptr);

    res_base[0] = TYPE_ARRAY;
    res_base[1] = (double)len;

    for(int i=0; i<len; i++) {
         // Reload base/types inside loop in case allocation moved things (if using realloc, but we aren't yet)
         // But context switch might happen? No, stdlib is atomic.
         double val = base[HEAP_HEADER_ARRAY + i];
         int type = types[HEAP_HEADER_ARRAY + i];

         if (native_target) {
             vm_push(val, type);
             native_target(vm);
         } else {
             vm_push((double)vm->code_size, T_NUM);
             vm_push((double)vm->fp, T_NUM);
             vm_push(val, type);
             vm->fp = vm->sp;

             run_vm_from(user_func_addr, false);
         }

         double res = vm_pop();
         int res_type = vm->stack_types[vm->sp + 1];

         res_base[HEAP_HEADER_ARRAY + i] = res;
         res_types[HEAP_HEADER_ARRAY + i] = res_type;
    }

    vm->ip = saved_ip;
    vm_push(res_ptr, T_OBJ);
}

void std_seed(VM *vm) {
    double val = vm_pop();
    srand((unsigned int)val);
    vm_push(0.0, T_NUM);
}

void std_rand(VM *vm) {
    double r = (double)rand() / (double)RAND_MAX;
    vm_push(r, T_NUM);
}

void std_rand_normal(VM *vm) {
    double u1 = (double)rand() / (double)RAND_MAX;
    double u2 = (double)rand() / (double)RAND_MAX;
    if (u1 < 1e-9) u1 = 1e-9;
    double z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
    vm_push(z0, T_NUM);
}

void std_mix(VM *vm) {
    double a = vm_pop();
    double y = vm_pop();
    double x = vm_pop();
    double result = x + (y - x) * a;
    vm_push(result, T_NUM);
}

void std_min(VM *vm) {
    double b = vm_pop();
    double a = vm_pop();
    if (a < b) vm_push(a, T_NUM);
    else vm_push(b, T_NUM);
}

void std_max(VM *vm) {
    double b = vm_pop();
    double a = vm_pop();
    if (a > b) vm_push(a, T_NUM);
    else vm_push(b, T_NUM);
}

void std_dist(VM *vm) {
    double y2 = vm_pop();
    double x2 = vm_pop();
    double y1 = vm_pop();
    double x1 = vm_pop();
    double dx = x2 - x1;
    double dy = y2 - y1;
    double dist = sqrt(dx*dx + dy*dy);
    vm_push(dist, T_NUM);
}

void std_list_min(VM *vm) {
    double list_ref = vm_pop();
    double* base = vm_resolve_ptr(list_ref);

    if (vm->stack_types[vm->sp + 1] != T_OBJ || (int)base[0] != TYPE_ARRAY) {
        printf("Runtime Error: list_min() expects an array.\n");
        exit(1);
    }

    int len = (int)base[HEAP_OFFSET_LEN];
    if (len == 0) {
        printf("Runtime Error: list_min() called on empty array.\n");
        exit(1);
    }

    double min_val = base[HEAP_HEADER_ARRAY + 0];
    for (int i = 1; i < len; i++) {
        double val = base[HEAP_HEADER_ARRAY + i];
        if (val < min_val) min_val = val;
    }
    vm_push(min_val, T_NUM);
}

void std_list_max(VM *vm) {
    double list_ref = vm_pop();
    double* base = vm_resolve_ptr(list_ref);

    if (vm->stack_types[vm->sp + 1] != T_OBJ || (int)base[0] != TYPE_ARRAY) {
        printf("Runtime Error: list_max() expects an array.\n");
        exit(1);
    }

    int len = (int)base[HEAP_OFFSET_LEN];
    if (len == 0) {
        printf("Runtime Error: list_max() called on empty array.\n");
        exit(1);
    }

    double max_val = base[HEAP_HEADER_ARRAY + 0];
    for (int i = 1; i < len; i++) {
        double val = base[HEAP_HEADER_ARRAY + i];
        if (val > max_val) max_val = val;
    }
    vm_push(max_val, T_NUM);
}

// --- Perlin Noise Internals ---
static int perlin_p[512] = {
   151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
   190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,
   125,136,171,168, 68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
   105,92,41,55,46,245,40,244,102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,
   196,135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,5,202,38,147,118,126,
   255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152, 2,44,154,163, 70,
   221,153,101,155,167, 43,172,9,129,22,39,253, 19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
   228,251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,49,192,214, 31,181,199,
   106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,
   195,78,66,215,61,156,180,
   // Repeat
   151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
   190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,
   125,136,171,168, 68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
   105,92,41,55,46,245,40,244,102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,
   196,135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,5,202,38,147,118,126,
   255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152, 2,44,154,163, 70,
   221,153,101,155,167, 43,172,9,129,22,39,253, 19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
   228,251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,49,192,214, 31,181,199,
   106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,
   195,78,66,215,61,156,180
};

static double perlin_fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static double perlin_lerp(double t, double a, double b) { return a + t * (b - a); }
static double perlin_grad(int hash, double x, double y, double z) {
    int h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

void std_noise(VM *vm) {
    double z = vm_pop();
    double y = vm_pop();
    double x = vm_pop();

    int X = (int)floor(x) & 255;
    int Y = (int)floor(y) & 255;
    int Z = (int)floor(z) & 255;

    x -= floor(x);
    y -= floor(y);
    z -= floor(z);

    double u = perlin_fade(x);
    double v = perlin_fade(y);
    double w = perlin_fade(z);

    int A = perlin_p[X] + Y, AA = perlin_p[A] + Z, AB = perlin_p[A + 1] + Z;
    int B = perlin_p[X + 1] + Y, BA = perlin_p[B] + Z, BB = perlin_p[B + 1] + Z;

    double res = perlin_lerp(w, perlin_lerp(v, perlin_lerp(u, perlin_grad(perlin_p[AA], x, y, z),
                                         perlin_grad(perlin_p[BA], x - 1, y, z)),
                                 perlin_lerp(u, perlin_grad(perlin_p[AB], x, y - 1, z),
                                         perlin_grad(perlin_p[BB], x - 1, y - 1, z))),
                         perlin_lerp(v, perlin_lerp(u, perlin_grad(perlin_p[AA + 1], x, y, z - 1),
                                         perlin_grad(perlin_p[BA + 1], x - 1, y, z - 1)),
                                 perlin_lerp(u, perlin_grad(perlin_p[AB + 1], x, y - 1, z - 1),
                                         perlin_grad(perlin_p[BB + 1], x - 1, y - 1, z - 1))));

    vm_push(res, T_NUM);
}


// Helper to check if string ends with suffix
static int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr) return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

void std_list_dir(VM *vm) {
    // Mylo pops arguments in reverse order of how they are passed
    double filter_id = vm_pop();
    const char *filter = get_str(vm, filter_id);
    
    double path_id = vm_pop();
    const char *path = get_str(vm, path_id);
    
    int count = 0;
    int capacity = 16;
    char **filenames = malloc(sizeof(char*) * capacity);

#ifdef _WIN32
    char search_path[MAX_PATH];
    snprintf(search_path, MAX_PATH, "%s\\*", path);

    WIN32_FIND_DATA fFD;
    HANDLE hFind = FindFirstFile(search_path, &fFD);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fFD.cFileName, ".") == 0 || strcmp(fFD.cFileName, "..") == 0) continue;
            
            // Apply filter logic
            if (strlen(filter) > 0 && !ends_with(fFD.cFileName, filter)) continue;

            if (count >= capacity) {
                capacity *= 2;
                filenames = realloc(filenames, sizeof(char*) * capacity);
            }
            filenames[count++] = _strdup(fFD.cFileName);
        } while (FindNextFile(hFind, &fFD));
        FindClose(hFind);
    }
#else
    DIR *d = opendir(path);
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;

            // Apply filter logic
            if (strlen(filter) > 0 && !ends_with(dir->d_name, filter)) continue;

            if (count >= capacity) {
                capacity *= 2;
                filenames = realloc(filenames, sizeof(char*) * capacity);
            }
            filenames[count++] = strdup(dir->d_name);
        }
        closedir(d);
    }
#endif

    // Push the results to the Mylo Heap
    double arr_addr = heap_alloc(count + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(arr_addr);
    int* types = vm_resolve_type(arr_addr);

    base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    base[HEAP_OFFSET_LEN] = (double)count;

    for (int i = 0; i < count; i++) {
        int id = make_string(filenames[i]);
        base[HEAP_HEADER_ARRAY + i] = (double)id;
        types[HEAP_HEADER_ARRAY + i] = T_STR;
        free(filenames[i]);
    }
    free(filenames);

    vm_push(arr_addr, T_OBJ);
}


// Helper to read a whole file into a string
static char* read_whole_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if(!f) return strdup("");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(sz + 1);
    if(buf) {
        fread(buf, 1, sz, f);
        buf[sz] = '\0';
    }
    fclose(f);
    return buf ? buf : strdup("");
}

// Platform agnostic execution logic
void internal_exec_command(const char* cmd, char** out_str, char** err_str) {
    char stderr_path[256];
    char actual_cmd[4096];

    // 1. Generate unique temp file for stderr
    // Using a random ID to avoid collisions
    int rnd = rand();
#ifdef _WIN32
    sprintf(stderr_path, "%s\\mylo_err_%d.tmp", getenv("TEMP"), rnd);
    // cmd 2> temp_file
    snprintf(actual_cmd, sizeof(actual_cmd), "%s 2> \"%s\"", cmd, stderr_path);
#else
    sprintf(stderr_path, "/tmp/mylo_err_%d.tmp", rnd);
    snprintf(actual_cmd, sizeof(actual_cmd), "%s 2> %s", cmd, stderr_path);
#endif

    // 2. Run with popen to capture stdout directly
    FILE *fp = popen(actual_cmd, "r");

    // 3. Read stdout
    size_t out_cap = 1024;
    size_t out_len = 0;
    char *out_buf = malloc(out_cap);
    out_buf[0] = '\0';

    if (fp) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            size_t chunk_len = strlen(buffer);
            if (out_len + chunk_len >= out_cap) {
                out_cap *= 2;
                out_buf = realloc(out_buf, out_cap);
            }
            strcpy(out_buf + out_len, buffer);
            out_len += chunk_len;
        }
        pclose(fp);
    }
    *out_str = out_buf;

    // 4. Read stderr from temp file
    *err_str = read_whole_file(stderr_path);

    // 5. Cleanup
    remove(stderr_path);
}

// Thread Worker Entry Point
#ifdef _WIN32
unsigned __stdcall job_worker(void* arg) {
#else
void* job_worker(void* arg) {
#endif
    Job* job = (Job*)arg;

    char *o = NULL;
    char *e = NULL;
    internal_exec_command(job->cmd, &o, &e);

    LOCK_JOBS;
    job->out_res = o;
    job->err_res = e;
    job->status = 1; // Done
    UNLOCK_JOBS;

    free(job->cmd); // Free the command string copy
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

// --- 1. System (Blocking) ---
void std_system(VM *vm) {
    double cmd_id = vm_pop();
    const char *cmd = get_str(vm, cmd_id);

    char *out_str = NULL;
    char *err_str = NULL;

    internal_exec_command(cmd, &out_str, &err_str);

    // Create Mylo Array [stdout, stderr]
    double arr_ptr = heap_alloc(2 + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(arr_ptr);
    int* types = vm_resolve_type(arr_ptr);

    base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    base[HEAP_OFFSET_LEN] = 2.0;

    int id_out = make_string(out_str ? out_str : "");
    int id_err = make_string(err_str ? err_str : "");

    base[HEAP_HEADER_ARRAY + 0] = (double)id_out;
    types[HEAP_HEADER_ARRAY + 0] = T_STR;

    base[HEAP_HEADER_ARRAY + 1] = (double)id_err;
    types[HEAP_HEADER_ARRAY + 1] = T_STR;

    if(out_str) free(out_str);
    if(err_str) free(err_str);

    vm_push(arr_ptr, T_OBJ);
}

// --- 2. System Thread (Non-Blocking) ---
void std_system_thread(VM *vm) {
    double name_id = vm_pop();
    double cmd_id = vm_pop();
    const char *name = get_str(vm, name_id);
    const char *cmd = get_str(vm, cmd_id);

    LOCK_JOBS;
    int slot = -1;
    for(int i=0; i<MAX_JOBS; i++) {
        if(!job_registry[i].active) {
            slot = i;
            break;
        }
    }

    if(slot == -1) {
        UNLOCK_JOBS;
        vm_push(0.0, T_NUM); // Failed (No slots)
        return;
    }

    // Setup Job
    strncpy(job_registry[slot].name, name, 63);
    job_registry[slot].cmd = strdup(cmd);
    job_registry[slot].out_res = NULL;
    job_registry[slot].err_res = NULL;
    job_registry[slot].status = 0; // Running
    job_registry[slot].active = 1;

    // Spawn Thread
#ifdef _WIN32
    unsigned threadID;
    job_registry[slot].thread = (HANDLE)_beginthreadex(NULL, 0, &job_worker, &job_registry[slot], 0, &threadID);
#else
    pthread_create(&job_registry[slot].thread, NULL, &job_worker, &job_registry[slot]);
    pthread_detach(job_registry[slot].thread); // Detach so we don't need to join
#endif

    UNLOCK_JOBS;
    vm_push(1.0, T_NUM); // Success
}

// --- 3. Get Job (Poll Status) ---
void std_get_job(VM *vm) {
    double name_id = vm_pop();
    const char *name = get_str(vm, name_id);

    LOCK_JOBS;
    int slot = -1;
    for(int i=0; i<MAX_JOBS; i++) {
        if(job_registry[i].active && strcmp(job_registry[i].name, name) == 0) {
            slot = i;
            break;
        }
    }

    // Case 1: Job not found
    if(slot == -1) {
        UNLOCK_JOBS;
        vm_push(-1.0, T_NUM);
        return;
    }

    // Case 2: Still Running
    if(job_registry[slot].status == 0) {
        UNLOCK_JOBS;
        vm_push(1.0, T_NUM);
        return;
    }

    // Case 3: Done - Return [out, err] and clean up
    char* o = job_registry[slot].out_res;
    char* e = job_registry[slot].err_res;

    // Make Mylo Array
    // NOTE: We must unlock before Allocating to prevent heap locks (if heap used mutexes later)
    // But here we need to copy strings to VM first.
    // For safety, we will grab the raw C strings, clear the job, unlock, THEN alloc VM memory.

    char* safe_o = o ? strdup(o) : strdup("");
    char* safe_e = e ? strdup(e) : strdup("");

    // Cleanup registry slot
    if(job_registry[slot].out_res) free(job_registry[slot].out_res);
    if(job_registry[slot].err_res) free(job_registry[slot].err_res);
    job_registry[slot].active = 0; // Free the slot

    UNLOCK_JOBS;

    // Now safe to alloc on VM Heap
    double arr_ptr = heap_alloc(2 + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(arr_ptr);
    int* types = vm_resolve_type(arr_ptr);

    base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    base[HEAP_OFFSET_LEN] = 2.0;

    int id_out = make_string(safe_o);
    int id_err = make_string(safe_e);

    base[HEAP_HEADER_ARRAY + 0] = (double)id_out;
    types[HEAP_HEADER_ARRAY + 0] = T_STR;

    base[HEAP_HEADER_ARRAY + 1] = (double)id_err;
    types[HEAP_HEADER_ARRAY + 1] = T_STR;

    free(safe_o);
    free(safe_e);

    vm_push(arr_ptr, T_OBJ);
}


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
    {"write_file", std_write_file, "num", 3, {"str", "str", "str"}},
    {"read_bytes", std_read_bytes, "arr", 2, {"str", "num"}},
    {"write_bytes", std_write_bytes, "num", 2, {"str", "arr"}},
    {"list", std_list, "arr", 1, {"num"}},
    {"remove", std_remove, "void", 2, {"any", "any"}},
    {"add", std_add, "arr", 3, {"arr", "num", "any"}},
    {"split", std_split, "arr", 2, {"str", "str"}},
    {"where", std_where, "num", 2, {"any", "any"}},
    {"range", std_range, "arr", 3, {"num", "num", "num"}},
    {"for_list", std_for_list, "arr", 2, {"str", "arr"}},
    {"seed", std_seed, "void", 1, {"num"}},
    {"rand", std_rand, "num", 0, {NULL}},
    {"rand_normal", std_rand_normal, "num", 0, {NULL}},
    {"mix", std_mix, "num", 3, {"num", "num", "num"}},
    {"min", std_min, "num", 2, {"num", "num"}},
    {"max", std_max, "num", 2, {"num", "num"}},
    {"distance", std_dist, "num", 4, {"num", "num", "num", "num"}},
    {"min_list", std_list_min, "num", 1, {"arr"}},
    {"max_list", std_list_max, "num", 1, {"arr"}},
    {"noise", std_noise, "num", 3, {"num", "num", "num"}},
    {"list_dir", std_list_dir, "arr", 2, {"str", "str"}},
    {"system", std_system, "arr", 1, {"str"}},
    {"system_thread", std_system_thread, "num", 2, {"str", "str"}},
    {"get_job", std_get_job, "any", 1, {"str"}},
    {NULL, NULL, NULL, 0, {NULL}}
};