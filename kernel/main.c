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
#include "syscall-init.h"
#include "stdio.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);
int prog_a_pid = 0, prog_b_pid = 0;

int main(void) {
    put_str("I am kernel\n");
    init_all();
    //两个消费者线程
    //thread_start("consumer_a", 31, k_thread_a, "argA");
    //thread_start("consumer_b", 31, k_thread_b, "argB");
    process_execute(u_prog_a, "user_prog_a");
    process_execute(u_prog_b, "user_prog_b");
    //打开时钟中断
	intr_enable();
    console_put_str("main_pid:0x");
    console_put_int(sys_getpid());
    console_put_char('\n');
    thread_start("consumer_a", 31, k_thread_a, "argA");
    thread_start("consumer_b", 31, k_thread_b, "argB");   

    while (1);// {
        //console_put_str("Main ");
    //}

    return 0;
}

void k_thread_a(void* arg) {
    char* para = arg;
    console_put_str("thread_a_pid:0x");
    console_put_int(sys_getpid());
    console_put_char('\n');
    // console_put_str("prog_a_pid:0x");
    // console_put_int(prog_a_pid);
    // console_put_char('\n');  
    while(1) {
        //console_put_str(" v_a:0x");
        //console_put_int(test_var_a);
    }
}

void k_thread_b(void* arg) {
    char* para = arg;
    console_put_str("thread_b_pid:0x");
    console_put_int(sys_getpid());
    console_put_char('\n');
    // console_put_str("prog_b_pid:0x");
    // console_put_int(prog_b_pid);
    // console_put_char('\n'); 
    while(1) {
        //console_put_str(" v_b:0x");
        //console_put_int(test_var_b);
    }
}

void u_prog_a(void) {
    //prog_a_pid = getpid();
    printf(" prog_a_pid:0x%x\n", getpid());
    while(1) {
        //test_var_a++;
    }
}

void u_prog_b(void) {
    //prog_b_pid = getpid();
    printf(" prog_b_pid:0x%x\n", getpid());
    while(1) {
        //test_var_b++;
    }
}