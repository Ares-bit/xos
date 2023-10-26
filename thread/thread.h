#ifndef _THREAD_THREAD_H
#define _THREAD_THREAD_H
#include "stdint.h"

//typedef用于自定义数据类型
typedef void thread_func(void*);

//定义线程状态
enum task_status {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED
};

//中断时保护的上下文 与intr_exit是相反的顺序
struct intr_stack {
    uint32_t vec_no;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;//pushad会压入esp popad时会忽略掉
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t gs;
    uint32_t err_code;
    void (*eip) (void);
    uint32_t cs;
    uint32_t eflags;
    //特权级转移时才ss esp
    void* esp;
    uint32_t ss;
};