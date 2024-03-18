#include "thread.h"
#include "global.h"
#include "memory.h"
#include "stdint.h"
#include "tss.h"

extern void intr_exit(void);

//填充intr_stack，并执行intr_exit构造用户进程上下文
void start_process(void* filename_)
{
    void* function = filename_;
    struct task_struct* cur = running_thread();
    cur->self_kstack += sizeof(struct thread_stack);
    struct intr_stack* proc_stack = (struct intr_stack*)cur->self_kstack;

    proc_stack->edi = proc_stack->esi = \
    proc_stack->ebp = proc_stack->esp_dummy = 0;

    proc_stack->ebx = proc_stack->edx = \
    proc_stack->ecx = proc_stack->eax = 0;

    proc_stack->gs = 0;
    proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
    proc_stack->eip = function;//为什么内核线程带参数？这个不需要吗？
    proc_stack->cs = SELECTOR_U_CODE;
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
    proc_stack->esp = (void*)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);
    proc_stack->ss = SELECTOR_U_DATA;

    asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(proc_stack) : memory);
}

//激活线程或进程页表
void page_dir_activate(struct task_struct* p_thread)
{
    uint32_t pagedir_phy_addr = 0x100000;

    if (p_thread->pgdit != NULL) {
        pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
    }

    asm volatile("movl %0, %%cr3" : : "r"(pagedir_phy_addr) : "memory");
}

//激活线程或进程即换页表+0特权级栈
void process_activate(struct task_struct* p_thread)
{
    ASSERT(p_thread != NULL);
    
    page_dir_activate(p_thread);

    //更新用户进程tss中esp0
    if (p_thread->pgdir) {
        update_tss_esp(p_thread);
    }
}