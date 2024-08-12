#include "fs.h"
#include "stdint.h"
#include "global.h"
#include "inode.h"
#include "dir.h"
#include "stdio_kernel.h"
#include "ide.h"
#include "string.h"
#include "debug.h"
#include "memory.h"

extern uint8_t channel_cnt;//按硬盘数计算的通道数
extern struct ide_channel channels[2];//有两个ide通道
extern struct list partition_list;//分区队列

static bool mount_partition(struct list_elem* pelem, int arg)
{
    char* part_name = (char*)arg;
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    if (!strcmp(part->name, part_name)) {//如果两个名字一样
        cur_part = part;
        struct disk* hd = cur_part->my_disk;
        
        //难道SECTOR_SIZE还跟struct super_block大小不一样？？？？？？是一样的啊。。。
        //printk("----sec:%d, super:%d\n", SECTOR_SIZE, sizeof(struct super_block));

        //存储从硬盘读入的超级块
        struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);
        
        //在内存中创建cur_part的超级块
        cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));  
        if (cur_part->sb == NULL) {
            PANIC("alloc memory failed!");
        }

        memset(sb_buf, 0, SECTOR_SIZE);
        //读入传入分区的超级块
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);

        //拷贝超级块到当前part指向的sb中
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

        //将硬盘中的bitmap读入内存
        //block
        cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
        if (cur_part->block_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
        ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);

        //inode
        cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
        if (cur_part->inode_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);

        //本分区打开的inode节点队列
        list_init(&cur_part->open_inodes);

        printk("mount %s done!\n", part->name);

        //list_traversal看到返回值是true才会停止遍历
        return true;
    }
    //继续遍历
    return false;
}

//格式化分区：初始化分区的元信息，创建文件系统
static void partition_format(struct partition* part)
{
    uint32_t boot_sector_sects = 1;
    uint32_t super_block_sects = 1;
    //inode位图占用扇区数，最多4096文件，即4096bit
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
    uint32_t inode_table_sects = DIV_ROUND_UP(sizeof(struct inode) * MAX_FILES_PER_PART, SECTOR_SIZE);
    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;//分区表中会记录分区全部扇区数

    //可用block位图占用扇区数
    uint32_t block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    //算上可用block位图后，可用block数量会减少，要重新计算可用block数和位图占用扇区数
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

    //超级块初始化
    struct super_block sb;
    sb.magic = 0x20001212;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;
    sb.block_bitmap_lba = sb.part_lba_base + 2;//跨过boot + super
    sb.block_bitmap_sects = block_bitmap_sects;
    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;
    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;
    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;//这根目录的inode不还是0么。。。那为啥不直接默认0，还要存一下
    sb.dir_entry_size = sizeof(struct dir_entry);

    printk("%s info:\n", part->name);
    printk("magic:0x%x\n part_lba_base:0x%x\n all_sectors:0x%x\n inode_cnt:0x%x\n block_bitmap_lba:0x%x\n block_bitmap_sectors:0x%x\n inode_bitmap_lba:0x%x\n inode_bitmap_sectors:0x%x\n inode_table_lba:0x%x\n inode_table_sectors:0x%x\n data_start_lba:0x%x\n",
            sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt,
            sb.block_bitmap_lba, sb.block_bitmap_sects,
            sb.inode_bitmap_lba, sb.inode_bitmap_sects,
            sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);

    struct disk* hd = part->my_disk;

    ide_write(hd, part->start_lba + 1, &sb, 1);//把super写入分区第二块，跳过boot
    printk("super_block_lba:0x%x\n", part->start_lba + 1);

    //找出数据量最大的元信息，用其尺寸做存储缓冲区???为啥要对比，你初始化bitmap，那直接申请bitmap sects不行吗？？？
    //哦哦，后续都要用这个buf，所以按照最大的申请
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);
    buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
    uint8_t* buf = (uint8_t*)sys_malloc(buf_size);

    //初始化block bitmap
    buf[0] |= 0x1;//0x1给根目录预留
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;//bitmap占用字节数，也是最后一个字节
    uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;//找出最后一个字节里面的最后有效bit
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);//找出最后一个块被bitmap占用了多少字节
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);//把bitmap所占最后一个扇区的字节全部置1，为了防止超出控制范围的block也被用了，提前给置1

    uint8_t bit_idx = 0;
    while (bit_idx <= block_bitmap_last_bit) {
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);//bit_idx表示最后一个字节内有效块的bit，这些因为在上一步中一起置0xff，所以要置回来
    }
    //block bitmap写入磁盘
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

    //初始化inode bitmap
    memset(buf, 0, buf_size);//清除上一次的配置
    buf[0] |= 0x1;
    //因为4096个inode bit正好占一个扇区，没有多余位，所以直接写进去就可
    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

    //初始化inode table(根目录inode)
    memset(buf, 0, buf_size);
    struct inode* i = (struct inode*)buf;
    i->i_size = sb.dir_entry_size * 2;//.和..
    i->i_no = 0;//根目录在0
    i->i_sectors[0] = sb.data_start_lba;
    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

    //初始化根目录
    //写入根目录的两个初始目录项.和..
    memset(buf, 0, buf_size);
    struct dir_entry* p_de = (struct dir_entry*)buf;
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = 0;//根的自己指向自己0
    p_de->f_type = FT_DIRECTORY;
    p_de++;

    memcpy(p_de->filename, "..", 2);
    p_de->i_no = 0;//根的前级指向自己0
    p_de->f_type = FT_DIRECTORY;

    ide_write(hd, sb.data_start_lba, buf, 1);

    printk("root_dir_lba:0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);

    sys_free(buf);
}

//将最上层路径名称解析出来
static char* path_parse(char* pathname, char* name_store)
{
    if (pathname[0] == '/') {//根目录不需要解析
        while (*(++pathname) == '/');//如果开头连续出现多个/，跳过他们按一个处理
    }

    //拿/后目录名，遇到下一个/跳出去
    while (*pathname != '/' && *pathname != 0) {
        *name_store++ = *pathname++;
    }

    if (pathname[0] == 0) {
        return NULL;
    }
    //返回下次要解析的首地址
    return pathname;
}

//返回路径深度，/a/b/c深度为3
int32_t path_depth_cnt(char* pathname)
{
    ASSERT(pathname != NULL);
    char* p = pathname;
    char name[MAX_FILE_NAME_LEN] = {0};

    uint32_t depth = 0;
    p = path_parse(p, name);
    while (name[0]) {
        depth++;
        memset(name, 0, MAX_FILE_NAME_LEN);
        if (p) {
            p = path_parse(p, name);
        }
    }
    return depth;
}

//在磁盘上搜索文件系统，若没有则格式化分区创建文件系统
void filesys_init()
{
    uint8_t channel_no = 0, dev_no, part_idx = 0;

    //存储从分区中读取的super
    struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

    if (sb_buf == NULL) {
        PANIC("alloc memory failed!");
    }
    printk("searching filesystem......\n");
    while(channel_no < channel_cnt) {
        dev_no = 0;
        while(dev_no < 2) {
            if (dev_no == 0) {//跨过裸盘
                dev_no++;
                continue;
            }
            struct disk* hd = &channels[channel_no].devices[dev_no];
            struct partition* part = hd->prim_parts;
            while(part_idx < 12) {//4主 8逻辑
                if (part_idx == 4) {
                    part = hd->logic_parts;
                }

                if (part->sec_cnt != 0) {//如果分区存在
                    memset(sb_buf, 0, SECTOR_SIZE);

                    //读出分区的第2个扇区super
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);

                    //判断该分区是否存在文件系统
                    if (sb_buf->magic == 0x20001212) {
                        printk("%s has filesystem\n", part->name);
                    } else {
                        printk("formatting %s's partition %s......\n", hd->name, part->name);
                        partition_format(part);
                    }
                }
                part_idx++;
                part++;
            }
            dev_no++;
        }
        channel_no++;
    }

    sys_free(sb_buf);

    char default_part[8] = "sdb1";
    //ide init时会把所有分区都挂到partition list上
    list_traversal(&partition_list, mount_partition, (int)default_part);
}