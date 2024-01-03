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

void k_thread_a(void*);
void k_thread_b(void*);

int main(void) {
    put_str("I am kernel\n");
    init_all();
    //两个消费者线程
    thread_start("consumer_a", 31, k_thread_a, " A_");
    thread_start("consumer_b", 31, k_thread_b, " B_");
    //打开时钟中断
    intr_enable();
    while (1);// {
        //console_put_str("Main ");
    //}

    return 0;
}

void k_thread_a(void* arg) {
    char* para = arg;
    enum intr_status old_status;
    while(1) {
        old_status = intr_disable();
        if(!ioq_empty(&kbd_buf)) {
            console_put_str(para);
            char byte = ioq_getchar(&kbd_buf);
            console_put_char(byte);
        }
        intr_set_status(old_status);//不能放外边，否则把中断关死了，再也无法响应中断了
    }
}

void k_thread_b(void* arg) {
    char* para = arg;
    enum intr_status old_status;
    while(1) {
        old_status = intr_disable();
        if(!ioq_empty(&kbd_buf)) {
            console_put_str(para);
            char byte = ioq_getchar(&kbd_buf);
            console_put_char(byte);
        }
        intr_set_status(old_status);
    }
}