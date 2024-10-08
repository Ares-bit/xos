#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"
#include "list.h"
#include "bitmap.h"
#include "memory.h"

#define TASK_NAME_LEN   16
#define MAX_FILES_OPEN_PER_PROC 8

typedef void thread_func(void*);
typedef int16_t pid_t;

struct list thread_ready_list;
struct list thread_all_list;

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
    uint32_t ds;
    uint32_t err_code;
    void (*eip) (void);
    uint32_t cs;
    uint32_t eflags;
    //特权级转移时才ss esp
    void* esp;
    uint32_t ss;
};

//线程自己的栈
struct thread_stack {
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;

	void (*eip) (thread_func* func, void* func_arg);

   	void (*unused_retaddr);
    thread_func* function;
    void* func_arg;
};

struct task_struct {
    uint32_t* self_kstack;//线程栈
    pid_t pid;
    enum task_status status;
    char name[TASK_NAME_LEN];
    uint8_t priority;
    uint8_t ticks;//每次在处理器上执行的时间滴答数
    uint32_t elapsed_ticks;//此任务已经占用了多少滴答
    int32_t fd_table[MAX_FILES_OPEN_PER_PROC];//文件描述符数组,int为了可以赋值-1
    struct list_elem general_tag;//一般队列
    struct list_elem all_list_tag;//线程队列
    uint32_t* pgdir;//进程页表虚拟地址
    struct virtual_addr userprog_vaddr;//用户进程虚拟地址空间
    struct mem_block_desc u_block_descs[DESC_CNT];//用户进程自己的内存块描述符
    uint32_t cwd_inode_nr;//进程所在的工作目录inode编号
    pid_t parent_pid;//父进程pid
    int8_t exit_status;//进程结束时自己调用exit传入的参数
    uint32_t stack_magic;//检测栈溢出
};

void thread_create(struct task_struct* pthread, thread_func function, void* func_arg);
void init_thread(struct task_struct* pthread, char* name, int prio);
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg);
struct task_struct* running_thread(void);
void schedule(void);
void thread_init(void);
void thread_block(enum task_status stat);
void thread_unblock(struct task_struct* pthread);
void thread_yield(void);
pid_t fork_pid(void);
void sys_ps(void);
struct task_struct* pid2thread(int32_t pid);
void thread_exit(struct task_struct* thread_over, bool need_schedule);
#endif