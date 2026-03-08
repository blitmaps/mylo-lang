#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <setjmp.h>
#include "vm.h"
#include "compiler.h"
#include "mylolib.h"
#include "utils.h"

// Helper to send JSON-RPC responses formatted for LSP
void lsp_send_response(int id, const char* result_json) {
    char body[65536];
    snprintf(body, sizeof(body), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":%s}", id, result_json);
    fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(body), body);
    fflush(stdout); // Crucial: Neovim requires flushing to receive the message
}

void handle_completion(int id, const char* body) {
    char items[65536] = "[";
    bool first = true;

    // 1. Standard Library Functions
    // std_library is defined in mylolib.h / mylolib.c
    for (int i = 0; std_library[i].name != NULL; i++) {
        if (!first) strcat(items, ",");
        char item[256];
        // kind: 3 represents a Function/Method in LSP
        snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":3}", std_library[i].name);
        strcat(items, item);
        first = false;
    }

    // 2. User Defined Symbols
    // Extract the file URI to read the user's current code
    char* uri_start = (char *)strstr(body, "\"uri\":\"file://");
    if (uri_start) {
        uri_start += 14; 
        char* uri_end = strchr(uri_start, '"');
        if (uri_end) {
            char filepath[1024] = {0};
            strncpy(filepath, uri_start, uri_end - uri_start);

            // Read the file and parse it to populate the compiler's symbol tables
            char* source = read_file(filepath); 
            if (source) {
                VM vm;
                vm_init(&vm); 
                compiler_reset(); 
                
                jmp_buf lsp_env;
                MyloConfig.repl_jmp_buf = &lsp_env; 
                // Note: If parse() calls exit() on a syntax error (e.g., while the user is typing),
                // the LSP will die. Neovim will automatically restart it, but for a production 
                // server, you should adapt parse() to return an error code instead.
                if (setjmp(lsp_env) == 0) {
                    parse(&vm, source); 
                }

                // User Functions
                for (int i = 0; i < func_count; i++) {
                    if (!first) strcat(items, ",");
                    char item[256];
                    snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":3}", funcs[i].name);
                    strcat(items, item);
                    first = false;
                }

                // User Global Variables
                for (int i = 0; i < global_count; i++) {
                    if (!first) strcat(items, ",");
                    char item[256];
                    // kind: 6 represents a Variable in LSP
                    snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":6}", globals[i].name);
                    strcat(items, item);
                    first = false;
                }

                // User Local Variables
                for (int i = 0; i < local_count; i++) {
                    if (!first) strcat(items, ",");
                    char item[256];
                    snprintf(item, sizeof(item), "{\"label\":\"%s\",\"kind\":6}", locals[i].name);
                    strcat(items, item);
                    first = false;
                }

                vm_cleanup(&vm);
                free(source);
            }
        }
    }

    strcat(items, "]");
    lsp_send_response(id, items);
}

int main() {
    char line[1024];
    int content_length = 0;

    // Listen for incoming LSP commands via stdin
    while (fgets(line, sizeof(line), stdin)) {
        if (strncmp(line, "Content-Length: ", 16) == 0) {
            content_length = atoi(line + 16);
        }
        
        // An empty \r\n marks the end of headers and the start of the JSON body
        if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0) {
            if (content_length > 0) {
                char* body = malloc(content_length + 1);
                fread(body, 1, content_length, stdin);
                body[content_length] = '\0';

                // Extract Request ID
                int id = -1;
                char* id_ptr = strstr(body, "\"id\":");
                if (id_ptr) id = atoi(id_ptr + 5);

                // Handle 'initialize'
                if (strstr(body, "\"method\":\"initialize\"")) {
                    const char* init_res = "{"
                        "\"capabilities\":{"
                        "\"textDocumentSync\": 1,"
                            "\"completionProvider\":{\"resolveProvider\":false}"
                        "}"
                    "}";
                    lsp_send_response(id, init_res);
                } 
                // Handle 'completion'
                else if (strstr(body, "\"method\":\"textDocument/completion\"")) {
                    handle_completion(id, body);
                }

                free(body);
                content_length = 0;
            }
        }
    }
    return 0;
}
