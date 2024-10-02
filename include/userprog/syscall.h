#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

void syscall_init (void);
void close (int fd);
int wait(int pid);

struct lock filesys_lock3;

#endif /* userprog/syscall.h */