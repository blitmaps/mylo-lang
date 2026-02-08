#ifndef MYLO_DEFINES_H
#define MYLO_DEFINES_H

// VM Memory Limits
#define STACK_SIZE 2048
#define MAX_CODE 536870912
#define MAX_HEAP 1000000000
#define MAX_GLOBALS 2048
#define MAX_CONSTANTS 1024
#define MAX_ARENAS 64         // 6 bits

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
#define TYPE_ANY -99
#define TYPE_NUM -98
#define TYPE_STR -97
#define TYPE_VOID -96

#define TYPE_ARRAY -1
#define TYPE_BYTES -2
#define TYPE_MAP -3

#define TYPE_I16_ARRAY  -10
#define TYPE_I32_ARRAY  -11
#define TYPE_I64_ARRAY  -12
#define TYPE_F16_ARRAY  -13
#define TYPE_F32_ARRAY  -14

#define TYPE_BOOL_ARRAY -20

// Heap Layout Constants
#define HEAP_OFFSET_TYPE 0
#define HEAP_OFFSET_LEN 1
#define HEAP_OFFSET_CAP 1
#define HEAP_OFFSET_COUNT 2
#define HEAP_OFFSET_DATA 3

#define HEAP_HEADER_STRUCT 1
#define HEAP_HEADER_ARRAY 2
#define HEAP_HEADER_MAP 4
#define MYLO_MONITOR_DEPTH 4

#define MAP_INITIAL_CAP 16
#define MAX_VM_FUNCTIONS 1024
#define MAX_BUS_ENTRIES 256
#define MAX_WORKERS 128


// --- POINTER PACKING (Safe Generational) ---
// We restrict the total used bits to 48 to ensure absolute safety
// within the 53-bit mantissa of a double, avoiding any implicit bit edge cases.
// Offset: 28 bits (256MB per region - plenty for tests)
// Arena:   6 bits (64 regions)
// Gen:    14 bits (16k versions)
// Total:  48 bits.

#define PTR_OFFSET_BITS 28
#define PTR_ARENA_BITS 6
#define PTR_GEN_BITS 14

#define PACK_PTR(gen, arena, offset) \
    ((double)( \
        ((unsigned long long)(offset)) | \
        ((unsigned long long)(arena) << PTR_OFFSET_BITS) | \
        ((unsigned long long)(gen) << (PTR_OFFSET_BITS + PTR_ARENA_BITS)) \
    ))

#define UNPACK_OFFSET(ptr) \
    ((int)((unsigned long long)(ptr) & 0xFFFFFFF))

#define UNPACK_ARENA(ptr) \
    ((int)(((unsigned long long)(ptr) >> PTR_OFFSET_BITS) & 0x3F))

#define UNPACK_GEN(ptr) \
    ((int)(((unsigned long long)(ptr) >> (PTR_OFFSET_BITS + PTR_ARENA_BITS)) & 0x3FFF))

#endif