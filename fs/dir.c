#include "dir.h"
#include "inode.h"
#include "memory.h"
#include "string.h"
#include "file.h"
#include "debug.h"
#include "bitmap.h"

//打开根目录
void open_root_dir(struct partition* part)
{
    root_dir.inode = inode_open(part, part->sb->root_inode_no);
    root_dir.dir_pos = 0;
}

//打开目录并返回目录指针
struct dir* dir_open(struct partition* part, uint32_t inode_no)
{   
    //struct dir不再硬盘中，是个内存中的管理结构
    struct dir* pdir = (struct dir*)sys_malloc(sizeof(struct dir));
    pdir->inode = inode_open(part, inode_no);
    pdir->dir_pos = 0;
    return pdir;
}

//从目录中找到name对应的文件或目录
bool search_dir_entry(struct partition* part, struct dir* pdir, const char* name, struct dir_entry* dir_e)
{
    uint32_t block_cnt = 12 + 128;//12个直接块+128个间接块

    uint32_t* all_blocks = (uint32_t*)sys_malloc(block_cnt * 4);//每个文件块要用4B存地址码？？
    if (all_blocks == NULL) {
        printk("search_dir_entry: sys_malloc for all_blocks failed\n");
        return false;
    }

    uint32_t block_idx = 0;
    while (block_idx < 12) {
        //果然申请的内存是存储每个目录条目的前12个直接块的地址用的
        all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
        block_idx++;
    }
    block_idx = 0;

    //如果有间接块，就将其指向位置读入all blocks，此处装了128个间接块的地址，至此所有块的Lba都被得到了
    if (pdir->inode->i_sectors[12] != 0) {
        ide_read(part->my_disk, pdir->inode->i_sectors[12], all_blocks + 12, 1);
    }

    //写目录项时已保证目录项不跨扇区，读的时候直接读一个扇区即可（谁保证的？我咋不记得了
    uint8_t* buf = (uint8_t*)sys_malloc(SECTOR_SIZE);
    struct dir_entry* p_de = (struct dir_entry*)buf;
    uint32_t dir_entry_size = part->sb->dir_entry_size;
    //一个扇区可容纳的目录项个数
    uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size;

    //在所有块中查找目录项
    while (block_idx < block_cnt) {
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        ide_read(part->my_disk, all_blocks[block_idx], buf, 1);

        uint32_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entry_cnt) {
            //如果找到直接复制整个目录项给外边传进来接受的目录项，但是如果重名咋办
            if (!strcmp(p_de->filename, name)) {
                memcpy(dir_e, p_de, dir_entry_size);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
            dir_entry_idx++;
            p_de++;
        }
        block_idx++;
        //走到这里表明已经查完了一个文件实体块，但是未找到，需要清零并让Pde指向开头，重新查找下个块
        p_de = (struct dir_entry*)buf;
        memset(buf, 0, SECTOR_SIZE);
    }
    sys_free(buf);
    sys_free(all_blocks);
    return false;
}

//关闭目录
void dir_close(struct dir* dir)
{
    //根目录打开后就不应关闭，且无法关闭，在栈上分配的内存释放不了
    if (dir == &root_dir) {
        return;
    }

    inode_close(dir->inode);
    sys_free(dir);
}

//在内存中初始化目录项
void create_dir_entry(char* filename, uint32_t inode_no, enum file_types file_type, struct dir_entry* p_de)
{
    ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);
    
    memcpy(p_de->filename, filename, strlen(filename));
    p_de->i_no = inode_no;
    p_de->f_type = file_type;
}

//将目录项写入父目录中
bool sync_dir_entry(struct dir* parent_dir, struct dir_entry* p_de, void* io_buf)
{
    struct inode* dir_inode = parent_dir->inode;
    uint32_t dir_size = dir_inode->i_size;//文件大小
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;

    ASSERT(dir_size % dir_entry_size == 0);//dir_size应该是entry的整数倍

    //一个扇区可以存储的目录项数量
    uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size);

    int32_t block_lba = -1;

    uint8_t block_idx = 0;
    //存储每个块的地址
    uint32_t all_blocks[12 + 128] = {0};
    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }

    //dir_e用来在io_buf中遍历目录项
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;

    int32_t block_bitmap_idx = -1;

    block_idx = 0;
    //在当前文件中寻找空闲位置存储该目录项，如果没有了，就在不超过文件大小（140个块）的前提下申请扇区存储新目录项
    while (block_idx < 12 + 128) {
        block_bitmap_idx = -1;
        if (all_blocks[block_idx] == 0) {
            //如果走到这个分支就证明确实没找到，那么只能在这里申请一个扇区
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) {
                printk("alloc block bitmap for sync_dir_entry failed\n");
                return false;
            }
            //刚分配的这块扇区在bitmap中的位置，必须火速同步到硬盘
            block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
            ASSERT(block_bitmap_idx != -1);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

            block_bitmap_idx = -1;
            if (block_idx < 12) {
                //必须同步到inode的地址表中
                dir_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
            } else if (block_idx == 12) {
                dir_inode->i_sectors[block_idx] = block_lba;//这块是给间接表存地址用的
                block_lba = -1;
                block_lba = block_bitmap_alloc(cur_part);//这块是间接文件实体块0
                if (block_lba == -1) {
                    block_bitmap_idx = dir_inode->i_sectors[block_idx] - cur_part->sb->data_start_lba;
                    bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);//如果间接块分配失败，就把刚才分配的地址块给还回去
                    dir_inode->i_sectors[block_idx] = 0;
                    printk("alloc block bitmap for sync_dir_entry failed\n");
                    return false;
                }
                //分配一块就必须同步硬盘一次
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                ASSERT(block_bitmap_idx != -1);
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                all_blocks[block_idx] = block_lba;
                //把刚分配的间接块0地址写入一级间接块表
                ide_write(cur_part->my_disk, dir_inode->i_sectors[block_idx], all_blocks + block_idx, 1);
            } else {
                //block_idx12 == 0表示第一个间接块都没有，要把第一个间接块和间接地址表一起分配了
                //如果idx > 12表示已经有第一个间接块了，不需要再分配地址块了
                all_blocks[block_idx] = block_lba;
                //其它间接块分配后，要立即更新地址表，从all blocks12开始的128个单元是一个地址块的扇区内容
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            }

            memset(io_buf, 0, SECTOR_SIZE);
            //将传入的目录项写入：1内存中父目录 2硬盘中
            memcpy(io_buf, p_de, dir_entry_size);
            ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
            dir_inode->i_size += dir_entry_size;
            return true;
        }

        //如果block idx已存在，则将其读入内存io buf
        ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
        uint8_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entrys_per_sec) {
            //目录项初始化后或删除后都会是unknown（0），所以要挑unknown的用
            if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN) {
                //更新目录中的目录项
                memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);
                //将更新了目录项的目录写回硬盘
                ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
                dir_inode->i_size += dir_entry_size;
                return true;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }
    printk("directory is full!\n");
    return false;
}

//把分区目录pdir中编号位inode_no的目录项删除
bool delete_dir_entry(struct partition* part, struct dir* pdir, uint32_t inode_no, void* io_buf)
{
    struct inode* dir_inode = pdir->inode;
    uint32_t block_idx = 0, all_blocks[12 + 128] = {0};

    //收集目录的所有文件块地址
    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if (dir_inode->i_sectors[12]) {
        ide_read(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
    }

    //目录项存储的时候保证了不会跨扇区
    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = SECTOR_SIZE / dir_entry_size;
    
    //io_buf中的目录项地址
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    struct dir_entry* dir_entry_found = NULL;
    uint8_t dir_entry_idx, dir_entry_cnt;
    bool is_dir_first_block = false;//是否是当前目录的第一个块

    //遍历所有块寻找目录项
    block_idx = 0;
    while (block_idx < 140) {
        is_dir_first_block = false;
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        //找到一个不为0的块，读出其中数据开始查找
        dir_entry_idx = dir_entry_cnt = 0;
        memset(io_buf, 0, SECTOR_SIZE);
        ide_read(part->my_disk, all_blocks[block_idx], io_buf, 1);
        //遍历其中目录项
        while (dir_entry_idx < dir_entrys_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN) {
                if (!strcmp((dir_e + dir_entry_idx)->filename, ".")) {
                    is_dir_first_block = true;//如果当前文件块中有.目录项，就表明这是目录项的第一块
                } else if (strcmp((dir_e + dir_entry_idx)->filename, ".") && strcmp((dir_e + dir_entry_idx)->filename, "..")) {
                    //如果当前目录项不是.和..则是要判断和统计的目录项
                    dir_entry_cnt++;
                    if ((dir_e + dir_entry_idx)->i_no == inode_no) {
                        ASSERT(dir_entry_found == NULL);
                        //找到目录项就用found指向它，找到后也继续遍历，统计所有目录项数
                        dir_entry_found = dir_e + dir_entry_idx;
                    }
                }
            }
            dir_entry_idx++;
        }

        if (dir_entry_found == NULL) {
            block_idx++;
            continue;//本文件块没找到就继续去找
        }

        //找到后的处理
        ASSERT(dir_entry_cnt >= 1);
        //如果当前块只有这一个目录项，并且不是第一个块，则将此块一起收回，节省空间
        if (dir_entry_cnt == 1 && !is_dir_first_block) {
            uint32_t block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);//直接回收整个块，目录项都不用清
            bitmap_sync(part, block_bitmap_idx, BLOCK_BITMAP);

            if (block_idx < 12) {
                dir_inode->i_sectors[block_idx] = 0;//从直接块中去除
            } else {
                //如果间接只有这一个块，那么连地址块一起收回
                uint32_t indirect_blocks = 0;
                uint32_t indirect_block_idx = 12;
                while (indirect_block_idx < 140) {
                    //遍历所有间接块，统计用了多少块
                    if (all_blocks[indirect_block_idx] != 0) {
                        indirect_blocks++;
                    }
                }
                ASSERT(indirect_blocks >= 1);

                if (indirect_blocks > 1) {
                    all_blocks[block_idx] = 0;
                    //把当前块在间接地址块中除名，间接地址块不要动
                    ide_write(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
                } else {
                    //如果只有这一个间接块，那么连地址块一起回收了
                    block_bitmap_idx = dir_inode->i_sectors[12] - part->sb->data_start_lba;
                    bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);//直接回收整个块，目录项都不用清
                    bitmap_sync(part, block_bitmap_idx, BLOCK_BITMAP);
                    dir_inode->i_sectors[12] = 0;//不要忘了改inode的地址索引
                }
            }
        } else {
            //如果是第一个块，那么清除目录项就好了，前边差inode的时候打开过了，直接用即可
            memset(dir_entry_found, 0, dir_entry_size);
            ide_write(part->my_disk, all_blocks[block_idx], io_buf, 1);
        }

        ASSERT(dir_inode->i_size >= dir_entry_size);
        dir_inode->i_size -= dir_entry_size;
        memset(io_buf, 0, SECTOR_SIZE * 2);//inode可能跨扇区，所以要两个扇区大小
        inode_sync(part, dir_inode, io_buf);//改动了dir inode，需要同步到硬盘

        return true;
    }
    //遍历了所有目录文件块都没有找到inode_no文件
    return false;
}

//读取目录，返回其中一个目录项
struct dir_entry* dir_read(struct dir* dir)
{
    struct dir_entry* dir_e = (struct dir_entry*)dir->dir_buf;//终于用到dir buf了
    struct inode* dir_inode = dir->inode;
    uint32_t all_blocks[12 + 128] = {0}, block_cnt = 12;
    uint32_t block_idx = 0, dir_entry_idx = 0;

    //典中典之保存所有文件块地址
    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }

    if (dir_inode->i_sectors[12] != 0) {
        ide_read(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;
    }

    block_idx = 0;
    //当前文件块的偏移
    uint32_t cur_dir_entry_pos = 0;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = SECTOR_SIZE / dir_entry_size;

    //遍历当前扇区的目录项
    while (block_idx < block_cnt) {
    //while (dir->dir_pos < dir_inode->i_size) {
        if (dir->dir_pos >= dir_inode->i_size) {
            return NULL;
        }
        //如果块地址为0则不遍历
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        memset(dir_e, 0, SECTOR_SIZE);
        //读一个块到dir缓冲里
        ide_read(cur_part->my_disk, all_blocks[block_idx], dir_e, 1);
        dir_entry_idx = 0;
        //刚才读出来了，现在开始遍历
        while (dir_entry_idx < dir_entrys_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN) {
                //如果当前pos小于dir pos，表明之前已经返回过几条目录项了，要从上次返回的位置继续
                if (cur_dir_entry_pos < dir->dir_pos) {
                    //一直continue直到当前dir pos位置，把此处dir entry返回即可
                    cur_dir_entry_pos += dir_entry_size;
                    dir_entry_idx++;
                    continue;
                }
                ASSERT(cur_dir_entry_pos == dir->dir_pos);
                //修改dir pos后移一条
                dir->dir_pos += dir_entry_size;
                return dir_e + dir_entry_idx;
            }
            //为啥这里不让dir post后移？dir pos到底指向哪里，现在看是每找成功一条，dir pos就后移一条，但是成功这条不一定是dir pos指向的
            //知道了，看348行，dir pos是把目录文件中的空洞去除后的文件指针，这下明白了，并无实际意义
            dir_entry_idx++;
        }
        block_idx++;
    }
    return NULL;
}

//判断目录是否非空，1表示空，0表示非空
bool dir_is_empty(struct dir* dir)
{
    struct inode* dir_inode = dir->inode;
    //如果目录中只有.和.. 则表明目录已空
    return (dir_inode->i_size == cur_part->sb->dir_entry_size * 2);
}

//在父目录中删除子目录
int32_t dir_remove(struct dir* parent_dir, struct dir* child_dir)
{
    struct inode* child_dir_inode = child_dir->inode;
    //空目录只在sector[0]有文件块，其它块都应该是空的
    int32_t block_idx = 1;
    while (block_idx < 13) {
        ASSERT(child_dir_inode->i_sectors[block_idx] == 0);
        block_idx++;
    }

    void* io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL) {
        printk("dir_remove: malloc for io_buf failed\n");
        return -1;
    }

    //在父目录中删除子目录项
    delete_dir_entry(cur_part, parent_dir, child_dir_inode->i_no, io_buf);

    //回收子目录唯一的文件块所占扇区以及Inode
    inode_release(cur_part, child_dir_inode->i_no);
    sys_free(io_buf);
    return 0;
}