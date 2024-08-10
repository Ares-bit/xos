#include "file.h"
#include "stdio-kernel.h"

//全局打开的文件表，全局就一个文件表
struct file file_table[MAX_FILE_OPEN];

//从文件表中获取一个空缺位置
int32_t get_free_slot_in_global(void)
{
    uint32_t fd_idx = 3;//跨过三个标准描述符
    while (fd_idx < MAX_FILE_OPEN) {
        if (file_table[fd_idx].fd_inode == NULL) {
            break;
        }
        fd_idx++;
    }
    if (fd_idx == MAX_FILE_OPEN) {
        printk("exceed max open files\n");
        return -1;//所以要返回int型
    }
    return fd_idx;
}

//将全局描述符下标安装到PCB的fd table中
int32_t pcb_fd_install(int32_t global_fd_idx)
{
    struct task_struct* cur = running_thread();
    uint8_t local_fd_idx = 3;//跨过三个标准描述符
    while (local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
        //进程可打开的最多文件个数8
        if (cur->fd_table[local_fd_idx] == -1) {
            cur->fd_table[local_fd_idx] == global_fd_idx;
            break;
        }
        local_fd_idx++;
    }
    if (local_fd_idx == MAX_FILES_OPEN_PER_PROC) {
        printk("exceed max open files_per_proc\n");
        return -1;
    }
    return local_fd_idx;
}

//在分区中占用一个ionde：分配一个i结点，返回i结点号
int32_t inode_bitmap_alloc(struct partition* part)
{
    int32_t bit_idx == bitmap_scan(&part->inode_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->inode_bitmap, bit_idx, 1);
    return bit_idx;
}

//分配一个扇区，返回扇区地址
int32_t block_bitmap_alloc(struct partition* part)
{
    int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->block_bitmap, 1);
    return part->sb->data_start_lba + bit_idx;//第一个数据块地址+偏移地址
}

//将bitmap第bit idx位所在的512字节同步到硬盘
void bitmap_sync(struct partition* part, uint32_t bit_idx, enum bitmap_type btmp_type)
{
    uint32_t off_sec = bit_idx / (512 * 8);//查询当前bit idx位于第几块block
    uint32_t off_size = off_sec * BLOCK_SIZE;
    uint32_t sec_lba;
    uint8_t* bitmap_off;
    //更新bitmap到硬盘
    switch (btmp_type) {
        case INODE_BITMAP:
            sec_lba = part->sb->inode_bitmap_lba + off_sec;
            bitmap_off = part->inode_bitmap.bits + off_size;
            break;
        case BLOCK_BITMAP:
            sec_lba = part->sb->block_bitmap_lba + off_sec;
            bitmap_off = part->block_bitmap.bits + off_size;
            break;        
    }
    ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}

