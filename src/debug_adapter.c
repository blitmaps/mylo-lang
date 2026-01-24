#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>          // <--- Added for isdigit
#include "debug_adapter.h"
#include "vm.h"
#include "compiler.h"       // <--- Now provides 'Symbol' and 'globals'

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
    // Send standard output event to editor console
    char buf[1024];
    snprintf(buf, 1024, "{\"category\": \"stdout\", \"output\": \"%s\\n\"}", msg);
    send_event("output", buf);
}

// --- State ---
bool is_paused = false;
int breakpoints[100];
int bp_count = 0;

void handle_step(int seq) {
    // Perform one VM step
    int op = vm_step(false);

    if (op == -1) { // OP_HLT
        send_event("terminated", "{}");
        exit(0);
    }

    // Check if we hit a breakpoint AFTER the step
    bool hit = false;
    for(int i=0; i<bp_count; i++) {
        // Simple mapping: Bytecode Index -> Source Line
        // If the current IP corresponds to a line with a breakpoint...
        if (vm.lines[vm.ip] == breakpoints[i]) {
            hit = true;
            break;
        }
    }

    if (hit) {
        char buf[128];
        snprintf(buf, 128, "{\"reason\": \"breakpoint\", \"threadId\": 1}");
        send_event("stopped", buf);
        is_paused = true;
    } else {
        // If we are "stepping", we stop after every instruction (or every line change)
        // For simple "step over", we can check if line changed.
        // For now, let's implement "Step" as "Step Instruction" for simplicity,
        // or just send "stopped" immediately implies "Step complete".

        char buf[128];
        snprintf(buf, 128, "{\"reason\": \"step\", \"threadId\": 1}");
        send_event("stopped", buf);
        is_paused = true;
    }
}

// --- Message Loop ---

void start_debug_adapter() {
    MyloConfig.debug_mode = true;
    MyloConfig.print_callback = debug_print_callback;

    // We need to buffer stdin to read headers
    char buffer[8192];

    while (1) {
        // 1. Read Header "Content-Length: <n>"
        int content_len = 0;
        while (fgets(buffer, sizeof(buffer), stdin)) {
            if (strncmp(buffer, "Content-Length: ", 16) == 0) {
                content_len = atoi(buffer + 16);
            }
            if (strcmp(buffer, "\r\n") == 0) break; // End of headers
        }

        if (content_len == 0) continue;

        // 2. Read Body
        char* body = malloc(content_len + 1);
        fread(body, 1, content_len, stdin);
        body[content_len] = '\0';

        // 3. Simple Parsing (Extract 'command' and 'seq')
        // NOTE: In a real app, use a JSON parser. Here we use strstr hacks.

        int seq = 0;
        char* seq_ptr = strstr(body, "\"seq\":");
        if (seq_ptr) seq = atoi(seq_ptr + 6);

        char command[64] = {0};
        char* cmd_ptr = strstr(body, "\"command\":");
        if (cmd_ptr) {
            char* start = strchr(cmd_ptr, '"') + 1; // "command"
            start = strchr(start, '"') + 1; // :
            start = strchr(start, '"') + 1; // start of value
            char* end = strchr(start, '"');
            strncpy(command, start, end - start);
        }

        // 4. Dispatch
        if (strcmp(command, "initialize") == 0) {
            send_response(seq, command, "{\"supportsConfigurationDoneRequest\": true}");
            send_event("initialized", "{}");
        }
        else if (strcmp(command, "launch") == 0) {
            // In launch, we normally parse arguments to find the file.
            // For now, assume the file was passed via CLI args to mylo.
            send_response(seq, command, "{}");
            // Don't run yet, wait for configDone
        }
        else if (strcmp(command, "setBreakpoints") == 0) {
            // Simple Parse: Find "lines": [1, 2, 3]
            bp_count = 0;
            char* lines_ptr = strstr(body, "\"lines\":");
            if (lines_ptr) {
                char* p = strchr(lines_ptr, '[');
                while (*p && *p != ']') {
                    if (isdigit(*p)) {
                        breakpoints[bp_count++] = atoi(p);
                        while(isdigit(*p)) p++;
                    } else {
                        p++;
                    }
                }
            }
            // Acknowledge (Mocking the response structure)
            send_response(seq, command, "{\"breakpoints\": []}");
        }
        else if (strcmp(command, "configurationDone") == 0) {
            send_response(seq, command, "{}");
            // Stop on entry
            send_event("stopped", "{\"reason\": \"entry\", \"threadId\": 1}");
            is_paused = true;
        }
        else if (strcmp(command, "threads") == 0) {
            send_response(seq, command, "{\"threads\": [{\"id\": 1, \"name\": \"Main Thread\"}]}");
        }
        else if (strcmp(command, "stackTrace") == 0) {
            // Report current position
            // line is vm.lines[vm.ip]
            // We need the file name. For now hardcode or use global 'current_filename'
            int line = vm.lines[vm.ip];
            char buf[512];
            snprintf(buf, 512, "{\"totalFrames\": 1, \"stackFrames\": [{\"id\": 0, \"name\": \"main\", \"line\": %d, \"column\": 1, \"source\": {\"name\": \"Source\", \"path\": \"/path/to/source.mylo\"}}]}", line);
            send_response(seq, command, buf);
        }
        else if (strcmp(command, "scopes") == 0) {
            // Return "Globals" (ref 1) and "Locals" (ref 2)
            send_response(seq, command, "{\"scopes\": [{\"name\": \"Globals\", \"variablesReference\": 1, \"expensive\": false}]}");
        }
        else if (strcmp(command, "variables") == 0) {
            // Check varRef
            char* ref_ptr = strstr(body, "\"variablesReference\":");
            int varRef = ref_ptr ? atoi(ref_ptr + 21) : 0;

            if (varRef == 1) {
                // GLOBALS
                // Build JSON list of globals
                // Warning: buffer size limit. In prod use dynamic string.
                char json[8192] = "[";
                for(int i=0; i<global_count; i++) {
                    if (i > 0) strcat(json, ",");
                    char item[256];
                    // Retrieve value from VM
                    double val = vm.globals[globals[i].addr];
                    // Simple logic for display
                    snprintf(item, 256, "{\"name\": \"%s\", \"value\": \"%g\", \"variablesReference\": 0}",
                             globals[i].name, val);
                    strcat(json, item);
                }
                strcat(json, "]");

                // Wrap in object
                char full[8250];
                sprintf(full, "{\"variables\": %s}", json);
                send_response(seq, command, full);
            } else {
                send_response(seq, command, "{\"variables\": []}");
            }
        }
        else if (strcmp(command, "next") == 0 || strcmp(command, "stepIn") == 0) {
            send_response(seq, command, "{}");
            handle_step(seq);
        }
        else if (strcmp(command, "continue") == 0) {
            send_response(seq, command, "{}");
            // Run until breakpoint
            is_paused = false;
            while (!is_paused) {
                handle_step(seq); // This will pause if BP hit
                if (vm.ip >= vm.code_size) break;
            }
        }
        else if (strcmp(command, "disconnect") == 0) {
            send_response(seq, command, "{}");
            exit(0);
        }
        else {
            // Unknown
            send_response(seq, command, "{}");
        }

        free(body);
    }
}