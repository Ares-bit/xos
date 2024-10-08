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

pid_t getpid(void) {
    return _syscall0(SYS_GETPID);
}

int32_t write(int32_t fd, const void* buf, uint32_t count) {
    return _syscall3(SYS_WRITE, fd, buf, count);
}

void* malloc(uint32_t size) {
    return (void*)_syscall1(SYS_MALLOC, size);
}

void free(void* ptr) {
    _syscall1(SYS_FREE, ptr);
}

pid_t fork(void) {
    return _syscall0(SYS_FORK);
}

int32_t read(int32_t fd, void* buf, uint32_t count)
{
    return _syscall3(SYS_READ, fd, buf, count);
}

void putchar(char char_asci)
{
    _syscall1(SYS_PUTCHAR, char_asci);
}

void clear(void) {
    _syscall0(SYS_CLEAR);
}

char* getcwd(char* buf, uint32_t size)
{
    //指针类型一定要转换吗？不会根据函数声明自己转换吗？
    return (char*)_syscall2(SYS_GETCWD, buf, size);
}

//flag为什么不用枚举呢
int32_t open(char* pathname, uint8_t flag)
{
    return _syscall2(SYS_OPEN, pathname, flag);
}

int32_t close(int32_t fd)
{
    return _syscall1(SYS_CLOSE, fd);
}

int32_t lseek(int32_t fd, int32_t offset, uint8_t whence)
{
    return _syscall3(SYS_LSEEK, fd, offset, whence);
}

int32_t unlink(const char* pathname)
{
    return _syscall1(SYS_UNLINK, pathname);
}

int32_t mkdir(const char* pathname)
{
    return _syscall1(SYS_MKDIR, pathname);
}

struct dir* opendir(const char* name)
{
    return (struct dir*)_syscall1(SYS_OPENDIR, name);
}

int32_t closedir(struct dir* dir)
{
    return _syscall1(SYS_CLOSEDIR, dir);
}

int32_t rmdir(const char* pathname)
{
    return _syscall1(SYS_RMDIR, pathname);
}

struct dir_entry* readdir(struct dir* dir)
{
    return (struct dir_entry*)_syscall1(SYS_READDIR, dir);
}

void rewinddir(struct dir* dir)
{
    _syscall1(SYS_REWINDDIR, dir);
}

int32_t stat(const char* path, struct stat* buf)
{
    return _syscall2(SYS_STAT, path, buf);
}

int32_t chdir(const char* path)
{
    return _syscall1(SYS_CHDIR, path);
}

void ps(void)
{
    _syscall0(SYS_PS);
}

int32_t execv(const char* path, char** argv)
{
    return _syscall2(SYS_EXECV, path, argv);
}

pid_t wait(int32_t* status)
{
    return _syscall1(SYS_WAIT, status);
}

void exit(int32_t status)
{
    _syscall1(SYS_EXIT, status);
}

int32_t pipe(int32_t pipefd[2])
{
    return _syscall1(SYS_PIPE, pipefd);
}

int32_t fd_redirect(uint32_t old_local_fd, uint32_t new_local_fd)
{
    return _syscall2(SYS_FD_REDIRECT, old_local_fd, new_local_fd);
}

void help(void)
{
    _syscall0(SYS_HELP);
}