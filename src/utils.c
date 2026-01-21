#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include "utils.h"

char* read_file(const char* fn) {
    FILE* f = fopen(fn, "rb");
    if(!f) {
        // Changed: Return NULL instead of exit(1) to allow search paths
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* b = (char*)malloc(len + 1);
    if(len > 0) fread(b, 1, len, f);
    b[len] = '\0';
    fclose(f);
    return b;
}