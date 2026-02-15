#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mylolib.h"
#include "vm.h"
#include "defines.h"
#include "os_job.h"
#include <fcntl.h>
#define STRDUP strdup

#include <stdio.h>
#include <stdlib.h>

// AMIGA

/* mylo_amiga.c */
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <stdlib.h>
#include <stdio.h>

#include "vm.h"
#include "mylolib.h"

// Globals for Amiga Libraries
struct Library *IntuitionBasePtr = NULL;
struct Library *GfxBasePtr = NULL;

// Helper to ensure libraries are open
void ensure_amiga_libs() {
    if (!IntuitionBasePtr) {
        IntuitionBasePtr = OpenLibrary("intuition.library", 36);
        if (!IntuitionBasePtr) {
            printf("Error: Could not open intuition.library v36+\n");
            exit(1);
        }
    }
    if (!GfxBasePtr) {
        GfxBasePtr = OpenLibrary("graphics.library", 36);
        if (!GfxBasePtr) {
            printf("Error: Could not open graphics.library v36+\n");
            exit(1);
        }
    }
}

// --- Window Management ---

// Amiga.OpenWindow(title, x, y, width, height) -> window_ptr (num)
void amiga_open_window(VM* vm) {
    ensure_amiga_libs();

    double h = vm_pop(vm);
    double w = vm_pop(vm);
    double y = vm_pop(vm);
    double x = vm_pop(vm);
    double title_idx = vm_pop(vm);
    const char* title = vm->string_pool[(int)title_idx];

    struct NewWindow nw = {
        (WORD)x, (WORD)y, (WORD)w, (WORD)h,
        0, 1,
        IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_VANILLAKEY,
        WFLG_SIZEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE,
        NULL, NULL, (STRPTR)title,
        NULL, NULL, 0, 0, 0, 0, WBENCHSCREEN
    };

    struct Window* win = OpenWindow(&nw);

    // Return pointer as a double (safe for 32-bit Amiga pointers)
    vm_push(vm, (double)((unsigned long)win), T_NUM);
}

// Amiga.CloseWindow(window_ptr)
void amiga_close_window(VM* vm) {
    double win_ref = vm_pop(vm);
    struct Window* win = (struct Window*)((unsigned long)win_ref);
    if (win) CloseWindow(win);
    vm_push(vm, 0, T_NUM);
}

// --- Graphics Primitives ---

// Amiga.SetColor(window_ptr, pen, r, g, b)
void amiga_set_color(VM* vm) {
    double b = vm_pop(vm);
    double g = vm_pop(vm);
    double r = vm_pop(vm);
    double pen = vm_pop(vm);
    double win_ref = vm_pop(vm);

    struct Window* win = (struct Window*)((unsigned long)win_ref);
    if (!win) return;

    // Set the RGB value for this pen (simple palette modification)
    SetRGB4(win->WScreen->ViewPort.ColorMap, (long)pen, (long)r, (long)g, (long)b);
    SetAPen(win->RPort, (long)pen);

    vm_push(vm, 0, T_NUM);
}

// Amiga.DrawLine(window_ptr, x1, y1, x2, y2, color_pen)
void amiga_draw_line(VM* vm) {
    double pen = vm_pop(vm);
    double y2 = vm_pop(vm);
    double x2 = vm_pop(vm);
    double y1 = vm_pop(vm);
    double x1 = vm_pop(vm);
    double win_ref = vm_pop(vm);

    struct Window* win = (struct Window*)((unsigned long)win_ref);
    if (win) {
        SetAPen(win->RPort, (long)pen);
        Move(win->RPort, (WORD)x1, (WORD)y1);
        Draw(win->RPort, (WORD)x2, (WORD)y2);
    }
    vm_push(vm, 0, T_NUM);
}

// Amiga.DrawRect(window_ptr, x1, y1, x2, y2, color_pen)
void amiga_draw_rect(VM* vm) {
    double pen = vm_pop(vm);
    double y2 = vm_pop(vm);
    double x2 = vm_pop(vm);
    double y1 = vm_pop(vm);
    double x1 = vm_pop(vm);
    double win_ref = vm_pop(vm);

    struct Window* win = (struct Window*)((unsigned long)win_ref);
    if (win) {
        SetAPen(win->RPort, (long)pen);
        RectFill(win->RPort, (WORD)x1, (WORD)y1, (WORD)x2, (WORD)y2);
    }
    vm_push(vm, 0, T_NUM);
}

// Amiga.DrawText(window_ptr, x, y, text, color_pen)
void amiga_draw_text(VM* vm) {
    double pen = vm_pop(vm);
    double text_idx = vm_pop(vm);
    double y = vm_pop(vm);
    double x = vm_pop(vm);
    double win_ref = vm_pop(vm);

    struct Window* win = (struct Window*)((unsigned long)win_ref);
    const char* str = vm->string_pool[(int)text_idx];

    if (win) {
        SetAPen(win->RPort, (long)pen);
        Move(win->RPort, (WORD)x, (WORD)y);
        Text(win->RPort, (STRPTR)str, strlen(str));
    }
    vm_push(vm, 0, T_NUM);
}

// --- Event Handling ---

// Amiga.GetEvent(window_ptr) -> Map { type: "code", x: 0, y: 0, code: 0 }
// Returns null if no event is waiting.
void amiga_get_event(VM* vm) {
    double win_ref = vm_pop(vm);
    struct Window* win = (struct Window*)((unsigned long)win_ref);

    if (!win) { vm_push(vm, 0, T_NUM); return; }

    struct IntuiMessage* msg = (struct IntuiMessage*)GetMsg(win->UserPort);
    if (!msg) {
        vm_push(vm, 0, T_NUM); // Return 0 (null) if empty
        return;
    }

    ULONG class = msg->Class;
    UWORD code = msg->Code;
    WORD mouseX = msg->MouseX;
    WORD mouseY = msg->MouseY;

    // We must reply before pushing new objects to VM to avoid GC issues or lag
    ReplyMsg((struct Message*)msg);

    // Create a Map to return
    // We reuse the standard library OP_MAP logic manually or call helper
    // For simplicity, we'll build a map manually using heap_alloc

    int cap = 4;
    double map_ptr = heap_alloc(vm, 4);
    double data_ptr = heap_alloc(vm, cap * 2);
    double* base = vm_resolve_ptr(vm, map_ptr);
    base[0] = TYPE_MAP; base[1] = (double)cap; base[2] = 0; base[3] = data_ptr;

    // Helper to push key/value to this map
    // Note: In a real implementation, you'd add a helper function for this
    // We will just return an Array [type_str, code, x, y] for speed and ease

    /* Return Format: [Type (str), Code (num), MouseX (num), MouseY (num)]
    */

    double arr_ptr = heap_alloc(vm, 4 + HEAP_HEADER_ARRAY);
    double* arr = vm_resolve_ptr(vm, arr_ptr);
    int* types = vm_resolve_type(vm, arr_ptr);

    arr[0] = TYPE_ARRAY;
    arr[1] = 4; // Length

    // 1. Type String
    const char* typeStr = "unknown";
    if (class == IDCMP_CLOSEWINDOW) typeStr = "close";
    else if (class == IDCMP_MOUSEBUTTONS) typeStr = "mouse";
    else if (class == IDCMP_VANILLAKEY) typeStr = "key";

    int str_id = make_string(vm, typeStr);
    arr[2] = (double)str_id; types[2] = T_STR;

    // 2. Code
    arr[3] = (double)code; types[3] = T_NUM;

    // 3. X
    arr[4] = (double)mouseX; types[4] = T_NUM;

    // 4. Y
    arr[5] = (double)mouseY; types[5] = T_NUM;

    vm_push(vm, arr_ptr, T_OBJ);
}

// Amiga.Wait(window_ptr) - Blocks until signal
void amiga_wait(VM* vm) {
    double win_ref = vm_pop(vm);
    struct Window* win = (struct Window*)((unsigned long)win_ref);
    if(win) Wait(1L << win->UserPort->mp_SigBit);
    vm_push(vm, 0, T_NUM);
}



// Implementation of std_copy (Deep Copy)
void std_copy(VM* vm) {
    if (vm->sp < 0) {
        //mylo_runtime_error(vm, "Stack Underflow"); return;
        printf("Runtime Error: Stack Overflow at copy");
        exit(1);
    }

    double val = vm->stack[vm->sp];
    int type = vm->stack_types[vm->sp];
    vm->sp--; // Pop argument

    if (type == T_OBJ) {
        // Force a deep copy by pretending the object is in a "danger zone" (target_head = 0)
        // This copies it to the end of the current arena.
        double new_val = vm_evacuate_object(vm, val, 99999999);
        // WAIT: vm_evacuate_object takes "target_head" as the SAFETY boundary.
        // If (offset < target_head) -> Safe.
        // We want (offset < target_head) to be FALSE.
        double res = vm_evacuate_object(vm, val, 0);
        vm_push(vm, res, T_OBJ);
    } else {
        // Primitives copy by value
        vm_push(vm, val, type);
    }
}
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

// --- Previous Standard Lib Functions (unchanged) ---

void std_sqrt(VM *vm) { vm_push(vm, sqrt(vm_pop(vm)), T_NUM); }
void std_sin(VM *vm) { vm_push(vm, sin(vm_pop(vm)), T_NUM); }
void std_cos(VM *vm) { vm_push(vm, cos(vm_pop(vm)), T_NUM); }
void std_tan(VM *vm) { vm_push(vm, tan(vm_pop(vm)), T_NUM); }
void std_floor(VM *vm) { vm_push(vm, floor(vm_pop(vm)), T_NUM); }
void std_ceil(VM *vm) { vm_push(vm, ceil(vm_pop(vm)), T_NUM); }

void std_len(VM *vm) {
    double val = vm_pop(vm);

    if (vm->stack_types[vm->sp + 1] == T_OBJ) {
        double* base = vm_resolve_ptr(vm, val);
        if(!base) { vm_push(vm, 0, T_NUM); return; }

        int type = (int)base[HEAP_OFFSET_TYPE];

        if(type == TYPE_ARRAY || type == TYPE_BYTES || (type <= TYPE_I16_ARRAY && type >= TYPE_BOOL_ARRAY)) {
            vm_push(vm, base[HEAP_OFFSET_LEN], T_NUM);
        }
        else if (type == TYPE_MAP) {
            vm_push(vm, base[HEAP_OFFSET_COUNT], T_NUM);
        }
        else {
            printf("Runtime Error: len() expects array, string, map, or bytes.\n");
            exit(1);
        }
    } else if (vm->stack_types[vm->sp + 1] == T_STR) {
        const char *s = get_str(vm, val);
        vm_push(vm, (double) strlen(s), T_NUM);
    } else {
        printf("Runtime Error: len() expects array or string.\n");
        exit(1);
    }
}
void std_contains(VM *vm) {
    double needle_val = vm_pop(vm);
    int needle_type = vm->stack_types[vm->sp + 1];
    double haystack_val = vm_pop(vm);
    int haystack_type = vm->stack_types[vm->sp + 1];

    if (haystack_type == T_STR) {
        if (needle_type != T_STR) {
            printf("Runtime Error: contains() string needle\n");
            exit(1);
        }
        if (strstr(get_str(vm, haystack_val), get_str(vm, needle_val)) != NULL) vm_push(vm, 1.0, T_NUM);
        else vm_push(vm, 0.0, T_NUM);
        return;
    }

    if (haystack_type == T_OBJ) {
        double* base = vm_resolve_ptr(vm, haystack_val);
        int* types = vm_resolve_type(vm, haystack_val);
        if (!base) { vm_push(vm, 0.0, T_NUM); return; }

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
            vm_push(vm, found ? 1.0 : 0.0, T_NUM);
            return;
        } else if (objType == TYPE_MAP) {
            if (needle_type != T_STR) {
                printf("Runtime Error: contains() map check requires string key\n");
                exit(1);
            }
            int count = (int)base[HEAP_OFFSET_COUNT];
            double dataPtrVal = base[HEAP_OFFSET_DATA];
            double* data = vm_resolve_ptr(vm, dataPtrVal);

            int found = 0;
            if(data) {
                for (int i = 0; i < count; i++) {
                    if (data[i * 2] == needle_val) {
                        found = 1;
                        break;
                    }
                }
            }
            vm_push(vm, found ? 1.0 : 0.0, T_NUM);
            return;
        }
    }
    printf("Runtime Error: contains() type\n");
    exit(1);
}

void std_to_string(VM *vm) {
    double val = vm_pop(vm);
    int type = vm->stack_types[vm->sp + 1];

    if (type == T_NUM) {
        char buf[64];
        if (val == (int) val) snprintf(buf, 64, "%d", (int) val);
        else snprintf(buf, 64, "%g", val);

        int str_id = make_string(vm, buf);
        vm_push(vm, (double) str_id, T_STR);
    } else if (type == T_STR) {
        vm_push(vm, val, T_STR);
    } else if (type == T_OBJ) {
        char buf[64];
        snprintf(buf, 64, "[Object]");
        int str_id = make_string(vm, buf);
        vm_push(vm, (double) str_id, T_STR);
    } else {
        int str_id = make_string(vm, "");
        vm_push(vm, (double) str_id, T_STR);
    }
}

void std_to_num(VM *vm) {
    double val = vm_pop(vm);
    int type = vm->stack_types[vm->sp + 1];

    if (type == T_NUM) {
        vm_push(vm, val, T_NUM);
    } else if (type == T_STR) {
        const char *s = get_str(vm, val);
        char *end;
        double d = strtod(s, &end);
        vm_push(vm, d, T_NUM);
    } else {
        vm_push(vm, 0.0, T_NUM);
    }
}

void std_read_lines(VM *vm) {
    double path_id = vm_pop(vm);
    const char *path = get_str(vm, path_id);

    FILE *f = fopen(path, "r");
    if (!f) {
        double addr = heap_alloc(vm, HEAP_HEADER_ARRAY);
        double* base = vm_resolve_ptr(vm, addr);
        base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
        base[HEAP_OFFSET_LEN] = 0;
        vm_push(vm, addr, T_OBJ);
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

    double arr_addr = heap_alloc(vm, lines + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(vm, arr_addr);
    int* types = vm_resolve_type(vm, arr_addr);

    base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    base[HEAP_OFFSET_LEN] = (double) lines;

    rewind(f);
    char buffer[4096];
    int i = 0;
    while (i < lines && fgets(buffer, sizeof(buffer), f)) {
        trim_newline(buffer);
        int str_id = make_string(vm, buffer);
        base[HEAP_HEADER_ARRAY + i] = (double) str_id;
        types[HEAP_HEADER_ARRAY + i] = T_STR;
        i++;
    }
    fclose(f);

    vm_push(vm, arr_addr, T_OBJ);
}

void std_write_file(VM *vm) {
    double mode_id = vm_pop(vm);
    double content_id = vm_pop(vm);
    double path_id = vm_pop(vm);

    const char *mode = get_str(vm, mode_id);
    const char *content = get_str(vm, content_id);
    const char *path = get_str(vm, path_id);

    if (strcmp(mode, "w") != 0 && strcmp(mode, "a") != 0) {
        printf("Runtime Error: write_file mode must be 'w' or 'a'\n");
        exit(1);
    }

    FILE *f = fopen(path, mode);
    if (!f) {
        vm_push(vm, 0.0, T_NUM);
        return;
    }

    fprintf(f, "%s", content);
    fclose(f);
    vm_push(vm, 1.0, T_NUM);
}

void std_read_bytes(VM *vm) {
    double stride_val = vm_pop(vm);
    double path_id = vm_pop(vm);
    const char *path = get_str(vm, path_id);
    int stride = (int) stride_val;

    if (stride != 1 && stride != 4) {
        printf("Runtime Error: read_bytes stride must be 1 (byte) or 4 (int)\n");
        exit(1);
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        double addr = heap_alloc(vm, HEAP_HEADER_ARRAY);
        double* base = vm_resolve_ptr(vm, addr);
        base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
        base[HEAP_OFFSET_LEN] = 0;
        vm_push(vm, addr, T_OBJ);
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
    double addr = heap_alloc(vm, doubles_needed + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(vm, addr);

    base[HEAP_OFFSET_TYPE] = TYPE_BYTES;
    base[HEAP_OFFSET_LEN] = (double) element_count;

    char* heap_bytes = (char*)&base[HEAP_HEADER_ARRAY];
    memcpy(heap_bytes, buf, element_count);

    free(buf);
    vm_push(vm, addr, T_OBJ);
}

void std_write_bytes(VM *vm) {
    double arr_ref = vm_pop(vm);
    double path_id = vm_pop(vm);

    double* base = vm_resolve_ptr(vm, arr_ref);
    if (!base || (int) base[HEAP_OFFSET_TYPE] != TYPE_ARRAY) {
        printf("Runtime Error: write_bytes expects array\n");
        exit(1);
    }

    const char *path = get_str(vm, path_id);
    int len = (int) base[HEAP_OFFSET_LEN];

    FILE *f = fopen(path, "wb");
    if (!f) {
        vm_push(vm, 0.0, T_NUM);
        return;
    }

    for (int i = 0; i < len; i++) {
        unsigned char b = (unsigned char) base[HEAP_HEADER_ARRAY + i];
        fputc(b, f);
    }

    fclose(f);
    vm_push(vm, 1.0, T_NUM);
}

void std_list(VM *vm) {
    double size_val = vm_pop(vm);
    int size = (int)size_val;

    if (size < 0) {
        printf("Runtime Error: list() size must be positive\n");
        exit(1);
    }

    double ptr = heap_alloc(vm, size + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(vm, ptr);
    int* types = vm_resolve_type(vm, ptr);

    base[HEAP_OFFSET_TYPE] = TYPE_ARRAY;
    base[HEAP_OFFSET_LEN] = (double)size;

    for(int i = 0; i < size; i++) {
        base[HEAP_HEADER_ARRAY + i] = 0.0;
        types[HEAP_HEADER_ARRAY + i] = T_NUM;
    }

    vm_push(vm, ptr, T_OBJ);
}

void std_remove(VM *vm) {
    double key_val = vm_pop(vm);
    int key_type = vm->stack_types[vm->sp + 1];
    double obj_val = vm_pop(vm);

    if (vm->stack_types[vm->sp + 1] != T_OBJ) {
        printf("Runtime Error: remove() expects an object\n");
        exit(1);
    }

    double* base = vm_resolve_ptr(vm, obj_val);
    int* types = vm_resolve_type(vm, obj_val);
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
        double* data = vm_resolve_ptr(vm, data_ptr_val);
        int* data_types = vm_resolve_type(vm, data_ptr_val);

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
    vm_push(vm, obj_val, T_OBJ);
}

void std_add(VM *vm) {
    double val = vm_pop(vm);
    int val_type = vm->stack_types[vm->sp + 1];

    double idx_val = vm_pop(vm);
    int idx = (int)idx_val;

    double arr_val = vm_pop(vm);

    double* base = vm_resolve_ptr(vm, arr_val);
    int* types = vm_resolve_type(vm, arr_val);

    if (vm->stack_types[vm->sp + 1] != T_OBJ || (int)base[0] != TYPE_ARRAY) {
        printf("Runtime Error: add() expects an array\n");
        exit(1);
    }

    int len = (int)base[HEAP_OFFSET_LEN];

    if (idx < 0) idx += len;
    if (idx < 0) idx = 0;
    if (idx > len) idx = len;

    double new_ptr = heap_alloc(vm, len + 1 + HEAP_HEADER_ARRAY);
    double* new_base = vm_resolve_ptr(vm, new_ptr);
    int* new_types = vm_resolve_type(vm, new_ptr);

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

    vm_push(vm, new_ptr, T_OBJ);
}

void std_split(VM* vm) {
    double del_val = vm_pop(vm);
    double str_val = vm_pop(vm);

    const char* str = get_str(vm, str_val);
    const char* del = get_str(vm, del_val);
    int del_len = strlen(del);

    if (del_len == 0) {
        int len = strlen(str);
        double ptr = heap_alloc(vm, len + HEAP_HEADER_ARRAY);
        double* base = vm_resolve_ptr(vm, ptr);
        int* types = vm_resolve_type(vm, ptr);

        base[0] = TYPE_ARRAY;
        base[1] = (double)len;

        for (int i = 0; i < len; i++) {
            char tmp[2] = { str[i], '\0' };
            int id = make_string(vm, tmp);
            base[2 + i] = (double)id;
            types[2 + i] = T_STR;
        }
        vm_push(vm, ptr, T_OBJ);
        return;
    }

    int count = 1;
    const char* p = str;
    while ((p = strstr(p, del)) != NULL) {
        count++;
        p += del_len;
    }

    double ptr = heap_alloc(vm, count + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(vm, ptr);
    int* types = vm_resolve_type(vm, ptr);

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

        int id = make_string(vm, buf);
        free(buf);

        base[2 + idx] = (double)id;
        types[2 + idx] = T_STR;
        idx++;
        p = next + del_len;
    }

    int id = make_string(vm, p);
    base[2 + idx] = (double)id;
    types[2 + idx] = T_STR;

    vm_push(vm, ptr, T_OBJ);
}

void std_where(VM* vm) {
    double item_val = vm_pop(vm);
    int item_type = vm->stack_types[vm->sp + 1];
    double col_val = vm_pop(vm);
    int col_type = vm->stack_types[vm->sp + 1];

    if (col_type == T_STR) {
        if (item_type != T_STR) { vm_push(vm, -1.0, T_NUM); return; }
        const char* haystack = get_str(vm, col_val);
        const char* needle = get_str(vm, item_val);
        const char* found = strstr(haystack, needle);
        if (found) vm_push(vm, (double)(found - haystack), T_NUM);
        else vm_push(vm, -1.0, T_NUM);
    }
    else if (col_type == T_OBJ) {
        double* base = vm_resolve_ptr(vm, col_val);
        int* types = vm_resolve_type(vm, col_val);
        if(!base) { vm_push(vm, -1.0, T_NUM); return; }

        int type = (int)base[0];

        if (type == TYPE_ARRAY) {
            int len = (int)base[1];
            for (int i = 0; i < len; i++) {
                double el = base[2 + i];
                int et = types[2 + i];
                if (el == item_val && et == item_type) {
                    vm_push(vm, (double)i, T_NUM);
                    return;
                }
            }
        }
        else if (type == TYPE_BYTES) {
            int len = (int)base[1];
            unsigned char* b = (unsigned char*)&base[HEAP_HEADER_ARRAY];
            for (int i = 0; i < len; i++) {
                if ((double)b[i] == item_val) {
                    vm_push(vm, (double)i, T_NUM);
                    return;
                }
            }
        }
        vm_push(vm, -1.0, T_NUM);
    }
    else {
        vm_push(vm, -1.0, T_NUM);
    }
}

void std_range(VM *vm) {
    double stop_val = vm_pop(vm);
    double step_val = vm_pop(vm);
    double start_val = vm_pop(vm);

    if (step_val == 0) {
        printf("Runtime Error: range() step cannot be 0\n");
        exit(1);
    }

    double abs_step = fabs(step_val);
    if (abs_step == 0) abs_step = 1.0;

    double diff = fabs(stop_val - start_val);
    int count = (int)((diff / abs_step) + 1.00000001);
    if (count < 0) count = 0;

    double ptr = heap_alloc(vm, count + HEAP_HEADER_ARRAY);
    double* base = vm_resolve_ptr(vm, ptr);
    int* types = vm_resolve_type(vm, ptr);

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

    vm_push(vm, ptr, T_OBJ);
}

void std_for_list(VM *vm) {
    double list_ref = vm_pop(vm);
    double func_val = vm_pop(vm);

    double* base = vm_resolve_ptr(vm, list_ref);
    int* types = vm_resolve_type(vm, list_ref);

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

    double res_ptr = heap_alloc(vm, len + HEAP_HEADER_ARRAY);
    double* res_base = vm_resolve_ptr(vm, res_ptr);
    int* res_types = vm_resolve_type(vm, res_ptr);

    res_base[0] = TYPE_ARRAY;
    res_base[1] = (double)len;

    for(int i=0; i<len; i++) {
         double val = base[HEAP_HEADER_ARRAY + i];
         int type = types[HEAP_HEADER_ARRAY + i];

         if (native_target) {
             vm_push(vm, val, type);
             native_target(vm);
         } else {
             vm_push(vm, (double)vm->code_size, T_NUM);
             vm_push(vm, (double)vm->fp, T_NUM);
             vm_push(vm, val, type);
             vm->fp = vm->sp;

             run_vm_from(vm, user_func_addr, false);
         }

         double res = vm_pop(vm);
         int res_type = vm->stack_types[vm->sp + 1];

         res_base[HEAP_HEADER_ARRAY + i] = res;
         res_types[HEAP_HEADER_ARRAY + i] = res_type;
    }

    vm->ip = saved_ip;
    vm_push(vm, res_ptr, T_OBJ);
}

void std_seed(VM *vm) {
    double val = vm_pop(vm);
    srand((unsigned int)val);
    vm_push(vm, 0.0, T_NUM);
}

void std_rand(VM *vm) {
    double r = (double)rand() / (double)RAND_MAX;
    vm_push(vm, r, T_NUM);
}

void std_rand_normal(VM *vm) {
    double u1 = (double)rand() / (double)RAND_MAX;
    double u2 = (double)rand() / (double)RAND_MAX;
    if (u1 < 1e-9) u1 = 1e-9;
    double z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
    vm_push(vm, z0, T_NUM);
}

void std_mix(VM *vm) {
    double a = vm_pop(vm);
    double y = vm_pop(vm);
    double x = vm_pop(vm);
    double result = x + (y - x) * a;
    vm_push(vm, result, T_NUM);
}

void std_min(VM *vm) {
    double b = vm_pop(vm);
    double a = vm_pop(vm);
    if (a < b) vm_push(vm, a, T_NUM);
    else vm_push(vm, b, T_NUM);
}

void std_max(VM *vm) {
    double b = vm_pop(vm);
    double a = vm_pop(vm);
    if (a > b) vm_push(vm, a, T_NUM);
    else vm_push(vm, b, T_NUM);
}

void std_dist(VM *vm) {
    double y2 = vm_pop(vm);
    double x2 = vm_pop(vm);
    double y1 = vm_pop(vm);
    double x1 = vm_pop(vm);
    double dx = x2 - x1;
    double dy = y2 - y1;
    double dist = sqrt(dx*dx + dy*dy);
    vm_push(vm, dist, T_NUM);
}

void std_list_min(VM *vm) {
    double list_ref = vm_pop(vm);
    double* base = vm_resolve_ptr(vm, list_ref);

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
    vm_push(vm, min_val, T_NUM);
}

void std_list_max(VM *vm) {
    double list_ref = vm_pop(vm);
    double* base = vm_resolve_ptr(vm, list_ref);

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
    vm_push(vm, max_val, T_NUM);
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
    double z = vm_pop(vm);
    double y = vm_pop(vm);
    double x = vm_pop(vm);

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

    vm_push(vm, res, T_NUM);
}


// Helper to check if string ends with suffix
static int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr) return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
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
    {"copy", std_copy, "any", 1, {"any"}},
//{"window", std_amiga_window, "void", 2, {"num", "num"}},
    {"amiga_open_window", amiga_open_window, "num", 5, {"str", "num", "num", "num", "num"}},
    {"amiga_close_window", amiga_close_window, "void", 1, {"num"}},
    {"amiga_draw_line", amiga_draw_line, "void", 6, {"num", "num", "num", "num", "num", "num"}},
    {"amiga_draw_rect", amiga_draw_rect, "void", 6, {"num", "num", "num", "num", "num", "num"}},
    {"amiga_draw_text", amiga_draw_text, "void", 5, {"num", "num", "num", "str", "num"}},
    {"amiga_set_color", amiga_set_color, "void", 5, {"num", "num", "num", "num", "num"}},
    {"amiga_get_event", amiga_get_event, "any", 1, {"num"}}, // Returns Array or Null
    {"amiga_wait", amiga_wait, "void", 1, {"num"}},
    {NULL, NULL, NULL, 0, {NULL}}
};
