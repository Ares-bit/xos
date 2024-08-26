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

    sys_mkdir("/dir1");
    sys_mkdir("/dir1/subdir1");
    uint32_t fd1 = sys_open("/dir1/subdir1/file1", O_CREAT);//创建的文件会直接加到file table中
    sys_close(fd1);
    //printf("/file4 delete %s!\n", sys_unlink("/file4") == 0 ? "done" : "fail");
    // printf("/dir1/subdir1 create %s!\n", sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");
    // printf("/dir1 create %s!\n", sys_mkdir("/dir1") == 0 ? "done" : "fail");
    // printf("/dir1/subdir1 create %s!\n", sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");
    // sys_open("/dir1/subdir1/file1", O_CREAT);
    // uint32_t fd1 = sys_open("/dir1/subdir1/file1", O_RDWR);
    // if (fd1 > 0) {
    //     sys_write(fd1, "aiyouwo\n", 8);
    //     sys_lseek(fd1, 0, SEEK_SET);   
    //     char buf[64] = {0};
    //     sys_read(fd1, buf, 8);
    //     printf("/dir1/subdir1/file1 says:\n%s", buf);
    //     sys_close(fd1);
    // }

    struct dir* p_dir = NULL, *p_dir1 = NULL;
    p_dir = sys_opendir("/");
    if (p_dir) {
        printf("/ open done!\n");
        char* type = NULL;
        struct dir_entry* dir_e = NULL;
        while (dir_e = sys_readdir(p_dir)) {
            if (dir_e->f_type == FT_REGULAR) {
                type = "regular";
            } else {
                type = "directory";
            }
            printf("    %s  %s\n", type, dir_e->filename);
        }
        sys_rewinddir(p_dir);
        if (sys_closedir(p_dir) == 0) {
            printf("/ close done!\n");
        } else {
            printf("/ close fail!\n");
        }
    } else {
        printf("/ open fail!\n");
    }
    
    p_dir = sys_opendir("/dir1");
    if (p_dir) {
        printf("/dir1 open done!\n");
        char* type = NULL;
        struct dir_entry* dir_e = NULL;
        while (dir_e = sys_readdir(p_dir)) {
            if (dir_e->f_type == FT_REGULAR) {
                type = "regular";
            } else {
                type = "directory";
            }
            printf("    %s  %s\n", type, dir_e->filename);
        }
        sys_rewinddir(p_dir);
        if (sys_closedir(p_dir) == 0) {
            printf("/dir1 close done!\n");
        } else {
            printf("/dir1 close fail!\n");
        }
    } else {
        printf("/dir1 open fail!\n");
    }

    p_dir1 = sys_opendir("/dir1/subdir1");
    if (p_dir1) {
        printf("/dir1/subdir1 open done!\n");
        char* type = NULL;
        struct dir_entry* dir_e = NULL;
        while (dir_e = sys_readdir(p_dir1)) {
            if (dir_e->f_type == FT_REGULAR) {
                type = "regular";
            } else {
                type = "directory";
            }
            printf("    %s  %s\n", type, dir_e->filename);
        }
        sys_rewinddir(p_dir1);
        if (sys_closedir(p_dir1) == 0) {
            printf("/dir1/subdir1 close done!\n");
        } else {
            printf("/dir1/subdir1 close fail!\n");
        }
    } else {
        printf("/dir1/subdir1 open fail!\n");
    }
#if 0
    printf("delete non empty /dir1/subdir1\n");
    if (sys_rmdir("/dir1/subdir1") == -1) {
        printf("sys_rmdir: /dir1/subdir1 delete fail!\n");
    }

    printf("delete /dir1/subdir1/file1\n");
    if (sys_rmdir("/dir1/subdir1/file1") == -1) {
        printf("sys_rmdir: /dir1/subdir1/file1 delete fail!\n");
    }

    printf("delete /dir1/subdir1/file1\n");
    if (sys_unlink("/dir1/subdir1/file1") == 0) {
        printf("sys_unlink: /dir1/subdir1/file1 delete done!\n");
    }

    p_dir = sys_opendir("/dir1/subdir1");
    if (p_dir) {
        printf("/dir1/subdir1 open done!\n");
        char* type = NULL;
        struct dir_entry* dir_e = NULL;
        while (dir_e = sys_readdir(p_dir)) {
            if (dir_e->f_type == FT_REGULAR) {
                type = "regular";
            } else {
                type = "directory";
            }
            printf("    %s  %s\n", type, dir_e->filename);
        }
        sys_rewinddir(p_dir);
        if (sys_closedir(p_dir) == 0) {
            printf("/dir1/subdir1 close done!\n");
        } else {
            printf("/dir1/subdir1 close fail!\n");
        }
    } else {
        printf("/dir1/subdir1 open fail!\n");
    }
#endif

#if 0
    printf("---------delete /dir1/subdir1\n");
    if (sys_rmdir("/dir1/subdir1") == 0) {
        printf("sys_rmdir: /dir1/subdir1 delete done!\n");
    }

    // printf("delete /file1\n");
    // if (sys_unlink("/file1") == 0) {
    //     printf("sys_unlink: /file1 delete done!\n");
    // }

    p_dir = sys_opendir("/dir1");
    if (p_dir) {
        printf("/dir1 open done!\n");
        char* type = NULL;
        struct dir_entry* dir_e = NULL;
        while (dir_e = sys_readdir(p_dir)) {
            if (dir_e->f_type == FT_REGULAR) {
                type = "regular";
            } else {
                type = "directory";
            }
            printf("    %s  %s\n", type, dir_e->filename);
        }
        sys_rewinddir(p_dir);
        if (sys_closedir(p_dir) == 0) {
            printf("/dir1 close done!\n");
        } else {
            printf("/dir1 close fail!\n");
        }
    } else {
        printf("/dir1 open fail!\n");
    }
#endif
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