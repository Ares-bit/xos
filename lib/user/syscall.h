#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H
#include "stdint.h"
#include "thread.h"

enum SYSCALL_NR {
    SYS_GETPID,
    SYS_WRITE,
    SYS_MALLOC,
    SYS_FREE,
    SYS_FORK,
    SYS_READ,
    SYS_PUTCHAR,
    SYS_CLEAR
};

pid_t getpid(void);
int32_t write(int32_t fd, const void* buf, uint32_t count);
void* malloc(uint32_t size);
void free(void* ptr);
pid_t fork(void);
int32_t read(int32_t fd, void* buf, uint32_t count);
void sys_putchar(char char_asci);
#endif