#ifndef MYLO_DEFINES_H
#define MYLO_DEFINES_H

// VM Memory Limits
#define STACK_SIZE 2048
#define MAX_CODE 536870912
#define MAX_HEAP 4096
#define MAX_GLOBALS 2048
#define MAX_CONSTANTS 1024
#define MAX_ARENAS 64
// String Limits
#define MAX_STRINGS 1024
#define MAX_STRING_LENGTH 1024
#define MAX_C_HEADERS 32
// Compilation Limits
#define MAX_IDENTIFIER 64
#define MAX_STRUCTS 64
#define MAX_FIELDS 16
#define MAX_NATIVES 64
#define MAX_FFI_ARGS 4
#define MAX_C_BLOCK_SIZE 1024
#define MAX_LOOP_NESTING 32
#define MAX_JUMPS_PER_LOOP 64
#define MAX_ENUM_MEMBERS 1024
#define MAX_SEARCH_PATHS 16

// Output
#define OUTPUT_BUFFER_SIZE 128000

// Types
#define TYPE_ARRAY -1
#define TYPE_BYTES -2
#define TYPE_MAP -3

#define TYPE_I16_ARRAY  -10
#define TYPE_I32_ARRAY  -11
#define TYPE_I64_ARRAY  -12 // Note: Stack is double, i64 > 2^53 will lose precision
#define TYPE_F16_ARRAY  -13 // Storage only
#define TYPE_F32_ARRAY  -14

#define TYPE_BOOL_ARRAY -20

// Heap Layout Constants
#define HEAP_OFFSET_TYPE 0
#define HEAP_OFFSET_LEN 1     // For Arrays
#define HEAP_OFFSET_CAP 1     // For Maps
#define HEAP_OFFSET_COUNT 2   // For Maps
#define HEAP_OFFSET_DATA 3    // For Maps (Pointer to Buffer)

#define HEAP_HEADER_STRUCT 1  // Size of struct header (TypeID)
#define HEAP_HEADER_ARRAY 2   // Size of array header (TypeID + Length)
#define HEAP_HEADER_MAP 4     // Type, Cap, Count, DataPtr

#define MAP_INITIAL_CAP 16

// VM function table
#define MAX_VM_FUNCTIONS 1024  // Adjust as needed

// ARENAs
#define PACK_PTR(arena, offset) ((double)((unsigned long long)(arena) << 32 | (unsigned int)(offset)))
#define UNPACK_ARENA(ptr) ((int)((unsigned long long)(ptr) >> 32))
#define UNPACK_OFFSET(ptr) ((int)((unsigned long long)(ptr) & 0xFFFFFFFF))
#endif