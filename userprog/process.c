#include "thread.h"
#include "global.h"
#include "memory.h"
#include "stdint.h"
#include "tss.h"
#include "console.h"
#include "string.h"
#include "bitmap.h"
#include "process.h"

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

//创建用户进程页目录表，并将最后一项赋值为其物理地址，方便以后修改
uint32_t* create_page_dir(void)
{
    uint32_t* page_dir_vaddr = get_kernel_pages(1);

    if (page_dir_vaddr == NULL) {
        console_put_str("create_page_dir: get_kernel_pages failed!");
        return NULL;
    }

    //此时仍是内核再运行，故0xfffff000虚拟地址会找到内核页目录表的物理地址
    memcpy((uint32_t*)((uint32_t)page_dir_vaddr + 0x300 * 4), \
        (uint32_t*)(0xfffff000 + 0x300 * 4), (0x400 - 0x300) * 4);

    uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
    page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;

    return page_dir_vaddr;
}

//创建用户进程虚拟地址位图
void create_user_vaddr_bitmap(struc task_struct* user_prog)
{
    user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
    user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
    bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

//创建并初始化用户进程
void process_execute(void* filename, char* name)
{
    struct task_struct* thread = get_kernel_pages(1);
    init_thread(thread, name, DEFAULT_PRIO);
    create_user_vaddr_bitmap(thread);
    thread_create(thread, start_process, filename);
    thread->pgdir = create_page_dir();

    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list,  &thread->general_tag);

    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);
    intr_set_status(old_status);
}