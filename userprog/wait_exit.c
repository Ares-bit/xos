#include "wait_exit.h"
#include "thread.h"
#include "debug.h"

//释放用户空间虚拟页 物理页 页表 页目录表项 关闭文件
static void release_prog_resource(struct task_struct* release_thread)
{
    uint32_t* pgdir_vaddr = release_thread->pgdir;//用户进程页目录表虚拟地址
    uint16_t user_pde_nr = 768, pde_idx = 0;//用户空间为页目录表项0-767
    uint32_t pde = 0;//页目录表项的值
    uint32_t* v_pde_ptr = NULL;//页目录表项地址，与pde_ptr函数名区分开

    uint16_t user_pte_nr = 1024, pte_idx = 0;//每个页表1024项
    uint32_t pte = 0;//页表项的值
    uint32_t* v_pte_ptr = NULL;//页表项地址，与pte_ptr函数名区分开

    uint32_t* first_pte_vaddr_in_pde = NULL;//pde中第一个pte指向的物理页虚拟地址
    uint32_t pg_phy_addr = 0;

    //遍历页目录表所有表项
    while (pde_idx < user_pde_nr) {
        v_pde_ptr = pgdir_vaddr + pde_idx;//指针相加，每次移动4B，移动到下个表项
        pde = *v_pde_ptr;//取出页目录表项的值
        if (pde & 0x00000001) {
            //一个页表可以表示4MB空间，pde_idx指向的页表所表示的虚拟空间首地址为pde_idx * 4M，再用此首地址得到页表0项的指针
            first_pte_vaddr_in_pde = pte_ptr(pde_idx * 400000);
            pte_idx = 0;
            //遍历页表所有表项
            while (pte_idx < user_pte_nr) {
                v_pte_ptr = first_pte_vaddr_in_pde + pte_idx;
                pte = *v_pte_ptr;
                //如果页表项存在，则释放其指向的物理地址页，不改变页表项，也没有将页表项P位置0，难道后边还用？为了避免频繁改动页表刻意为之
                if (pte & 0x00000001) {
                    pg_phy_addr = pte & 0xfffff000;
                    free_a_phy_page(pg_phy_addr);
                }
                pte_idx++;
            }
            //释放完页表中所有物理页后，释放页表本身所占物理页，不动页表，也没有将页目录表项P位置0，难道后边还用？为了避免频繁改动页表刻意为之
            pg_phy_addr = pde & 0xfffff000;
            free_a_phy_page(pg_phy_addr);
        }
        pde_idx++;
    }

    //不释放页目录表为了继续访问内核空间

    //回收用户虚拟地址池占用的物理页面
    uint32_t bitmap_pg_cnt = release_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len / PG_SIZE; //这个分配的时候就可以保证是整数了，不需再向上取整
    uint8_t user_vaddr_pool_bitmap = release_thread->userprog_vaddr.vaddr_bitmap.bits;
    //连物理地址带页表一起释放
    mfree_page(PF_KERNEL, user_vaddr_pool_bitmap, bitmap_pg_cnt);

    //关闭进程打开的文件
    uint8_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN_PER_PROC) {
        if (release_thread->fd_table[fd_idx] != -1) {
            sys_close(fd_idx);
        }
        fd_idx++;
    }
}

//查找进程父进程pid是否为ppid
static bool find_child(struct list_elem* pelem, int32_t ppid)
{
    struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == ppid) {
        //list_traversal遇见true就返回了，所以这是要找第一个ppid的子进程
        return true;
    }
    return false;
}

//查找进程父进程pid为ppid且状态为hanging的子进程
static bool find_hanging_child(struct list_elem* pelem, int32_t ppid)
{
    struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == ppid && pthread->status == TASK_HANGING) {
        return true;
    }
    return false;
}

//让init收养子进程
static bool init_adopt_a_child(struct list_elem* pelem, int32_t pid)
{
    struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == pid) {
        pthread->parent_pid = 1;
        //不要返回true，要让traversal遍历所有子进程 改其父进程为init
    }
    return false;
}

//等待子进程调用exit，将其状态存到status，成功返回子进程pid，失败返回-1
pid_t sys_wait(int32_t* status)
{
    //当前调用wait的进程是父进程
    struct task_struct* parent_thread = running_thread();

    while (1) {
        //优先处理挂起状态的子进程
        struct list_elem* child_elem = list_traversal(&thread_all_list, find_hanging_child, parent_thread->pid);
        if (child_elem != NULL) {
            //获取子进程PCB
            struct task_struct* child_thread = elem2entry(struct task_struct, all_list_tag, child_elem);
            //获取子进程退出状态
            *status = child_thread->exit_status;
            //子进程exit后会回收pid，所以wait时就要保存下来
            uint16_t child_pid = child_thread->pid;
            //释放子进程PCB 页目录表 从队列中去除，false不调度，调用thread_exit后回来走到下边return
            thread_exit(child_thread, false);
            return child_pid;
        }

        child_elem = list_traversal(&thread_all_list, find_child, parent_thread->pid);
        if (child_elem == NULL) {
            return -1;//如果find child没找到，就说明没有子进程，返回-1
        } else {
            //如果走到这里说明找到了子进程，但是不是hanging状态，则阻塞父进程
            thread_block(TASK_WAITING);
        }
    }
}

//子进程结束自己
void sys_exit(int32_t status)
{
    struct task_struct* child_thread = running_thread();
    child_thread->exit_status = status;
    if (child_thread->parent_pid == -1) {
        PANIC("sys_exit: child_thread->parent_pid is -1\n");
    }

    //将当前将要结束进程的所有子进程过继给init
    list_traversal(&thread_all_list, init_adopt_a_child, child_thread->pid);

    //回收其资源
    release_prog_resource(child_thread);

    //如果父进程正在等待则唤醒
    struct task_struct* parent_thread = pid2thread(child_thread->parent_pid);
    if (parent_thread->status == TASK_WAITING) {
        thread_unblock(parent_thread);
    }

    //唤醒父进程后，将自己置为hanging，等父进程被调度到后就可以将自己彻底消除
    thread_block(TASK_HANGING);
}