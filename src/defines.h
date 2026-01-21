#ifndef MYLO_DEFINES_H
#define MYLO_DEFINES_H

// VM Memory Limits
#define STACK_SIZE 2048
#define MAX_CODE 4096
#define MAX_HEAP 4096
#define MAX_GLOBALS 2048
#define MAX_CONSTANTS 1024

// String Limits
#define MAX_STRINGS 1024
#define MAX_STRING_LENGTH 1024

// Compilation Limits
#define MAX_IDENTIFIER 64
#define MAX_STRUCTS 64
#define MAX_FIELDS 16
#define MAX_NATIVES 64
#define MAX_FFI_ARGS 4
#define MAX_C_BLOCK_SIZE 1024
#define MAX_LOOP_NESTING 32
#define MAX_JUMPS_PER_LOOP 64
#define MAX_ENUM_MEMBERS 1024 // <--- NEW

// Output
#define OUTPUT_BUFFER_SIZE 128000

// Types
#define TYPE_ARRAY -1

// Heap Layout Constants
#define HEAP_OFFSET_TYPE 0
#define HEAP_OFFSET_LEN 1     // For Arrays
#define HEAP_HEADER_STRUCT 1  // Size of struct header (TypeID)
#define HEAP_HEADER_ARRAY 2   // Size of array header (TypeID + Length)

#endif