#include "fork.h"
#include "string.h"
#include "debug.h"

//switch to时弹内核栈用
extern void intr_exit(void);

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
    //把父进程bitmap拷给子进程
    memcpy(vaddr_btmp, parent_thread->userprog_vaddr.vaddr_bitmap.bits, bitmap_pg_cnt * PG_SIZE);
    //将子进程bitmap指向新分配的bitmap
    child_thread->userprog_vaddr.vaddr_bitmap.bits = vaddr_btmp;
    //为避免strcat越界，需要小于11
    ASSERT(strlen(child_thread->name) < 11);
    //按理说父子进程应同名，但为了调试，最后把子进程名字改为：父进程名_fork，以示区别
    strcat(child_thread->name, "_fork");
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