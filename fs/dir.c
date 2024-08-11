#include "dir.h"
#include "inode.h"
#include "memory.h"
#include "string.h"
#include "file.h"

struct dir root_dir;

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
    struct dir* pdir = (struct dir*)sys_malloc(sizeof(struct dir*));
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
                ide_write(cur_part->my_disk, dir_inode->i_sectors[block_idx], &all_blocks[block_idx], 1);
            } else {
                //block_idx12 == 0表示第一个间接块都没有，要把第一个间接块和间接地址表一起分配了
                //如果idx > 12表示已经有第一个间接块了，不需要再分配地址块了
                all_blocks[block_idx] = block_lba;
                //其它间接块分配后，要立即更新地址表，从all blocks12开始的128个单元是一个地址块的扇区内容
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], &all_blocks[12], 1);
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