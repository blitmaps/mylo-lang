#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include "debug_adapter.h"
#include "vm.h"
#include "compiler.h"

#define MAX_PACKET_SIZE 8192
static char current_source_path[4096];
static char* pending_source_content = NULL; // <--- Store source here

const char* get_function_name(int ip) {
    // Find the function with the highest address that is still <= ip
    const char* best_name = "main";
    int best_addr = -1;

    for(int i=0; i<func_count; i++) {
        if (funcs[i].addr <= ip && funcs[i].addr > best_addr) {
            best_name = funcs[i].name;
            best_addr = funcs[i].addr;
        }
    }
    return best_name;
}

// --- JSON Helpers ---

void send_json(const char* content) {
    int len = strlen(content);
    fprintf(stdout, "Content-Length: %d\r\n\r\n%s", len, content);
    fflush(stdout);
}

void send_event(const char* event, const char* body_json) {
    char buf[4096];
    snprintf(buf, 4096, "{\"jsonrpc\": \"2.0\", \"type\": \"event\", \"event\": \"%s\", \"body\": %s}", event, body_json ? body_json : "{}");
    send_json(buf);
}

void send_response(int seq, const char* command, const char* body_json) {
    char buf[4096];
    snprintf(buf, 4096, "{\"jsonrpc\": \"2.0\", \"type\": \"response\", \"request_seq\": %d, \"success\": true, \"command\": \"%s\", \"body\": %s}",
             seq, command, body_json ? body_json : "{}");
    send_json(buf);
}

void debug_print_callback(const char* msg) {
    char buf[1024];
    // JSON escape newlines slightly for safety, though robust escaping needs a library
    snprintf(buf, 1024, "{\"category\": \"stdout\", \"output\": \"%s\\n\"}", msg);
    send_event("output", buf);
}

void debug_error_callback(const char* msg) {
    // 1. Send the error text to the Debug Console (stderr category makes it red)
    char buf[1024];
    snprintf(buf, 1024, "{\"category\": \"stderr\", \"output\": \"%s\\n\"}", msg);
    send_event("output", buf);

    // 2. Tell VS Code we are dead
    send_event("terminated", "{}");
}

// --- State ---
bool is_paused = false;
int breakpoints[100];
int bp_count = 0;

// --- EXECUTION LOOP ---
void run_debug_loop(bool step_mode) {
    is_paused = false;

    // Log why we are running
    fprintf(stderr, "[MyloDebug] Running... (StepMode: %s)\n", step_mode ? "YES" : "NO");

    while (vm.ip < vm.code_size) {
        int prev_line = vm.lines[vm.ip];

        // Execute one instruction
        int op = vm_step(false);
        if (op == -1) {
            send_event("terminated", "{}");
            exit(0);
        }

        int current_line = vm.lines[vm.ip];

        // Only check for stops if the line number changed
        if (current_line != prev_line) {

            // 1. Check Breakpoints
            for(int i=0; i<bp_count; i++) {
                if (breakpoints[i] == current_line) {
                    fprintf(stderr, "[MyloDebug] Hit Breakpoint at Line %d\n", current_line);
                    send_event("stopped", "{\"reason\": \"breakpoint\", \"threadId\": 1}");
                    is_paused = true;
                    return;
                }
            }

            // 2. Check Step Mode
            if (step_mode) {
                send_event("stopped", "{\"reason\": \"step\", \"threadId\": 1}");
                is_paused = true;
                return;
            }
        }
    }

    send_event("terminated", "{}");
    exit(0);
}
// --- Message Loop ---

void start_debug_adapter(const char* filename, char* source_content) {
    if (filename) strncpy(current_source_path, filename, 4095);
    else strcpy(current_source_path, "unknown.mylo");

    // Store content for later parsing
    pending_source_content = source_content;

    MyloConfig.debug_mode = true;
    MyloConfig.print_callback = debug_print_callback;
    MyloConfig.error_callback = debug_error_callback; // <--- Register it

    char buffer[MAX_PACKET_SIZE];

    while (1) {
        int content_len = 0;
        while (fgets(buffer, sizeof(buffer), stdin)) {
            if (strncmp(buffer, "Content-Length: ", 16) == 0) content_len = atoi(buffer + 16);
            if (strcmp(buffer, "\r\n") == 0) break;
        }

        if (content_len == 0) continue;

        char* body = malloc(content_len + 1);
        fread(body, 1, content_len, stdin);
        body[content_len] = '\0';

        int seq = 0;
        char* seq_ptr = strstr(body, "\"seq\":");
        if (seq_ptr) seq = atoi(seq_ptr + 6);

        char command[64] = {0};
        char* cmd_ptr = strstr(body, "\"command\":");
        if (cmd_ptr) {
            char* start = strchr(cmd_ptr, '"') + 1;
            start = strchr(start, '"') + 1;
            start = strchr(start, '"') + 1;
            char* end = strchr(start, '"');
            strncpy(command, start, end - start);
        }

        if (strcmp(command, "initialize") == 0) {
            send_response(seq, command, "{\"supportsConfigurationDoneRequest\": true}");
            send_event("initialized", "{}");
        }
        else if (strcmp(command, "launch") == 0) {
            if (pending_source_content) {
                mylo_reset();
                parse(pending_source_content);
            }
            send_response(seq, command, "{}");
        }
        else if (strcmp(command, "setBreakpoints") == 0) {
            bp_count = 0;
            char* lines_ptr = strstr(body, "\"lines\":");
            if (lines_ptr) {
                char* p = strchr(lines_ptr, '[');
                while (*p && *p != ']') {
                    if (isdigit(*p)) {
                        breakpoints[bp_count++] = atoi(p);
                        while(isdigit(*p)) p++;
                    } else p++;
                }
            }
            send_response(seq, command, "{\"breakpoints\": []}");
        }
        else if (strcmp(command, "configurationDone") == 0) {
            send_response(seq, command, "{}");

            // FIX: Don't stop immediately. Check if Line 1 is a breakpoint.
            int start_line = vm.lines[vm.ip];
            bool hit_start = false;
            for(int i=0; i<bp_count; i++) {
                if (breakpoints[i] == start_line) hit_start = true;
            }

            if (hit_start) {
                send_event("stopped", "{\"reason\": \"breakpoint\", \"threadId\": 1}");
                is_paused = true;
            } else {
                // Run freely until breakpoint
                run_debug_loop(false);
            }
        }
        else if (strcmp(command, "threads") == 0) {
            send_response(seq, command, "{\"threads\": [{\"id\": 1, \"name\": \"Main Thread\"}]}");
        }
        else if (strcmp(command, "scopes") == 0) {
            // Return Locals (2) and Globals (1)
            send_response(seq, command,
                "{\"scopes\": ["
                "{\"name\": \"Locals\", \"variablesReference\": 2, \"expensive\": false},"
                "{\"name\": \"Globals\", \"variablesReference\": 1, \"expensive\": false}"
                "]}");
        }
        else if (strcmp(command, "stackTrace") == 0) {
            // DYNAMIC STACK TRACE GENERATION
            char json[8192] = "[";
            int frame_id = 0;

            // Start at current state
            int current_fp = vm.fp;
            int current_ip = vm.ip;

            // Safety limit to prevent infinite loops on corrupted stacks
            while (frame_id < 20) {
                if (frame_id > 0) strcat(json, ",");

                int line = vm.lines[current_ip];
                const char* name = get_function_name(current_ip);

                char frame[512];
                snprintf(frame, 512,
                    "{\"id\": %d, \"name\": \"%s\", \"line\": %d, \"column\": 1, \"source\": {\"name\": \"Source\", \"path\": \"%s\"}}",
                    frame_id, name, line, current_source_path);
                strcat(json, frame);

                // WALK UP THE STACK
                // In Mylo: Saved FP is at [FP-1], Saved IP is at [FP-2]
                // If FP is -1 or 0, we are at the bottom (main script body)
                if (current_fp <= 0) break;

                int saved_fp = (int)vm.stack[current_fp - 1];
                int saved_ip = (int)vm.stack[current_fp - 2];

                // Sanity check to ensure we are going "up" (stack grows down/up depending on arch, but indexes usually decrease or match valid ranges)
                if (saved_fp >= current_fp && frame_id > 0) break; // Infinite recursion guard or garbage

                current_fp = saved_fp;
                current_ip = saved_ip;
                frame_id++;
            }
            strcat(json, "]");

            char full[8200];
            snprintf(full, 8200, "{\"totalFrames\": %d, \"stackFrames\": %s}", frame_id + 1, json);
            send_response(seq, command, full);
        }
        else if (strcmp(command, "variables") == 0) {
            char* ref_ptr = strstr(body, "\"variablesReference\":");
            int varRef = ref_ptr ? atoi(ref_ptr + 21) : 0;
            if (varRef == 2) { // LOCALS
                char json[MAX_PACKET_SIZE] = "[";
                bool first = true;

                // DIAGNOSTIC LOG (Check Debug Console in VSCode)
                fprintf(stderr, "[Debug] Scanning Locals for IP: %d (FP: %d, SP: %d)\n", vm.ip, vm.fp, vm.sp);

                for(int i=0; i<debug_symbol_count; i++) {
                    DebugSym* sym = &debug_symbols[i];

                    // Filter: Variable must be alive at this IP
                    if (vm.ip >= sym->start_ip && vm.ip <= sym->end_ip) {

                        int stack_idx = vm.fp + sym->stack_offset;

                        // DIAGNOSTIC: Print found candidates
                        fprintf(stderr, "  > Match: %s (StackIdx: %d)\n", sym->name, stack_idx);

                        if (stack_idx <= vm.sp) {
                            if (!first) strcat(json, ",");
                            first = false;

                            char item[512];
                            double val = vm.stack[stack_idx];
                            int type = vm.stack_types[stack_idx];

                            if (type == T_NUM) {
                                snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"%g\", \"variablesReference\": 0}", sym->name, val);
                            } else if (type == T_STR) {
                                // Simple escape for quotes
                                snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"\\\"string...\\\"\", \"variablesReference\": 0}", sym->name);
                            } else {
                                snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"[Object]\", \"variablesReference\": 0}", sym->name);
                            }
                            strcat(json, item);
                        }
                    }
                }
                strcat(json, "]");
                char full[8250];
                sprintf(full, "{\"variables\": %s}", json);
                send_response(seq, command, full);
            }
            if (varRef == 1) { // GLOBALS
                char json[8192] = "[";
                for(int i=0; i<global_count; i++) {
                    if (i > 0) strcat(json, ",");
                    char item[512];

                    int addr = globals[i].addr;
                    double val = vm.globals[addr];
                    int type = vm.global_types[addr];

                    if (type == T_NUM) {
                        snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"%g\", \"variablesReference\": 0}",
                                 globals[i].name, val);
                    }
                    else if (type == T_STR) {
                        snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"\\\"%s\\\"\", \"variablesReference\": 0}",
                                 globals[i].name, vm.string_pool[(int)val]);
                    }
                    else if (type == T_OBJ) {
                         // FIX: Use vm_resolve_ptr instead of direct heap access
                         double* base = vm_resolve_ptr(val);
                         int objType = base ? (int)base[HEAP_OFFSET_TYPE] : 0;

                         if (base && objType == TYPE_BYTES) {
                             snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"[Bytes]\", \"variablesReference\": 0}", globals[i].name);
                         } else {
                             snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"[Object]\", \"variablesReference\": 0}", globals[i].name);
                         }
                    }
                    else {
                        snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"unknown\", \"variablesReference\": 0}", globals[i].name);
                    }
                    strcat(json, item);
                }
                strcat(json, "]");
                char full[8250];
                sprintf(full, "{\"variables\": %s}", json);
                send_response(seq, command, full);
            }
        }
        else if (strcmp(command, "next") == 0 || strcmp(command, "stepIn") == 0) {
            send_response(seq, command, "{}");
            run_debug_loop(true); // Step Mode = True
        }
        else if (strcmp(command, "continue") == 0) {
            send_response(seq, command, "{}");
            run_debug_loop(false); // Step Mode = False (Run until breakpoint)
        }
        else if (strcmp(command, "disconnect") == 0) {
            send_response(seq, command, "{}");
            exit(0);
        }
        else {
            send_response(seq, command, "{}");
        }

        free(body);
    }
}