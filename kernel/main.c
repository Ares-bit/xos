#include "print.h"
#include "init.h"
#include "debug.h"
#include "stdint.h"
#include "memory.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"

void k_thread_a(void*);
void k_thread_b(void*);

int main(void) {
   put_str("I am kernel\n");
   init_all();

   thread_start("k_thread_a", 31, k_thread_a, "argA ");
   thread_start("k_thread_b", 8, k_thread_b, "argB ");
   //thread_start("k_thread_c", 16, k_thread_c, "argC ");
   //thread_start("k_thread_d", 8, k_thread_d, "argD ");

    //打开时钟中断
    intr_enable();
    while (1) {
        //console_put_str("Main ");
        intr_disable();
        put_str("Main ");
        intr_enable();
    }

    return 0;
}

void k_thread_a(void* arg) {
    char* para = arg;
    while(1) {
        //console_put_str(para);
        intr_disable();
        put_str(para);
        intr_enable();
    }
}

void k_thread_b(void* arg) {
    char* para = arg;
    while(1) {
        intr_disable();
        put_str(para);
        intr_enable();
    }
}