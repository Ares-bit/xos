#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "interrupt.h"
#include "debug.h"
#include "print.h"

#define PG_SIZE 4096

struct task_struct* main_thread;
struct list thread_ready_list;
struct list thread_all_list;
static struct list_elem* thread_tag;

extern void switch_to(struct task_struct* cur, struct task_struct* next);

//获取当前线程PCB起始地址
struct task_struct* running_thread()
{
    uint32_t esp;
    asm ("mov %%esp,%0": "=g"(esp));
    return (struct task_struct*)(esp & 0xfffff000);
};

static void kernel_thread(thread_func function, void* func_arg)
{
    //避免后面线程无法调度
    intr_enable();
    function(func_arg);
}

//在栈中预留线程需要的中断栈+线程栈空间，初始化线程栈
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg)
{
    pthread->self_kstack -= sizeof(struct intr_stack);
    pthread->self_kstack -= sizeof(struct thread_stack);
    struct thread_stack* kthread_stack = (struct thread_stack*)pthread->self_kstack;
    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = \
    kthread_stack->esi = kthread_stack->edi = 0;
}

//初始化线程PCB
void init_thread(struct task_struct* pthread, char* name, int prio)
{
    memset(pthread, 0, sizeof(*pthread));
    strcpy(pthread->name, name);
    if (pthread == main_thread) {
        pthread->status = TASK_RUNNING;//主线程的main一直运行
    } else {
        pthread->status = TASK_READY;
    }

    pthread->priority = prio;
    pthread->ticks = prio;
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;
    pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
    pthread->stack_magic = 0x20001212;
}

struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg)
{
    struct task_struct* thread = get_kernel_pages(1);
    //初始化线程PCB
    init_thread(thread, name, prio);
    //初始化线程栈
    thread_create(thread, function, func_arg);

    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));

    list_append(&thread_ready_list, &thread->general_tag);

    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));

    list_append(&thread_all_list, &thread->all_list_tag);
/*之前为了直接跳去执行线程写的
    asm volatile("movl %0,%%esp; pop %%ebp; pop %%ebx; pop %%edi; pop %%esi; ret" :\
                : "g"(thread->self_kstack) : "memory");
*/
    return thread;
}

static void make_main_thread(void)
{
    //main thread PCB地址为0xc009e000，已经提前预留，不需要再申请
    main_thread = running_thread();
    init_thread(main_thread, "main", 31);

    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}

void schedule(void)
{   
    //进调度后一定得是关中断状态
    ASSERT(intr_get_status() == INTR_OFF);

    struct task_struct* cur = running_thread();
    //如果当前进程是正在运行状态 则不允许其同时出现在就绪队列
    if (cur->status == TASK_RUNNING) {
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
        list_append(&thread_ready_list, &cur->general_tag);
        cur->ticks = cur->priority;//一个线程能被换下CPU 说明其ticks已减为0
        cur->status = TASK_READY;//看着就只牵扯到就绪队列 全部队列没用到啊
    } else {
        //如果当前线程不是RUNNING状态什么都不做 后面会置除READY和RUNNING外的状态吗
    }

    //如果就绪队列为空则报错
    ASSERT(!list_empty(&thread_ready_list));
    thread_tag = NULL;
    thread_tag = list_pop(&thread_ready_list);
    //通过next的tag找到PCB起始地址
    struct task_struct* next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;
    //保存cur 切换到next
    switch_to(cur, next);
}

void thread_init(void)
{
    put_str("thread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    make_main_thread();
    put_str("thread_init done\n");
}