#include "inode.h"
#include "fs.h"
#include "debug.h"
#include "string.h"
#include "list.h"
#include "thread.h"
#include "ide.h"
#include "interrupt.h"
#include "stdint.h"

struct inode_position {
    bool two_sec;//inode是否跨扇区
    uint32_t sec_lba;//inode所在扇区号
    uint32_t off_size;//inode在扇区中的偏移量
};

//获取inode所在扇区和偏移量
static void inode_locate(struct partition* part, uint32_t inode_no, struct inode_position* inode_pos)
{
    ASSERT(inode_no < MAX_FILES_PER_PART);

    uint32_t inode_table_lba = part->sb->inode_table_lba;

    uint32_t inode_size = sizeof(struct inode);

    //inode相对于inode table的偏移字节
    uint32_t off_size = inode_no * inode_size;

    //inode相对于inode table的偏移扇区
    uint32_t off_sec = off_size / SECTOR_SIZE;

    //inode相对于其所在扇区的内部偏移量
    uint32_t off_size_in_sec = off_size % SECTOR_SIZE;

    //检查inode是否跨扇区
    uint32_t left_in_sec = SECTOR_SIZE - off_size_in_sec;
    if (left_in_sec < inode_size) {
        inode_pos->two_sec = true;
    } else {
        inode_pos->two_sec = false;
    }
    //Lba地址以扇区为单位
    inode_pos->sec_lba = inode_table_lba + off_sec;
    inode_pos->off_size = off_size_in_sec;
}

//将inode写入part
void inode_sync(struct partition* part, struct inode* inode, void* io_buf)
{
    //io_buf用于硬盘io的缓冲区
    uint8_t inode_no = inode->i_no;
    struct inode_position inode_pos;
    //获取inode在硬盘中的lba地址
    inode_locate(part, inode_no, &inode_pos);
    ASSERT(inode_pos.sec_lba <= part->start_lba + part->sec_cnt);

    struct inode pure_inode;
    memcpy(&pure_inode, inode, sizeof(struct inode));

    //inode中三个属性只在内存中生效，在硬盘中无效，清除后写入硬盘，估计是怕读出来带东西用错了
    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny = false;
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

    char* inode_buf = (char *)io_buf;
    if (inode_pos.two_sec) {
        //如果跨了两个扇区，需要将原硬盘的数据读出来，再和新的数据拼成一个扇区后写入，硬盘的读写单位是扇区
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);

        memcpy(inode_buf + inode_pos.off_size, &pure_inode, sizeof(struct inode));

        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    } else {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);

        memcpy(inode_buf + inode_pos.off_size, &pure_inode, sizeof(struct inode));

        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}

struct inode* inode_open(struct partition* part, uint32_t inode_no)
{
    struct list_elem* elem = part->open_inodes.head.next;
    struct inode* inode_found;

    //先去已打开的inode队列上找，加快速度
    while (elem != &part->open_inodes.tail) {
        inode_found = elem2entry(struct inode, inode_tag, elem);
        if (inode_found->i_no == inode_no) {
            inode_found->i_open_cnts++;
            return inode_found;
        }
        elem = elem->next;
    }

    //如果没找到就取硬盘加载
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);

    //为使sys malloc创建的新inode被所有任务共享，需要将inode置于内核空间，故需要临时将当前进程的页表地址置为NULL
    struct task_struct* cur = running_thread();
    uint32_t* cur_pagedir_bak = cur->pgdir;
    cur->pgdir = NULL;

    //此时分配的inode位于内核空间
    inode_found = (struct inode*)sys_malloc(sizeof(struct inode));
    
    //还原页表地址
    cur->pgdir = cur_pagedir_bak;

    char* inode_buf;
    if (inode_pos.two_sec) {
        inode_buf = (char *)sys_malloc(SECTOR_SIZE * 2);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    } else {
        inode_buf = (char *)sys_malloc(SECTOR_SIZE);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);        
    }
    memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));

    //将读出的inode挂到分区的打开队列首，因为可能很快就要再次用到，所以挂到头部
    list_push(&part->open_inodes, &inode_found->inode_tag);
    inode_found->i_open_cnts = 1;//打开次数置为1

    sys_free(inode_buf);
    return inode_found;
}

//关闭inode或减少inode打开数
void inode_close(struct inode* inode)
{
    enum intr_status old_status = intr_disable();
    //如果此次关闭后引用计数为0，则释放inode占用内存，如果不为0，那就只是减1
    if(--inode->i_open_cnts == 0) {
        list_remove(&inode->inode_tag);

        //因为inode分配在内核空间，所以还要将当前页目录地址置空后再释放
        struct task_struct* cur = running_thread();
        uint32_t* cur_pagedir_bak = cur->pgdir;
        cur->pgdir = NULL;
        sys_free(inode);
        cur->pgdir = cur_pagedir_bak;
    }
    intr_set_status(old_status);
}

//初始化新inode
void inode_init(uint32_t inode_no, struct inode* new_inode)
{
    new_inode->i_no = inode_no;
    new_inode->i_size = 0;
    new_inode->i_open_cnts = 0;
    new_inode->write_deny = false;

    //初始化inode中的块索引数组
    uint8_t sec_idx = 0;
    while (sec_idx < 13) {
        new_inode->i_sectors[sec_idx] = 0;
        sec_idx++;
    }
}