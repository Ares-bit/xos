#include "fs.h"
#include "file.h"
#include "stdio_kernel.h"
#include "thread.h"
#include "bitmap.h"
#include "dir.h"

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
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
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
    bitmap_set(&part->block_bitmap, bit_idx, 1);
    return part->sb->data_start_lba + bit_idx;//第一个数据块地址+偏移地址
}

//将bitmap第bit idx位所在的512字节同步到硬盘
void bitmap_sync(struct partition* part, uint32_t bit_idx, enum bitmap_type btmp_type)
{
    uint32_t off_sec = bit_idx / (SECTOR_SIZE * 8);//查询当前bit idx位于第几块block
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

//创建文件，成功则返回文件描述符，失败返回-1
int32_t file_create(struct dir* parent_dir, char* filename, enum oflags flag)
{
    //先创建一个缓冲区供后续使用
    //这有啥需要缓冲区的地方呢？？哦，读出来的目录块要用，要往里写目录项
    //同步硬盘都需要先读出扇区数据，修改一部分后再整体写回去
    void *io_buf = sys_malloc(1024);
    if (io_buf == NULL) {
        printk("in file_create: sys_malloc for io_buf failed\n");
        return -1;
    }

    //用于操作失败时回滚各资源状态
    uint8_t rollback_step = 0;

    //为文件分配inode
    int32_t inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("in file_create: alloc inode_no failed\n");
        goto rollback;//为0，只释放刚io_buf
    }

    //为新文件申请内存中的inode，要填写后写入硬盘
    struct inode* new_file_inode = (struct inode*)sys_malloc(sizeof(struct inode));
    if (new_file_inode == NULL) {
        printk("in file_create: sys_malloc struct inode failed\n");
        rollback_step = 1;
        goto rollback;
    }

    //初始化i节点
    inode_init(inode_no, new_file_inode);

    //获取全局文件表下标
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) {
        printk("exceed max open files\n");
        rollback_step = 2;
        goto rollback;
    }

    file_table[fd_idx].fd_inode = new_file_inode;
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    //file_table[fd_idx].fd_inode->write_deny = false; //inode_init中已经置过了

    //为新文件创建目录项
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));

    create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);

    //同步数据到硬盘，话说什么时候填写inode里的数据啊。。。
    //同步目录项到当前目录文件
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        printk("sync dir_entry to disk failed\n");
        rollback_step = 3;
        goto rollback;
    }

    //将当前目录的inode同步回硬盘，因为新建文件，大小有变
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, parent_dir->inode, io_buf);

    //将新文件的inode同步到硬盘，但是这个文件inode都没赋值呢啊。。。。
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, new_file_inode, io_buf);

    //将inode bitmap同步回硬盘bitmap应占据完整的扇区，所以不怕只占一部分扇区，不需要io_buf
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    //将新文件inode挂到open_inode队列上，并将打开次数置1
    list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
    new_file_inode->i_open_cnts = 1;

    sys_free(io_buf);
    //返回新文件在当前进程pcb中的的fd
    return pcb_fd_install(fd_idx);

rollback:
    switch (rollback_step) {
        case 3:
            memset(&file_table[fd_idx], 0, sizeof(struct file));
        case 2:
            sys_free(new_file_inode);
        case 1:
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
            break;
    }
    sys_free(io_buf);
    return -1;
}