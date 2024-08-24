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

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);
int prog_a_pid = 0, prog_b_pid = 0;

int main(void) {
    put_str("I am kernel\n");
    init_all();
    //打开时钟中断
	intr_enable();

    process_execute(u_prog_a, "u_prog_a");
    process_execute(u_prog_b, "u_prog_b");
    thread_start("k_thread_a", 31, k_thread_a, "I am thread_a");
    thread_start("k_thread_b", 31, k_thread_b, "I am thread_b");

    sys_open("/file4", O_CREAT);
    uint32_t fd = sys_open("/file4", O_RDWR);
    printf("fd:%d\n", fd);
    sys_write(fd, "hello, world\n", 13);
    sys_close(fd);
    printf("%d closed now\n", fd);

    while (1);

    return 0;
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