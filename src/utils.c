#define _CRT_SECURE_NO_WARNINGS
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>


char *read_file(const char *fn) {
  FILE *f = fopen(fn, "rb");
  if (!f) {
    // Changed: Return NULL instead of exit(1) to allow search paths
    return NULL;
  }

  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *b = (char *)malloc(len + 1);
  if (len > 0)
    fread(b, 1, len, f);
  b[len] = '\0';
  fclose(f);
  return b;
}

// Function to set colors
void setTerminalColor(enum MyloColor fg, enum MyloColor bg) {
  #ifdef ENABLE_TERMINAL_COLOURS
  printf("\033[%d;%dm", (int)fg, (int)bg);
  #endif
}

// Function to reset to default
void resetTerminal() { 
  #ifdef ENABLE_TERMINAL_COLOURS
  printf("\033[0m"); 
  #endif
}
