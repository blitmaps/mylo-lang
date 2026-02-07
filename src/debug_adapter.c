#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#ifdef _WIN32
    #include <windows.h>
    #define sleep(x) Sleep(1000 * (x))
#else
    #include <unistd.h>
#endif
#include "debug_adapter.h"
#include "vm.h"
#include "compiler.h" // For global symbol definition if needed

#define MAX_PACKET_SIZE 8192
static char current_source_path[4096];
static char* pending_source_content = NULL;

const char* get_function_name(VM* vm, int ip) {
    const char* best_name = "main";
    int best_addr = -1;
    // Uses the global function registry from compiler.h, which is static state
    // If we wanted to be pure VM, we should use vm->functions
    for(int i=0; i<vm->function_count; i++) {
        if (vm->functions[i].addr <= ip && vm->functions[i].addr > best_addr) {
            best_name = vm->functions[i].name;
            best_addr = vm->functions[i].addr;
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
    snprintf(buf, 4096, "{\"jsonrpc\": \"2.0\", \"type\": \"event\", \"event\": \"%s\", \"body\": %s}", event, body_json);
    send_json(buf);
}

void send_response(int seq, const char* command, const char* body_json) {
    char buf[8192];
    snprintf(buf, 8192, "{\"jsonrpc\": \"2.0\", \"type\": \"response\", \"request_seq\": %d, \"command\": \"%s\", \"success\": true, \"body\": %s}", seq, command, body_json);
    send_json(buf);
}

// --- Protocol Loop ---

void run_debug_loop(VM* vm, bool step_mode) {
    // If in step mode, we execute one instruction then stop
    if (step_mode) {
        // Send Stopped Event
        send_event("stopped", "{\"reason\": \"step\", \"threadId\": 1, \"allThreadsStopped\": true}");
    } else {
        // We are launching/continuing, so we run until breakpoint or end
        // Simple blocking run for now
        // Send Stopped on entry? No, usually 'initialized' does that.
    }

    // Input Loop
    char header[1024];
    while (1) {
        // Simple blocking read of headers
        // Real implementations use select/poll, here we just assume lock-step for simplicity
        if (step_mode) {
             // In step mode we don't block on VM, we wait for DAP command
        } else {
             // Run VM step
             int res = vm_step(vm, false);
             if (res == -1) {
                 send_event("terminated", "{}");
                 exit(0);
             }
             // Check Breakpoints
             // (Requires compiler line mapping which is in VM now)
             // ...
             // If breakpoint hit:
             // step_mode = true;
             // send_event("stopped", "{\"reason\": \"breakpoint\"}");
             continue; // Keep running
        }

        // Read Header
        if (!fgets(header, sizeof(header), stdin)) break;
        if (strncmp(header, "Content-Length: ", 16) == 0) {
            int len = atoi(header + 16);
            while(fgets(header, sizeof(header), stdin) && strcmp(header, "\r\n") != 0); // Skip other headers

            char* body = malloc(len + 1);
            fread(body, 1, len, stdin);
            body[len] = 0;

            // Very Basic Parser
            int seq = 0;
            char command[64] = {0};

            char* seq_ptr = strstr(body, "\"seq\"");
            if (seq_ptr) seq = atoi(seq_ptr + 6); // Approximation

            char* cmd_ptr = strstr(body, "\"command\"");
            if (cmd_ptr) {
                char* start = strchr(cmd_ptr, ':');
                if (start) {
                    start = strchr(start, '"');
                    if (start) {
                        start++;
                        char* end = strchr(start, '"');
                        if (end) {
                            strncpy(command, start, end - start);
                        }
                    }
                }
            }

            // Handle Commands
            if (strcmp(command, "initialize") == 0) {
                send_response(seq, command, "{\"supportsConfigurationDoneRequest\": true}");
                send_event("initialized", "{}");
            }
            else if (strcmp(command, "launch") == 0) {
                send_response(seq, command, "{}");
                // Stop at start?
                send_event("stopped", "{\"reason\": \"entry\", \"threadId\": 1}");
                step_mode = true;
            }
            else if (strcmp(command, "setBreakpoints") == 0) {
                 // TODO: Parse breakpoints
                 send_response(seq, command, "{\"breakpoints\": []}");
            }
            else if (strcmp(command, "threads") == 0) {
                send_response(seq, command, "{\"threads\": [{\"id\": 1, \"name\": \"Main Thread\"}]}");
            }
            else if (strcmp(command, "stackTrace") == 0) {
                char stack[4096];
                int line = vm->lines[vm->ip > 0 ? vm->ip - 1 : 0];
                const char* fn = get_function_name(vm, vm->ip);
                // We send a single frame for simplicity in this basic adapter
                snprintf(stack, 4096, "{\"stackFrames\": [{\"id\": 1, \"name\": \"%s\", \"source\": {\"name\": \"%s\", \"path\": \"%s\"}, \"line\": %d, \"column\": 1}], \"totalFrames\": 1}",
                    fn, "source.mylo", current_source_path, line);
                send_response(seq, command, stack);
            }
            else if (strcmp(command, "scopes") == 0) {
                send_response(seq, command, "{\"scopes\": [{\"name\": \"Locals\", \"variablesReference\": 1, \"expensive\": false}, {\"name\": \"Globals\", \"variablesReference\": 2, \"expensive\": false}]}");
            }
            else if (strcmp(command, "variables") == 0) {
                // Parse ref
                int ref = 0;
                char* ref_ptr = strstr(body, "\"variablesReference\"");
                if (ref_ptr) ref = atoi(ref_ptr + 21);

                char json[8192] = "[";
                if (ref == 1) { // Locals
                    bool first = true;
                    // Use VM local symbols
                    if (vm->local_symbols) {
                        for(int i=0; i<vm->local_symbol_count; i++) {
                             VMLocalInfo* sym = &vm->local_symbols[i];
                             if ((vm->ip-1) >= sym->start_ip && (sym->end_ip == -1 || (vm->ip-1) <= sym->end_ip)) {
                                 int stack_idx = vm->fp + sym->stack_offset;
                                 if (stack_idx <= vm->sp) {
                                     double val = vm->stack[stack_idx];
                                     int type = vm->stack_types[stack_idx];
                                     if (!first) strcat(json, ",");
                                     char item[512];
                                     if (type == T_NUM) snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"%g\", \"variablesReference\": 0}", sym->name, val);
                                     else if (type == T_STR) {
                                         const char* s = vm->string_pool[(int)val];
                                         snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"\\\"%s\\\"\", \"variablesReference\": 0}", sym->name, s);
                                     }
                                     else snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"[Object]\", \"variablesReference\": 0}", sym->name);
                                     strcat(json, item);
                                     first = false;
                                 }
                             }
                        }
                    }
                } else if (ref == 2) { // Globals
                    bool first = true;
                    if (vm->global_symbols) {
                         for(int i=0; i<vm->global_symbol_count; i++) {
                             if (!first) strcat(json, ",");
                             int addr = vm->global_symbols[i].addr;
                             double val = vm->globals[addr];
                             int type = vm->global_types[addr];
                             char item[512];
                             if (type == T_NUM) snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"%g\", \"variablesReference\": 0}", vm->global_symbols[i].name, val);
                             else if (type == T_STR) {
                                  const char* s = vm->string_pool[(int)val];
                                  snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"\\\"%s\\\"\", \"variablesReference\": 0}", vm->global_symbols[i].name, s);
                             }
                             else snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"[Object]\", \"variablesReference\": 0}", vm->global_symbols[i].name);
                             strcat(json, item);
                             first = false;
                         }
                    }
                }
                strcat(json, "]");
                char full[8250];
                sprintf(full, "{\"variables\": %s}", json);
                send_response(seq, command, full);
            }
            else if (strcmp(command, "next") == 0 || strcmp(command, "stepIn") == 0) {
                send_response(seq, command, "{}");
                int op = vm_step(vm, false);
                if (op == -1) {
                     send_event("terminated", "{}");
                     exit(0);
                }
                send_event("stopped", "{\"reason\": \"step\", \"threadId\": 1}");
            }
            else if (strcmp(command, "continue") == 0) {
                send_response(seq, command, "{}");
                step_mode = false;
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
}

void start_debug_adapter(VM* vm, const char* filename, char* source_content) {
    if (filename) strcpy(current_source_path, filename);
    else strcpy(current_source_path, "unknown.mylo");

    pending_source_content = source_content;

    // We start in a fake step mode to handle the handshake
    run_debug_loop(vm, true);
}