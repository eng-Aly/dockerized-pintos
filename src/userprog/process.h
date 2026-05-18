#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h" // <-- for semaphore
/* Process management. */
struct exec_info
{
    char *file_name;

    struct semaphore load_sema;

    bool load_success;

    tid_t tid;
};
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
