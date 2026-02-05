#include "os_job.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper to read a whole file into a string
static char* read_whole_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if(!f) return strdup("");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(sz + 1);
    if(buf) {
        fread(buf, 1, sz, f);
        buf[sz] = '\0';
    }
    fclose(f);
    return buf ? buf : strdup("");
}

// Platform agnostic execution logic
void internal_exec_command(const char* cmd, char** out_str, char** err_str) {
    char stderr_path[256];
    char actual_cmd[4096];

    // 1. Generate unique temp file for stderr
    // Using a random ID to avoid collisions
    int rnd = rand();
#ifdef _WIN32
    sprintf(stderr_path, "%s\\mylo_err_%d.tmp", getenv("TEMP"), rnd);
    // cmd 2> temp_file
    snprintf(actual_cmd, sizeof(actual_cmd), "%s 2> \"%s\"", cmd, stderr_path);
#else
    sprintf(stderr_path, "/tmp/mylo_err_%d.tmp", rnd);
    snprintf(actual_cmd, sizeof(actual_cmd), "%s 2> %s", cmd, stderr_path);
#endif

    // 2. Run with popen to capture stdout directly
    FILE *fp = popen(actual_cmd, "r");

    // 3. Read stdout
    size_t out_cap = 1024;
    size_t out_len = 0;
    char *out_buf = malloc(out_cap);
    out_buf[0] = '\0';

    if (fp) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            size_t chunk_len = strlen(buffer);
            if (out_len + chunk_len >= out_cap) {
                out_cap *= 2;
                out_buf = realloc(out_buf, out_cap);
            }
            strcpy(out_buf + out_len, buffer);
            out_len += chunk_len;
        }
        pclose(fp);
    }
    *out_str = out_buf;

    // 4. Read stderr from temp file
    *err_str = read_whole_file(stderr_path);

    // 5. Cleanup
    remove(stderr_path);
}

// Thread Worker Entry Point
#ifdef _WIN32
unsigned __stdcall job_worker(void* arg) {
#else
void* job_worker(void* arg) {
#endif
    Job* job = (Job*)arg;

    char *o = NULL;
    char *e = NULL;
    internal_exec_command(job->cmd, &o, &e);

    LOCK_JOBS;
    job->out_res = o;
    job->err_res = e;
    job->status = 1; // Done
    UNLOCK_JOBS;

    free(job->cmd); // Free the command string copy
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}