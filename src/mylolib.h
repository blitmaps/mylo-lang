#ifndef MYLO_LIB_H
#define MYLO_LIB_H

#include "vm.h"

#define MAX_STD_ARGS 4

typedef struct {
    const char *name;
    NativeFunc func;
    const char *ret_type;
    int arg_count;
    const char *arg_types[MAX_STD_ARGS];
} StdLibDef;

// Implementation functions
// --- Standard Library Implementation Functions ---
void std_sqrt(VM *vm);
void std_sin(VM *vm);
void std_cos(VM *vm);
void std_tan(VM *vm);
void std_floor(VM *vm);
void std_ceil(VM *vm);
void std_len(VM *vm);
void std_contains(VM *vm);
void std_to_string(VM *vm);
void std_to_num(VM *vm);
void std_read_lines(VM *vm);
void std_write_file(VM *vm);
void std_read_bytes(VM *vm);
void std_write_bytes(VM *vm);
void std_list(VM *vm);
void std_remove(VM *vm);
void std_add(VM *vm);
void std_split(VM *vm);
void std_where(VM *vm);
void std_range(VM *vm);
void std_for_list(VM *vm);

// Registry Declaration (Extern)
// We do NOT define it here, only say it exists.
extern const StdLibDef std_library[];

#endif