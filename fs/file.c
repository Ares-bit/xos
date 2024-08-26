#include "fs.h"
#include "file.h"
#include "stdio_kernel.h"
#include "thread.h"
#include "bitmap.h"
#include "dir.h"
#include "interrupt.h"
#include "debug.h"

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
            cur->fd_table[local_fd_idx] = global_fd_idx;
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
    uint32_t off_sec = bit_idx / (BLOCK_SIZE * 8);//查询当前bit idx位于第几块block
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

//打开inode_no的文件，成功返回描述符
int32_t file_open(uint32_t inode_no, uint8_t flag)
{
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) {
        printk("exceed max open files\n");
        return -1;
    }
    //将inode打开后放到全局文件表中
    file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no);
    //每次打开文件，要将pos置为0，文件指针指向文件头部
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    bool* write_deny = &file_table[fd_idx].fd_inode->write_deny;

    if (flag & O_WRONLY || flag & O_RDWR) {
        //如果文件可写，则必须考虑写文件的原子性
        enum intr_status old_status = intr_disable();
        if (!(*write_deny)) {
            //如果文件未被打开，则将打开标志置为真
            *write_deny = true;
            intr_set_status(old_status);
        } else {
            //如果文件已经被别人打开，则返回错误，不允许再写
            intr_set_status(old_status);
            printk("file can't be write now, try again later\n");
            return -1;
        }
    }
    return pcb_fd_install(fd_idx);
}

//关闭文件
int32_t file_close(struct file* file)
{
    if (file == NULL) {
        return -1;
    }

    file->fd_inode->write_deny = false;
    inode_close(file->fd_inode);
    file->fd_inode = NULL;//使file可以指向新文件
    return 0;
}

//把buf中count字节写入file 成功返回写入字节数，缺异常处理释放资源
//all block存储要写的块地址，本函数首先集中分配块，最后统一写
int32_t file_write(struct file* file, const void* buf, uint32_t count)
{
    //写入字节数超过文件最大限制
    if (file->fd_inode->i_size + count > (BLOCK_SIZE * (128 + 12))) {
        printk("exceed max file size 71680 bytes, write file failed!\n");
        return -1;
    }

    uint8_t* io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL) {
        printk("file_write: sys_malloc for io_buf failed!\n");
        return -1;
    }

    //文件所有块的LBA 512B存储间接块的内容，里边全是地址，剩余12 * 4存12个直接块地址
    uint32_t* all_blocks = (uint32_t*)sys_malloc(BLOCK_SIZE + 12 * 4);
    if (all_blocks == NULL) {
        printk("file_write: sys_malloc for all_blocks failed!\n");
        return -1;
    }

    //src指向输入buf
    const uint8_t* src = buf;
    uint32_t bytes_written = 0;//记录已写数据
    uint32_t size_left = count;//记录未写数据
    int32_t block_lba = -1;//块地址
    uint32_t block_bitmap_idx = 0;//记录文件块索引，用于同步新申请的block的bitmap

    uint32_t sec_idx;//索引扇区
    uint32_t sec_lba;//扇区地址
    uint32_t sec_off_bytes;//扇区内字节偏移
    uint32_t sec_left_bytes;//扇区剩余字节
    uint32_t chunk_size;//每次写入硬盘的数据块大小
    int32_t indirect_block_table;//获取一级间接表地址
    uint32_t block_idx;//文件实体块索引 12直接 1间接

    //如果文件第一次写，为其分配一个直接块
    if (file->fd_inode->i_sectors[0] == 0) {
        block_lba = block_bitmap_alloc(cur_part);
        if (block_lba == -1) {
            printk("file_write: block_bitmap_alloc failed!\n");
            return -1;
        }
        file->fd_inode->i_sectors[0] = block_lba;
        block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;//忘了idx还能这样算
        ASSERT(block_bitmap_idx != 0);
        //把bitmap修改同步到硬盘
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
    }

    //本次写之前，文件已经占了多少块
    uint32_t file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE + 1;

    //写入count字节后，文件占用的总块数
    uint32_t file_will_use_blocks = (file->fd_inode->i_size + count) / BLOCK_SIZE + 1;
    ASSERT(file_will_use_blocks <= 140);

    //判断是否需要继续分配扇区 为0表示文件最后一个可用块足够容纳count字节
    uint32_t add_blocks = file_will_use_blocks - file_has_used_blocks;
    //如果不需要新加块，就可原来的写，原来的要分直接块和间接块两种情况
    if (add_blocks == 0) {
        if (file_will_use_blocks <= 12) {
            block_idx = file_has_used_blocks - 1;//要被写的块idx
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        } else {
            //一级间接地址块必须存在
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            //把间接地址块读到all blocks 从12开始的后边，里边每32位存一个块的地址
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    } else {
        //新增直接块即可
        if (file_will_use_blocks <= 12) {
            //找到文件当前可用块
            block_idx = file_has_used_blocks - 1;
            ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
            //记录文件原有的有剩余空间的可用块
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

            //分配未来要用的扇区
            block_idx = file_has_used_blocks;
            while (block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 1 failed!\n");
                    return -1;
                }
                ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
                file->fd_inode->i_sectors[block_idx] = block_lba;
                //记录新分配的待使用块
                all_blocks[block_idx] = block_lba;
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                //同步block bitmap到硬盘
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                block_idx++;
            }
        } else if (file_has_used_blocks <= 12 && file_will_use_blocks > 12) {
            //直接块和间接块都要用

            //记录文件最后一个可用块
            block_idx = file_has_used_blocks - 1;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

            //创建一级间接块
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) {
                printk("file_write: block_bitmap_alloc for situation 2 failed!\n");
                return -1;
            }
            ASSERT(file->fd_inode->i_sectors[12] == 0);
            //记录间接地址表地址
            indirect_block_table = file->fd_inode->i_sectors[12] = block_lba;

            //从第一个新块开始，把所有要用的块都分配了
            block_idx = file_has_used_blocks;
            while (block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 2 failed!\n");
                    return -1;
                }
                //如果本次分配的是直接块可以直接赋值给i_sector
                if (block_idx < 12) {
                    ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
                    all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx] = block_lba;
                } else {
                    //如果本次分配间接块，还要写间接地址表，这里先存在all blocks里，后续一把同步到硬盘
                    all_blocks[block_idx] = block_lba;
                }
                block_bitmap_idx = block_lba =- cur_part->sb->data_start_lba;
                //同步刚分配的文件块到硬盘
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                block_idx++;
            }
            //同步间接地址表到硬盘，1表示写入1个扇区，间接地址表就是一个扇区大小
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        } else if (file_has_used_blocks > 12) {
            //只用间接块
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            //此时更新间接地址块要注意先读再写，不像之前新分配间接地址块可直接写，这个里边有之前的数据
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);

            block_idx = file_has_used_blocks;
            while (block_idx < file_will_use_blocks) {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1) {
                    printk("file_write: block_bitmap_alloc for situation 3 failed!\n");
                    return -1;
                }
                all_blocks[block_idx] = block_lba;
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                //同步新分配的间接块的block bitmap到硬盘
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                block_idx++;
            }
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }

    //要用块地址都已存在all blocks中，开始写数据
    bool first_write_block = true;//含有剩余空间的块标识
    file->fd_pos = file->fd_inode->i_size - 1;//置fd_pos，写数据时随时更新
    while (bytes_written < count) {
        memset(io_buf, 0, BLOCK_SIZE);//需要清零，因为如果最后一次写不满一块，那么剩余部分会残留上次写漫整块的数据
        sec_idx = file->fd_inode->i_size / BLOCK_SIZE;
        //文件最后一个可用块地址
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_inode->i_size % BLOCK_SIZE;
        //文件最后一个可用块里边的可用字节
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;//第一次往后都是BLOCK_SIZE大小了

        //判断此次写入硬盘的数据大小
        //一开始如果count < 文件最后一个扇区剩余字节，那就直接写count，如果大于，就写扇区剩余字节数
        //如果第一次大于，那么只有开始一次把文件剩余块补满，后续全都写BLOCK_SIZE大小
        //直到最后一个不满扇区的大小，就按它本身大小写进去
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
        if (first_write_block) {
            //读出文件写文件前的最后一个可用块，用后边剩余的部分存储，从此以后都是直接写一整块不需读取，因为是新块
            ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
            first_write_block = false;
        }
        memcpy(io_buf + sec_off_bytes, src, chunk_size);
        //写数据到硬盘
        ide_write(cur_part->my_disk, sec_lba, io_buf, 1);
        printk("file write at lba 0x%x\n", sec_lba);
        src += chunk_size;
        file->fd_inode->i_size += chunk_size;
        file->fd_pos += chunk_size;
        bytes_written += chunk_size;
        size_left -= chunk_size;
    }
    //同步文件inode
    inode_sync(cur_part, file->fd_inode, io_buf);
    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_written;
}

//从文件中读取count个字节，返回读出的字节数
int32_t file_read(struct file* file, void* buf, uint32_t count)
{
    uint8_t* buf_dst = (uint8_t*)buf;
    uint32_t size = count, size_left = size;

    //如果文件字节数小于Count，则把文件大小当做size，后边要支持pos定位读，所以不能直接比较count和i_size
    if ((file->fd_pos + count) > file->fd_inode->i_size) {
        size = file->fd_inode->i_size - file->fd_pos;//pos + count超了可以读，读的是文件从Pos开始到结尾的大小
        size_left = size;
        if (size == 0) {//如果pos本身超出文件实际就不该读了，应该是size <= 0
            return -1;
        }
    }

    uint8_t* io_buf = (uint8_t*)sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL) {
        printk("file_read: sys_malloc for io_buf failed!\n");
        return -1;
    }

    uint32_t* all_blocks = (uint32_t*)sys_malloc(BLOCK_SIZE + 12 * 4);
    if (all_blocks == NULL) {
        printk("file_read: sys_malloc for all_blocks failed!\n");
        return -1;        
    }

    //读取的起始块idx，结束块idx
    uint32_t block_read_start_idx = file->fd_pos / BLOCK_SIZE;
    uint32_t block_read_end_idx = (file->fd_pos + size) / BLOCK_SIZE;

    //要读取的块数-1
    uint32_t read_blocks = block_read_end_idx - block_read_start_idx;
    ASSERT(block_read_start_idx < 128 + 12 - 1 && block_read_end_idx < 128 + 12 - 1);
    
    int32_t indirect_block_table;
    uint32_t block_idx;

    //跟file write一样，先初始化all blocks
    if (read_blocks == 0) {
        //在起始块内读取即可
        ASSERT(block_read_start_idx == block_read_end_idx);
        if (block_read_end_idx < 12) {
            //只读一个直接块
            block_idx = block_read_end_idx;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];    
        } else {
            //只读一个间接块
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    } else {
        if (block_read_end_idx < 12) {
            block_idx = block_read_start_idx;
            while (block_idx <= block_read_end_idx) {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
        } else if (block_read_start_idx < 12 && block_read_end_idx >= 12) {
            block_idx = block_read_start_idx;
            while (block_idx < 12) {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        } else {
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }

    uint32_t sec_idx, sec_lba, sec_off_bytes, sec_left_bytes, chunk_size;
    uint32_t bytes_read = 0;
    while (bytes_read < size) {
        sec_idx = file->fd_pos / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_pos % BLOCK_SIZE;//pos所在扇区中的偏移量
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;//一开始值为第一个扇区中要读的不满整个扇区的部分

        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
        
        memset(io_buf, 0, BLOCK_SIZE);
        ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
        //如果只需要读一个块的情况，这种会拷贝超了，比如只读一个块的一个字节，这个会直接把剩余扇区都拷贝过去
        //不不不不不，不会拷贝超，你看512行，如果只读一个字节，那么size left会小于扇区中剩余字节，会使用size_left作为拷贝值
        memcpy(buf_dst, io_buf + sec_off_bytes, chunk_size);

        buf_dst += chunk_size;
        file->fd_pos += chunk_size;
        bytes_read += chunk_size;
        size_left -= chunk_size;
    }
    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_read;
}