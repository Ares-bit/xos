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

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);
int prog_a_pid = 0, prog_b_pid = 0;

int main(void) {
    put_str("I am kernel\n");
    init_all();
    intr_enable();
    //两个消费者线程
    process_execute(u_prog_a, "user_prog_a");
    process_execute(u_prog_b, "user_prog_b");
    console_put_str("main_pid:0x");
    console_put_int(sys_getpid());
    console_put_char('\n');
    thread_start("consumer_a", 31, k_thread_a, "argA");
    thread_start("consumer_b", 31, k_thread_b, "argB");   

#if 0
    sys_mkdir("/dir1");
    sys_mkdir("/dir1/subdir1");
    uint32_t fd = sys_open("/dir1/subdir1/file1", O_CREAT);//创建的文件会直接加到file table中
    sys_close(fd);

    struct stat obj_stat;

    sys_stat("/", &obj_stat);
    printf("/ info:\ni_no:%d  size:%d  filetype:%s\n", obj_stat.st_ino, obj_stat.st_size, obj_stat.st_filetype == FT_REGULAR ? "regular" : "directory");

    sys_stat("/dir1/", &obj_stat);
    printf("/dir1/ info:\ni_no:%d  size:%d  filetype:%s\n", obj_stat.st_ino, obj_stat.st_size, obj_stat.st_filetype == FT_REGULAR ? "regular" : "directory");
   
    sys_stat("/dir1/subdir1", &obj_stat);
    printf("/dir1/subdir1 info:\ni_no:%d  size:%d  filetype:%s\n", obj_stat.st_ino, obj_stat.st_size, obj_stat.st_filetype == FT_REGULAR ? "regular" : "directory");
   
    sys_stat("/dir1/subdir1/file1", &obj_stat);
    printf("/dir1/subdir1/file1 info:\ni_no:%d  size:%d  filetype:%s\n", obj_stat.st_ino, obj_stat.st_size, obj_stat.st_filetype == FT_REGULAR ? "regular" : "directory");
#endif
    while (1);

    return 0;
}

void init(void)
{
    uint32_t ret_pid = fork();
    if (ret_pid) {
        printf("i am father, my pid is:%d, child pid is:%d\n", getpid(), ret_pid);
    } else {
        printf("i am child, my pid is:%d, ret pid is:%d\n", getpid(), ret_pid);
    }
    while(1);
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