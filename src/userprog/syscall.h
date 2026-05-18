#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

extern struct lock fs_lock; 

void syscall_init (void);

#endif /* userprog/syscall.h */