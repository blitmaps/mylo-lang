#define _CRT_SECURE_NO_WARNINGS
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>

char *read_file(const char *fn) {
  FILE *f = fopen(fn, "rb");
  if (!f) return NULL;

  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *b = (char *)malloc(len + 1);
  if (len > 0) fread(b, 1, len, f);
  b[len] = '\0';
  fclose(f);
  return b;
}

// Wrapper for stdout
void setTerminalColor(enum MyloColor fg, enum MyloColor bg) {
  fsetTerminalColor(stdout, fg, bg);
}

// Wrapper for stdout
void resetTerminal() {
  fresetTerminal(stdout);
}

// Actual Implementation
void fsetTerminalColor(FILE* f, enum MyloColor fg, enum MyloColor bg) {
#ifdef ENABLE_TERMINAL_COLOURS
  fprintf(f, "\033[%d;%dm", (int)fg, (int)bg);
#endif
}

void fresetTerminal(FILE* f) {
#ifdef ENABLE_TERMINAL_COLOURS
  fprintf(f, "\033[0m");
#endif
}