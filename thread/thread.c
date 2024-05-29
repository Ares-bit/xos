#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "interrupt.h"
#include "debug.h"
#include "print.h"
#include "process.h"
#include "sync.h"

#define PG_SIZE 4096

static struct list_elem* thread_tag;
struct task_struct* main_thread;
struct lock pid_lock;
struct task_struct* idle_thread;

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

static pid_t allocate_pid(void)
{
	static pid_t next_pid = 0;
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
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
    pthread->pid = allocate_pid();
    strcpy(pthread->name, name);
    if (pthread == main_thread) {
        pthread->status = TASK_RUNNING;//主线程的main一直运行
    } else {
        pthread->status = TASK_READY;
    }

    pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
    pthread->priority = prio;
    pthread->ticks = prio;
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;
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

static void idle(void* arg UNUSED)
{
    while(1) {
        thread_block(TASK_BLOCKED);
        //执行hlt时必须要保证目前处在开中断的情况下
        asm volatile("sti; hlt" : : : "memory");
    }
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
    //ASSERT(!list_empty(&thread_ready_list));

    //如果就绪队列中没有可运行的任务就唤醒idle
    if (list_empty(&thread_ready_list)) {
        thread_unblock(idle_thread);
    }

    thread_tag = NULL;
    thread_tag = list_pop(&thread_ready_list);
    //通过next的tag找到PCB起始地址
    struct task_struct* next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;

    //激活进程或线程
    process_activate(next);

    //保存cur 切换到next
    switch_to(cur, next);
}

void thread_block(enum task_status stat)
{
   	ASSERT(((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING)));
    enum intr_status old_status = intr_disable();
    struct task_struct* cur_thread = running_thread();
    cur_thread->status = stat;
    schedule();//将当前线程换下处理器
    //当线程解除阻塞，再次上处理器后，才能执行下边这句
    intr_set_status(old_status);
}

void thread_unblock(struct task_struct* pthread)
{
    enum intr_status old_status = intr_disable();
   	ASSERT(((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING)));
    if (pthread->status != TASK_READY) {
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
        if (elem_find(&thread_ready_list, &pthread->general_tag)) {
            PANIC("thread_unblock: blocked thread in ready_list\n");
        }
        //挂到队列最前边，尽快调度
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->status = TASK_READY;
    }
    intr_set_status(old_status);
}

//把当前线程挂到就绪队列 主动让出CPU 调度下一个线程上CPU
void thread_yield(void)
{
    struct task_struct* cur = running_thread();
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    list_append(&thread_ready_list, &cur->general_tag);
    cur->status = TASK_READY;
    schedule();
    intr_set_status(old_status);
}

void thread_init(void)
{
    put_str("thread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    lock_init(&pid_lock);
    make_main_thread();
    idle_thread = thread_start("idle", 10, idle, NULL);
    put_str("thread_init done\n");
}