#include "fork.h"
#include "string.h"
#include "debug.h"
#include "thread.h"
#include "memory.h"
#include "fs.h"
#include "file.h"
#include "interrupt.h"
#include "process.h"

//switch to时弹内核栈用
extern void intr_exit(void);
extern struct file file_table[MAX_FILE_OPEN];
//将父进程的pcb所在页，包括内核栈拷贝给子进程
static int32_t copy_pcb_vaddrbitmap_stack0(struct task_struct* child_thread, struct task_struct* parent_thread)
{
    //不用去找pcb地址在哪，线程指针就是pcb的首地址
    memcpy(child_thread, parent_thread, PG_SIZE);

    //对子进程单独修改
    child_thread->pid = fork_pid();
    child_thread->elapsed_ticks = 0;
    child_thread->status = TASK_READY;
    child_thread->ticks = child_thread->priority;
    child_thread->parent_pid = parent_thread->pid;
    child_thread->general_tag.prev = child_thread->general_tag.next = NULL;
    child_thread->all_list_tag.prev = child_thread->all_list_tag.next = NULL;
    block_desc_init(child_thread->u_block_descs);

    //复制父进程虚拟地址池位图
    //虚拟地址池的总长度需要用多少个页的bitmap才能表示，从虚拟地址起始位置，直到内核地址起始，
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
    void* vaddr_btmp = get_kernel_pages(bitmap_pg_cnt);
    if (vaddr_btmp == NULL) {
        return -1;
    }
    //把父进程bitmap拷给子进程
    memcpy(vaddr_btmp, child_thread->userprog_vaddr.vaddr_bitmap.bits, bitmap_pg_cnt * PG_SIZE);
    //将子进程bitmap指向新分配的bitmap
    child_thread->userprog_vaddr.vaddr_bitmap.bits = vaddr_btmp;
    //为避免strcat越界，需要小于11
    ASSERT(strlen(child_thread->name) < 11);
    //按理说父子进程应同名，但为了调试，最后把子进程名字改为：父进程名_fork，以示区别
    //strcat(child_thread->name, "_fork");
    return 0;
}

//将父进程的代码段数据段用户栈段拷贝给子进程
static void copy_body_stack3(struct task_struct* child_thread, struct task_struct* parent_thread, void* buf_page)
{
    uint8_t* vaddr_btmp = parent_thread->userprog_vaddr.vaddr_bitmap.bits;
    uint32_t btmp_bytes_len = parent_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len;
    uint32_t vaddr_start = parent_thread->userprog_vaddr.vaddr_start;
    uint32_t idx_byte = 0;
    uint32_t idx_bit = 0;
    uint32_t prog_vaddr = 0;

    while (idx_byte < btmp_bytes_len) {
        //按字节遍历父bitmap，如果某个字节不为0，则将其中页进行拷贝
        if (vaddr_btmp[idx_byte]) {
            idx_bit = 0;
            while (idx_bit < 8) {
                if ((BITMAP_MASK << idx_bit) & vaddr_btmp[idx_byte]) {
                    //如果有一页是父目录占有的，则将其位置记录下来后拷贝
                    prog_vaddr = (idx_byte * 8 + idx_bit) * PG_SIZE + vaddr_start;
                    //拷贝到子进程就需要切换到子进程的页表，但是切过去后父进程就无法访问，所以要用大家都能访问的内核页做缓冲
                    memcpy(buf_page, (void*)prog_vaddr, PG_SIZE);
                    //PCB拷贝后，子进程的页表就指向了父进程的页表了吗？这不是同一个地址吗？
                    page_dir_activate(child_thread);
                    //因为拷贝bitmap时父进程已经置好位了，子进程往里填物理地址页就可以了
                    get_a_page_without_opvaddrbitmap(PF_USER, prog_vaddr);
                    memcpy((void*)prog_vaddr, buf_page, PG_SIZE);
                    page_dir_activate(parent_thread);
                }
                idx_bit++;
            }
        }
        idx_byte++;
    }
}

//构建子进程内核栈，从switch_to出来后，能接着父进程的位置继续运行
static int32_t build_child_stack(struct task_struct* child_thread)
{
    //获取子进程在内核栈中的中断栈
    struct intr_stack* intr_0_stack = (struct intr_stack*)((uint32_t)child_thread + PG_SIZE - sizeof(struct intr_stack));
    //修改中断栈中的eax=0，fork返回给子进程0
    intr_0_stack->eax = 0;

    //构建thread_stack，紧挨着intr_stack
    uint32_t* ret_addr_in_thread_stack = (uint32_t*)intr_0_stack - 1;
    //这几行不必要，只是为了说明结构
    uint32_t* esi_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 2;
    uint32_t* edi_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 3;
    uint32_t* ebx_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 4;
    uint32_t* ebp_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 5;

    //子进程从switch_to返回时让他跳到intr_exit将中断栈弹出，从而接着fork继续执行
    *ret_addr_in_thread_stack = (uint32_t)intr_exit;

    //也不必要，只是使栈更清晰
    *esi_ptr_in_thread_stack = *edi_ptr_in_thread_stack = *ebx_ptr_in_thread_stack = *ebp_ptr_in_thread_stack = 0;

    //让self_stack指向当前栈顶
    child_thread->self_kstack = ebp_ptr_in_thread_stack;
    return 0;
}

//更新inode打开数，将父进程打开的文件，都让子进程计数加1
static void update_inode_open_cnts(struct task_struct* thread)
{
    int32_t local_fd = 3, global_fd = 0;
    while (local_fd < MAX_FILES_OPEN_PER_PROC) {
        global_fd = thread->fd_table[local_fd];
        ASSERT(global_fd < MAX_FILE_OPEN);
        if (global_fd != -1) {
            file_table[global_fd].fd_inode->i_open_cnts++;
        }
        local_fd++;
    }
}

//开始拷贝父进程给子进程
static int32_t copy_process(struct task_struct* child_thread, struct task_struct* parent_thread)
{
    //分配内核页作为拷贝缓冲
    void* buf_page = get_kernel_pages(1);
    if (buf_page == NULL) {
        return -1;
    }

    //拷贝父进程PCB和内核栈
    if (copy_pcb_vaddrbitmap_stack0(child_thread, parent_thread) == -1) {
        return -1;
    }

    //为子进程创建页表
    child_thread->pgdir = create_page_dir();
    if (child_thread->pgdir == NULL) {
        return -1;
    }

    //创建完页表了，可以将父进程体拷贝给子进程了
    copy_body_stack3(child_thread, parent_thread, buf_page);

    //构建子进程栈
    build_child_stack(child_thread);

    //更新indoe打开次数
    update_inode_open_cnts(child_thread);

    mfree_page(PF_KERNEL, buf_page, 1);
    return 0;
}

pid_t sys_fork(void)
{
    struct task_struct* parent_thread = running_thread();
    //为子进程在内核空间中分配PCB
    struct task_struct* child_thread = get_kernel_pages(1);
    if (child_thread == NULL) {
        return -1;
    }
    ASSERT(INTR_OFF == intr_get_status() && parent_thread->pgdir != NULL);

    //开始拷贝
    if (copy_process(child_thread, parent_thread) == -1) {
        return -1;
    }

    //子进程刚创建，如果找到那肯定出错了
    ASSERT(!elem_find(&thread_ready_list, &child_thread->general_tag));
    list_append(&thread_ready_list, &child_thread->general_tag);
    ASSERT(!elem_find(&thread_all_list, &child_thread->all_list_tag));
    list_append(&thread_all_list, &child_thread->all_list_tag);

    return child_thread->pid;//给父进程返回子进程pid
}