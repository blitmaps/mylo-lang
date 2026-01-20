#ifndef MYLO_LIB_H
#define MYLO_LIB_H

#include "vm.h"

#define MAX_STD_ARGS 4

typedef struct {
    const char* name;
    NativeFunc func;
    const char* ret_type;
    int arg_count;
    const char* arg_types[MAX_STD_ARGS];
} StdLibDef;

// Implementation functions
void std_sqrt(VM* vm);
void std_len(VM* vm);
void std_contains(VM* vm); // <--- NEW
void std_read_lines(VM* vm);
void std_write_file(VM* vm);
void std_read_bytes(VM* vm);
void std_write_bytes(VM* vm);

// The Registry
static const StdLibDef std_library[] = {
    { "len",         std_len,         "num",  1, { "arr" } },
    { "contains",    std_contains,    "num",  2, { "any", "any" } }, // <--- NEW
    { "sqrt",        std_sqrt,        "num",  1, { "num" } },
    { "read_lines",  std_read_lines,  "arr",  1, { "str" } },
    { "write_file",  std_write_file,  "num",  3, { "str", "str", "str" } },
    { "read_bytes",  std_read_bytes,  "arr",  2, { "str", "num" } },
    { "write_bytes", std_write_bytes, "num",  2, { "str", "arr" } },
    { NULL, NULL, NULL, 0, {0} }
};

#endif