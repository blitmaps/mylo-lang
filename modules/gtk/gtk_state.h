#ifndef GTK_STATE_H
#define GTK_STATE_H

#include <gtk/gtk.h>
#include "vm.h" // Required to access the Mylo VM functions

// Shared GTK Builder instance
static GtkBuilder* g_builder = NULL;

// C struct to pass callback details and API pointers
typedef struct {
    VM* vm;
    char func_name[64];
    
    // Pointers to the Mylo VM API
    void (*push_func)(VM*, double, int);
    int (*find_func)(VM*, const char*);
    void (*exec_func)(VM*, int, bool);
} SignalPayload;

// Generic C handler that dispatches execution into the Mylo VM
static void on_gtk_dispatch(GtkWidget* widget, gpointer user_data) {
    SignalPayload* p = (SignalPayload*)user_data;
    
    // Use the injected function pointer to find the Mylo address
    int addr = p->find_func(p->vm, p->func_name);
    if (addr != -1) {
        // Use injected push pointers
        p->push_func(p->vm, (double)p->vm->code_size, T_NUM);
        p->push_func(p->vm, (double)p->vm->fp, T_NUM);
        
        p->vm->fp = p->vm->sp + 1; 
        
        // Use injected execution pointer
        p->exec_func(p->vm, addr, false);
    }
}

#endif
