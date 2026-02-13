#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "loader.h"
#include "vm.h" // Include full VM def for API



void get_lib_name(char* out, const char* base_name) {
    printf("GETLIB Not Supported on Amiga");
    exit(1);
}

void* load_library(const char* path) {
    printf("LOADLIB Not Supported on Amiga");
    exit(1);
}

void* get_symbol(void* handle, const char* symbol) {
    printf("GETSYMBOL Not Supported on Amiga");
    exit(1);

}