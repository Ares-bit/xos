#include "print.h"
#include "init.h"
#include "debug.h"
#include "stdint.h"
#include "memory.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "keyboard.h"
#include "ioqueue.h"
#include "process.h"
#include "syscall.h"
#include "syscall_init.h"
#include "stdio.h"
#include "fs.h"
#include "string.h"
#include "dir.h"
#include "shell.h"
#include "assert.h"
#include "stdio_kernel.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);
int prog_a_pid = 0, prog_b_pid = 0;

int main(void) {
    put_str("I am kernel\n");
    init_all();

    cls_screen();
#if 0
    //写入prog_no_arg
    uint32_t file_size = 5556;
    uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);
    struct disk* sda = &channels[0].devices[0];
    void* prog_buf = sys_malloc(file_size);
    ide_read(sda, 300, prog_buf, sec_cnt);
    int32_t fd = sys_open("/pipe", O_CREAT|O_RDWR);
    if (fd != -1) {
        if (sys_write(fd, prog_buf, file_size) == -1) {
            printk("file write error!\n");
            while(1);
        }
    }
#endif
#if 0
    //不知道为什么只有CREATE的时候才能向文件写入数据，正常追加写不好使。。。
    //应该是file.c:201创建的时候传入create|rdwr把wirte deny锁死了，除了第一次每次都写不进去
    int32_t fd1;
    console_put_str("write start!\n");
    fd1 = sys_open("/file1", O_RDWR);
    if (fd1 != -1) {
        sys_write(fd1, "Hello world!12345678\n", 22);
        sys_close(fd1);
        console_put_str("write succ!\n");
    } else {
        console_put_str("open file1 failed!\n");
    }
    console_put_str("write end!\n");
#endif
    console_put_str("[xxy@localhost /]$ ");
    //while (1);
    thread_exit(running_thread(), true);
    return 0;
}

void init(void)
{
    uint32_t ret_pid = fork();
    if (ret_pid) {
        //while(1);
        int status;
        int child_pid;
        //init在此不停回收僵尸进程
        while (1) {
            child_pid = wait(&status);
            printf("I`m init, My pid is 1, I recieve a child, It's pid is %d, status is %d\n", child_pid, status);
        }
    } else {
        my_shell();
    }
    panic("init: should not be here!");
}

void k_thread_a(void* arg) {
    char* para = arg;
    void* addr1 = sys_malloc(256);
    void* addr2 = sys_malloc(255);
    void* addr3 = sys_malloc(254);
    console_put_str("thread_a malloc addr:0x");
    console_put_int((int)addr1);
    console_put_char(',');
    console_put_int((int)addr2);
    console_put_char(',');
    console_put_int((int)addr3);
    console_put_char('\n');

    int cpu_delay = 100000;
    while (cpu_delay-- > 0);
    sys_free(addr1);
    sys_free(addr2);
    sys_free(addr3);
    while(1);
}

void k_thread_b(void* arg) {
    char* para = arg;
    void* addr1 = sys_malloc(256);
    void* addr2 = sys_malloc(255);
    void* addr3 = sys_malloc(254);
    console_put_str("thread_b malloc addr:0x");
    console_put_int((int)addr1);
    console_put_char(',');
    console_put_int((int)addr2);
    console_put_char(',');
    console_put_int((int)addr3);
    console_put_char('\n');

    int cpu_delay = 100000;
    while (cpu_delay-- > 0);
    sys_free(addr1);
    sys_free(addr2);
    sys_free(addr3);
    while(1);
}

void u_prog_a(void) {
    void* addr1 = malloc(256);
    void* addr2 = malloc(255);
    void* addr3 = malloc(254);
    printf("prog_a malloc addr:0x%x,0x%x,0x%x\n",(int)addr1,(int)addr2,(int)addr3);

    int cpu_delay = 100000;
    while (cpu_delay-- > 0);
    free(addr1);
    free(addr2);
    free(addr3);
    while(1);
}

void u_prog_b(void) {
    void* addr1 = malloc(256);
    void* addr2 = malloc(255);
    void* addr3 = malloc(254);
    printf("prog_b malloc addr:0x%x,0x%x,0x%x\n",(int)addr1,(int)addr2,(int)addr3);

    int cpu_delay = 100000;
    while (cpu_delay-- > 0);
    free(addr1);
    free(addr2);
    free(addr3);
    while(1);
}