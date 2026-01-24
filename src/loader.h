#ifndef MYLO_LOADER_H
#define MYLO_LOADER_H

#include "vm.h"

// Loads a shared library (.dll / .so) at the given path
// Returns an opaque handle, or NULL on failure
void* load_library(const char* path);

// Retrieves a function pointer from the library
void* get_symbol(void* lib_handle, const char* symbol_name);

// Unloads the library
void close_library(void* lib_handle);

// Helper to construct platform specific names (foo -> foo.dll or libfoo.so)
void get_lib_name(char* out, const char* base_name);

#endif