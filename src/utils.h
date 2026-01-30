//
// Created by brad on 19/01/2026.
//

#ifndef MYLO_UTILS_H
#define MYLO_UTILS_H
// This turns on Terminal Colours
#define ENABLE_TERMINAL_COLOURS
// Read file to char array
char *read_file(const char *fn);

// ANSI Color Codes
enum MyloColor {

  MyloFgRed = 31,
  MyloFgGreen = 32,
  MyloFgYellow = 33,
  MyloFgBlue = 34,
  MyloFgMagenta = 35,
  MyloFgCyan = 36,
  MyloFgWhite = 37,
  MyloFgDefault = 39,
  MyloBgColorRed = 41,
  MyloBgColorGreen = 42,
  MyloBgColorYellow = 43,
  MyloBgColorBlue = 44,
  MyloBgColorMagenta = 45,
  MyloBgColorCyan = 46,
  MyloBgColorWhite = 47,
  MyloBgColorDefault = 49
};

// Function to set colors
void setTerminalColor(enum MyloColor fg, enum MyloColor bg);

// Function to reset to default
void resetTerminal();

#endif