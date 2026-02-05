#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h> // For sleep()
#include "debug_adapter.h"
#include "vm.h"
#include "compiler.h"

#define MAX_PACKET_SIZE 8192
static char current_source_path[4096];
static char* pending_source_content = NULL;

const char* get_function_name(int ip) {
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
    // Simple escape for newlines to prevent JSON breakage
    int j=0;
    char safe_msg[512];
    for(int i=0; msg[i] && j<500; i++) {
        if(msg[i] == '\n') { safe_msg[j++]='\\'; safe_msg[j++]='n'; }
        else if(msg[i] == '"') { safe_msg[j++]='\\'; safe_msg[j++]='"'; }
        else safe_msg[j++] = msg[i];
    }
    safe_msg[j] = '\0';
    snprintf(buf, 1024, "{\"category\": \"stdout\", \"output\": \"%s\\n\"}", safe_msg);
    send_event("output", buf);
}

void debug_error_callback(const char* msg) {
    char buf[1024];
    snprintf(buf, 1024, "{\"category\": \"stderr\", \"output\": \"%s\\n\"}", msg);
    send_event("output", buf);
    send_event("terminated", "{}");
}

bool is_paused = false;
int breakpoints[100];
int bp_count = 0;

void run_debug_loop(bool step_mode) {
    is_paused = false;
    while (vm.ip < vm.code_size) {
        int prev_line = vm.lines[vm.ip];
        int op = vm_step(false);
        if (op == -1) {
            send_event("terminated", "{}");
            exit(0);
        }
        int current_line = vm.lines[vm.ip];
        if (current_line != prev_line) {
            for(int i=0; i<bp_count; i++) {
                if (breakpoints[i] == current_line) {
                    send_event("stopped", "{\"reason\": \"breakpoint\", \"threadId\": 1}");
                    is_paused = true;
                    return;
                }
            }
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

void start_debug_adapter(const char* filename, char* source_content) {
    // UNCOMMENT TO DEBUG THE ADAPTER ITSELF:
    // fprintf(stderr, "Waiting for attach PID %d...\n", getpid()); sleep(10);

    if (filename) strncpy(current_source_path, filename, 4095);
    else strcpy(current_source_path, "unknown.mylo");

    pending_source_content = source_content;
    MyloConfig.debug_mode = true;
    MyloConfig.print_callback = debug_print_callback;
    MyloConfig.error_callback = debug_error_callback;

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
            int start_line = vm.lines[vm.ip];
            bool hit_start = false;
            for(int i=0; i<bp_count; i++) if (breakpoints[i] == start_line) hit_start = true;

            if (hit_start) {
                send_event("stopped", "{\"reason\": \"breakpoint\", \"threadId\": 1}");
                is_paused = true;
            } else {
                run_debug_loop(false);
            }
        }
        else if (strcmp(command, "threads") == 0) {
            send_response(seq, command, "{\"threads\": [{\"id\": 1, \"name\": \"Main Thread\"}]}");
        }
        else if (strcmp(command, "scopes") == 0) {
            send_response(seq, command,
                "{\"scopes\": ["
                "{\"name\": \"Locals\", \"variablesReference\": 2, \"expensive\": false},"
                "{\"name\": \"Globals\", \"variablesReference\": 1, \"expensive\": false}"
                "]}");
        }
        else if (strcmp(command, "stackTrace") == 0) {
            char json[8192] = "[";
            int frame_id = 0;
            int current_fp = vm.fp;
            int current_ip = vm.ip;

            while (frame_id < 20) {
                if (frame_id > 0) strcat(json, ",");
                int line = vm.lines[current_ip];
                const char* name = get_function_name(current_ip);
                char frame[512];
                snprintf(frame, 512, "{\"id\": %d, \"name\": \"%s\", \"line\": %d, \"column\": 1, \"source\": {\"name\": \"Source\", \"path\": \"%s\"}}",
                    frame_id, name, line, current_source_path);
                strcat(json, frame);

                if (current_fp <= 0) break; // Reached root
                int saved_fp = (int)vm.stack[current_fp - 1];
                int saved_ip = (int)vm.stack[current_fp - 2];
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
                for(int i=0; i<debug_symbol_count; i++) {
                    DebugSym* sym = &debug_symbols[i];
                    if (vm.ip >= sym->start_ip && vm.ip <= sym->end_ip) {
                        int stack_idx = vm.fp + sym->stack_offset;
                        if (stack_idx <= vm.sp) {
                            if (!first) strcat(json, ",");
                            first = false;
                            char item[512];
                            double val = vm.stack[stack_idx];
                            int type = vm.stack_types[stack_idx];
                            if (type == T_NUM) snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"%g\", \"variablesReference\": 0}", sym->name, val);
                            else if (type == T_STR) snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"\\\"...\\\"\", \"variablesReference\": 0}", sym->name);
                            else snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"[Object]\", \"variablesReference\": 0}", sym->name);
                            strcat(json, item);
                        }
                    }
                }
                strcat(json, "]");
                char full[8250];
                sprintf(full, "{\"variables\": %s}", json);
                send_response(seq, command, full);
            }
            else if (varRef == 1) { // GLOBALS
                char json[8192] = "[";
                for(int i=0; i<global_count; i++) {
                    if (i > 0) strcat(json, ",");
                    char item[512];
                    int addr = globals[i].addr;
                    double val = vm.globals[addr];
                    int type = vm.global_types[addr];

                    if (type == T_NUM) snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"%g\", \"variablesReference\": 0}", globals[i].name, val);
                    else if (type == T_STR) {
                        const char* s = (val >= 0 && val < vm.str_count) ? vm.string_pool[(int)val] : "<?>";
                        snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"\\\"%s\\\"\", \"variablesReference\": 0}", globals[i].name, s);
                    }
                    else if (type == T_OBJ) {
                         double* base = vm_resolve_ptr_safe(val);
                         if (base) snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"[Object]\", \"variablesReference\": 0}", globals[i].name);
                         else snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"null\", \"variablesReference\": 0}", globals[i].name);
                    }
                    else snprintf(item, 512, "{\"name\": \"%s\", \"value\": \"unknown\", \"variablesReference\": 0}", globals[i].name);
                    strcat(json, item);
                }
                strcat(json, "]");
                char full[8250];
                sprintf(full, "{\"variables\": %s}", json);
                send_response(seq, command, full);
            }
            else send_response(seq, command, "{\"variables\": []}");
        }
        else if (strcmp(command, "next") == 0 || strcmp(command, "stepIn") == 0) {
            send_response(seq, command, "{}");
            run_debug_loop(true);
        }
        else if (strcmp(command, "continue") == 0) {
            send_response(seq, command, "{}");
            run_debug_loop(false);
        }
        else if (strcmp(command, "disconnect") == 0) {
            send_response(seq, command, "{}");
            exit(0);
        }
        else send_response(seq, command, "{}");

        free(body);
    }
}