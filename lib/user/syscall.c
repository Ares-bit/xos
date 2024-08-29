#include "syscall.h"

#define _syscall0(NUMBER)           \
({                                   \
    int retval;                     \
    asm volatile("int $0x80" \
    : "=a"(retval) \
    : "0"(NUMBER) \
    : "memory");                      \
    retval; \
})//大括号表达式返回值为最后一个表达式的值 

#define _syscall1(NUMBER, ARG1)     \
({                                   \
    int retval;                     \
    asm volatile("int $0x80"     \
    : "=a"(retval)  \
    : "0"(NUMBER), "b"(ARG1)    \
    : "memory");                 \
    retval;                     \
})

#define _syscall2(NUMBER, ARG1, ARG2)     \
({                                   \
    int retval;                     \
    asm volatile("int $0x80"      \
    : "=a"(retval) \
    : "0"(NUMBER), "b"(ARG1), "c"(ARG2) \
    : "memory");                       \
    retval; \
})

#if 1
#define _syscall3(NUMBER, ARG1, ARG2, ARG3)     \
({                                   \
    int retval;                     \
    asm volatile("int $0x80"     \
    : "=a"(retval) \
    : "0"(NUMBER), "b"(ARG1), "c"(ARG2), "d"(ARG3) \
    : "memory");                       \
    retval;     \
})
#else
#define _syscall3(NUMBER, ARG1, ARG2, ARG3)     \
({                                   \
    int retval;                     \
    asm volatile( \
    "pushl %[arg2]; pushl %[arg1]; pushl %[arg0];" \
    "pushl %[number]; int $0x80; addl $16, %%esp"     \ //这里把esp加回去是为了执行int 0x80后清3级栈
    : "=a"(retval) \
    : [number]"i"(NUMBER), [arg0]"b"(ARG1), [arg1]"c"(ARG2), [arg2]"d"(ARG3) \
    : "memory");                       \
    retval;     \
})
#endif

uint32_t getpid(void) {
    return _syscall0(SYS_GETPID);
}

uint32_t write(int32_t fd, const void* buf, uint32_t count) {
    return _syscall3(SYS_WRITE, fd, buf, count);
}

void* malloc(uint32_t size) {
    return (void*)_syscall1(SYS_MALLOC, size);
}

void free(void* ptr) {
    return _syscall1(SYS_FREE, ptr);
}

pid_t fork(void) {
    return _syscall0(SYS_FORK);
}