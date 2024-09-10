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
#include "stdio_kernel.h"
#include "file.h"

#define PG_SIZE 4096

//pid位图 最大支持1024个pid
uint8_t pid_bitmap_bits[128] = {0};

//pid池
struct pid_pool {
    struct bitmap pid_bitmap;//pid位图
    uint32_t pid_start;//起始pid
    struct lock pid_lock;//pid锁
} pid_pool;

static struct list_elem* thread_tag;
struct task_struct* main_thread;
struct lock pid_lock;
struct task_struct* idle_thread;

extern void switch_to(struct task_struct* cur, struct task_struct* next);
extern void init(void);

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

//初始化pid池
static void pid_pool_init(void)
{
    pid_pool.pid_start = 1;
    pid_pool.pid_bitmap.bits = pid_bitmap_bits;
    pid_pool.pid_bitmap.btmp_bytes_len = 128;
    bitmap_init(&pid_pool.pid_bitmap);
    lock_init(&pid_pool.pid_lock);
}

//分配pid
static pid_t allocate_pid(void)
{
    lock_acquire(&pid_pool.pid_lock);
    int32_t bit_idx = bitmap_scan(&pid_pool.pid_bitmap, 1);
    bitmap_set(&pid_pool.pid_bitmap, bit_idx, 1);
    lock_release(&pid_pool.pid_lock);
    return (bit_idx + pid_pool.pid_start);
}

//释放pid
void release_pid(pid_t pid)
{
    lock_acquire(&pid_pool.pid_lock);
    int32_t bit_idx = pid - pid_pool.pid_start;
    bitmap_set(&pid_pool.pid_bitmap, bit_idx, 0);
    lock_release(&pid_pool.pid_lock);
}

pid_t fork_pid(void)
{
    return allocate_pid();
}

//在栈中预留线程需要的中断栈+线程栈空间，初始化线程栈
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg)
{
    pthread->self_kstack = (uint32_t*)((uint32_t)pthread->self_kstack - sizeof(struct intr_stack));
    pthread->self_kstack = (uint32_t*)((uint32_t)pthread->self_kstack - sizeof(struct thread_stack));
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

    //初始化fd table
    pthread->fd_table[0] = 0;
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;
    uint8_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN_PER_PROC) {
        pthread->fd_table[fd_idx] = -1;
        fd_idx++;
    }

    pthread->pgdir = NULL;
    pthread->cwd_inode_nr = 0;//以根目录为默认工作路径
    pthread->parent_pid = -1;//表示没有父进程
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
        asm volatile("sti\n\t hlt\n\t" : : : "memory");
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

//以填充空格的方式输出buf，将打印对齐
static void pad_print(char* buf, int32_t buf_len, void* ptr, char format)
{
    memset(buf, 0, buf_len);
    uint8_t out_pad_0idx = 0;
    switch (format) {
        case 's':
            out_pad_0idx = sprintf(buf, "%s", ptr);
            break;
        case 'd':
        //这个指针的格式转换有必要吗？有必要，这里要处理pid，是16位的整数，并非32为，要取栈中前2B打印
            out_pad_0idx = sprintf(buf, "%d", *((int16_t*)ptr));
            break;
        case 'x':
        //这个指针的格式转换有必要吗？有必要，这里要处理ticks，是32位的整数，要取前4B打印
            out_pad_0idx = sprintf(buf, "%x", *((uint32_t*)ptr));
            break;
    }

    //buf剩余部分填充空格
    while (out_pad_0idx < buf_len) {
        buf[out_pad_0idx] = ' ';
        out_pad_0idx++;
    }

    sys_write(stdout_no, buf, buf_len);
}

//遍历线程队列时，对线程进行处理，打印线程状态
static bool elem2thread_info(struct list_elem* pelem, int arg UNUSED)
{

    struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    
    char out_pad[16] = {0};
    //填充pid字符串
    pad_print(out_pad, 16, &pthread->pid, 'd');

    if (pthread->parent_pid == -1) {
        pad_print(out_pad, 16, "NULL", 's');
    } else {
        pad_print(out_pad, 16, &pthread->parent_pid, 'd');
    }

    switch (pthread->status) {
        case 0:
            pad_print(out_pad, 16, "RUNNING", 's');
            break;
        case 1:
            pad_print(out_pad, 16, "READY", 's');
            break;
        case 2:
            pad_print(out_pad, 16, "BLOCKED", 's');
            break;
        case 3:
            pad_print(out_pad, 16, "WAITING", 's');
            break;
        case 4:
            pad_print(out_pad, 16, "HANGING", 's');
            break;
        case 5:
            pad_print(out_pad, 16, "DIED", 's');
            break;
    }

    pad_print(out_pad, 16, &pthread->elapsed_ticks, 'x');

    memset(out_pad, 0, 16);
    ASSERT(strlen(pthread->name) < 17);
    memcpy(out_pad, pthread->name, strlen(pthread->name));
    strcat(out_pad, "\n");
    //因为pad_print填充后最后不会留结尾标志，没法用strcat，所以用write写出去
    sys_write(stdout_no, out_pad, strlen(out_pad));

    return false;//为了能继续遍历
}

void sys_ps(void)
{
    char *ps_title = "PID             PPID            STAT            TICKS           COMMAND\n";
    sys_write(stdout_no, ps_title, strlen(ps_title));
    list_traversal(&thread_all_list, elem2thread_info, 0);
}

//回收thread_over的pcb和页目录表，并将其从调度队列中去除
void thread_exit(struct task_struct* thread_over, bool need_schedule)
{
    //schedule要关中断
    intr_disable();
    thread_over->status = TASK_DIED;

    //如果thread_over不是当前线程，就有可能还在就绪队列中，将其从中删除
    if (elem_find(&thread_ready_list, &thread_over->general_tag)) {
        list_remove(&thread_over->general_tag);
    }

    //如果是用户进程，回收其页目录表，退出的线程有可能是主线程不需要释放
    //子进程的页表和进程体在exit中释放，只剩页目录表还未释放，因为子进程要用页目录表的768-1022项访问内核，去执行exit代码
    //进系统调用后cr3仍然是用户进程的页目录表，访问内核还是要用用户的
    if (thread_over->pgdir) {
        mfree_page(PF_KERNEL, thread_over->pgdir, 1);
    }

    //从all thread list中去掉任务，肯定在这里，不用再elem find
    list_remove(&thread_over->all_list_tag);

    //回收其pcb，主线程的pcb在栈中分配
    if (thread_over != main_thread) {
        mfree_page(PF_KERNEL, thread_over, 1);
    }

    //回收pid，前边thread_over PCB都释放了，再用pid，这里没问题？？
    release_pid(thread_over->pid);

    //如果不需要回到主调，则调度
    if (need_schedule) {
        schedule();
        PANIC("thread_exit: should not be here\n");
    }
}

//比对任务pid
static bool pid_check(struct list_elem* pelem, int32_t pid)
{
    struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->pid == pid) {
        return true;
    }
    return false;
}

//根据pid获取thread
struct task_struct* pid2thread(int32_t pid)
{
    struct list_elem* pelem = list_traversal(&thread_all_list, pid_check, pid);
    if (pelem == NULL) {
        return NULL;
    }
    struct task_struct* thread = elem2entry(struct task_struct, all_list_tag, pelem);
    return thread;
}

void thread_init(void)
{
    put_str("thread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    pid_pool_init();
    process_execute(init, "init");
    make_main_thread();
    idle_thread = thread_start("idle", 10, idle, NULL);
    put_str("thread_init done\n");
}