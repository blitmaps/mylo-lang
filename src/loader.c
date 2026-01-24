#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "loader.h"
#include "vm.h" // Include full VM def for API

#ifdef _WIN32
    #include <windows.h>
    #define EXT ".dll"
#else
    #include <dlfcn.h>
    #define EXT ".so"
#endif

void get_lib_name(char* out, const char* base_name) {
    char temp[1024];
    strcpy(temp, base_name);
    char* dot = strrchr(temp, '.');
    if (dot) *dot = '\0';

#ifdef _WIN32
    sprintf(out, "%s.dll", temp);
#else
    sprintf(out, "./%s.so", temp);
#endif
}

void* load_library(const char* path) {
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) //printf("Failed to load library %s (Error: %lu)\n", path, GetLastError());
        fprintf(stderr, "Failed to load library %s (Error: %lu)\n", path, GetLastError());
    return (void*)h;
#else
    // RTLD_GLOBAL allows the plugin to see symbols if we exported them,
    // but we are using the struct method now, so standard load is fine.
    void* h = dlopen(path, RTLD_NOW);
    if (!h) //printf("Failed to load library %s (%s)\n", path, dlerror());
        fprintf(stderr, "Failed to load library %s (%s)\n", path, dlerror());
    return h;
#endif
}

void* get_symbol(void* lib_handle, const char* symbol_name) {
    if (!lib_handle) return NULL;
#ifdef _WIN32
    return (void*)GetProcAddress((HMODULE)lib_handle, symbol_name);
#else
    return dlsym(lib_handle, symbol_name);
#endif
}

void close_library(void* lib_handle) {
    if (!lib_handle) return;
#ifdef _WIN32
    FreeLibrary((HMODULE)lib_handle);
#else
    dlclose(lib_handle);
#endif
}