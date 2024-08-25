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

    sys_write(fd, "hello, world\n", 13);
    sys_lseek(fd, 0, SEEK_SET);//写完文件pos跑到最后去了，需要给定位回去下边才能读到

    printf("fd:%d\n", fd);
    char buf[64] = {0};
    uint32_t read_bytes = sys_read(fd, buf, 18);
    printf("1 read %d bytes:\n%s\n", read_bytes, buf);

    memset(buf, 0, 64);
    read_bytes = sys_read(fd, buf, 6);
    printf("2 read %d bytes:\n%s\n", read_bytes, buf);    

    memset(buf, 0, 64);
    read_bytes = sys_read(fd, buf, 6);
    printf("3 read %d bytes:\n%s\n", read_bytes, buf);  

    printf("--------close file and reopen---------\n");

    sys_close(fd);
    sys_open("/file4", O_RDWR);
    memset(buf, 0, 64);
    read_bytes = sys_read(fd, buf, 26);
    printf("4 read %d bytes:\n%s\n", read_bytes, buf);  

    printf("--------lseek file SEEK_SET---------\n");
    sys_lseek(fd, 0, SEEK_SET);
    memset(buf, 0, 64);
    read_bytes = sys_read(fd, buf, 26);
    printf("5 read %d bytes:\n%s\n", read_bytes, buf);     

    printf("%d closed now\n", fd);
    sys_close(fd);

    //printf("/file4 delete %s!\n", sys_unlink("/file4") == 0 ? "done" : "fail");
    printf("/dir1/subdir1 create %s!\n", sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");
    printf("/dir1 create %s!\n", sys_mkdir("/dir1") == 0 ? "done" : "fail");
    printf("/dir1/subdir1 create %s!\n", sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");
    sys_open("/dir1/subdir1/file1", O_CREAT);
    uint32_t fd1 = sys_open("/dir1/subdir1/file1", O_RDWR);
    if (fd1 > 0) {
        sys_write(fd1, "aiyouwo\n", 8);
        sys_lseek(fd1, 0, SEEK_SET);   
        char buf[64] = {0};
        sys_read(fd1, buf, 8);
        printf("/dir1/subdir1/file1 says:\n%s", buf);
        sys_close(fd1);
    }

    struct dir* p_dir = sys_opendir("/dir/subdir1");
    if (p_dir) {
        printf("/dir/subdir1 open done!\n");
        if (sys_closedir(p_dir) == 0) {
            printf("/dir/subdir1 close done!\n");
        } else {
            printf("/dir/subdir1 close fail!\n");
        }
    } else {
        printf("/dir/subdir1 open fail!\n");
    }

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