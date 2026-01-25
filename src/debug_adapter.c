#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include "debug_adapter.h"
#include "vm.h"
#include "compiler.h"

static char current_source_path[4096];

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

void start_debug_adapter(const char* filename) {
    if (filename) strncpy(current_source_path, filename, 4095);
    else strcpy(current_source_path, "unknown.mylo");

    MyloConfig.debug_mode = true;
    MyloConfig.print_callback = debug_print_callback;

    char buffer[8192];

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
        else if (strcmp(command, "stackTrace") == 0) {
            int line = vm.lines[vm.ip];
            char buf[1024];
            snprintf(buf, 1024, "{\"totalFrames\": 1, \"stackFrames\": [{\"id\": 0, \"name\": \"main\", \"line\": %d, \"column\": 1, \"source\": {\"name\": \"Source\", \"path\": \"%s\"}}]}", line, current_source_path);
            send_response(seq, command, buf);
        }
        else if (strcmp(command, "scopes") == 0) {
            send_response(seq, command, "{\"scopes\": [{\"name\": \"Globals\", \"variablesReference\": 1, \"expensive\": false}]}");
        }
        else if (strcmp(command, "variables") == 0) {
            char* ref_ptr = strstr(body, "\"variablesReference\":");
            int varRef = ref_ptr ? atoi(ref_ptr + 21) : 0;

            if (varRef == 1) {
                char json[8192] = "[";
                for(int i=0; i<global_count; i++) {
                    if (i > 0) strcat(json, ",");
                    char item[256];
                    double val = vm.globals[globals[i].addr];
                    snprintf(item, 256, "{\"name\": \"%s\", \"value\": \"%g\", \"variablesReference\": 0}", globals[i].name, val);
                    strcat(json, item);
                }
                strcat(json, "]");
                char full[8250];
                sprintf(full, "{\"variables\": %s}", json);
                send_response(seq, command, full);
            } else {
                send_response(seq, command, "{\"variables\": []}");
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