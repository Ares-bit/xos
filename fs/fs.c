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
#include "file.h"
#include "thread.h"

extern struct file file_table[MAX_FILE_OPEN];
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

//搜索文件，找到返回inode号，找不到返回-1（用循环代替递归）
static int search_file(const char* pathname, struct path_search_record* searched_record)
{
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_DIRECTORY;
        searched_record->searched_path[0] = 0;//空
        return 0;
    }

    uint32_t path_len = strlen(pathname);
    //仅支持绝对路径，路径名开头必须是/
    ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);//最后一位装/0结尾，所以不能小于等于
    char* sub_path = (char*)pathname;
    struct dir* parent_dir = &root_dir;
    struct dir_entry dir_e;

    //解析出的每级路径名称存放处
    char name[MAX_FILE_NAME_LEN] = {0};
    searched_record->parent_dir = parent_dir;
    searched_record->file_type = FT_UNKNOWN;//现在还不确定根目录下的第一级能不能找到
    uint32_t parent_inode_no = 0;//父目录的inode号

    sub_path = path_parse(sub_path, name);
    while (name[0]) {
        ASSERT(strlen(searched_record->searched_path) < MAX_PATH_LEN);

        //记录刚才已经走过的父路径
        strcat(searched_record->searched_path, "/");//第一次表示是根目录，后续就是分隔符了
        strcat(searched_record->searched_path, name);

        //在根目录中找name
        if (search_dir_entry(cur_part, parent_dir, name, &dir_e)) {
            memset(name, 0, MAX_FILE_NAME_LEN);

            if (sub_path) {
                sub_path = path_parse(sub_path, name);
            }

            if (FT_DIRECTORY == dir_e.f_type) {//如果当前找到的是目录
                parent_inode_no = parent_dir->inode->i_no;
                dir_close(parent_dir);//关闭当前的父亲
                parent_dir = dir_open(cur_part, dir_e.i_no);//以自己作为父亲，即从此在自己下级搜索
                searched_record->parent_dir = parent_dir;
                continue;
            } else if (FT_REGULAR == dir_e.f_type) {//如果找到的是普通文件，则直接返回当前文件inode no
                searched_record->file_type = FT_REGULAR;//它的父亲是自己所在目录
                //为啥这里不关闭父目录？？？
                return dir_e.i_no;
            }
        } else {
            return -1;//如果没找到返回-1
        }
    }

    //如果遍历了完整路径还没出现-1，那就说明本次找的是最后一个目录
    dir_close(searched_record->parent_dir);//关闭最后一次查找打开的自己，否则会内存泄漏
    searched_record->parent_dir = dir_open(cur_part, parent_inode_no);//关闭自己后，将父目录改为自己的父目录，上面备份就是为了这里
    searched_record->file_type = FT_DIRECTORY;
    return dir_e.i_no;//返回自己的inode no
}

//打开文件或创建文件成功后返回fd
int32_t sys_open(const char* pathname, enum oflags flags)
{   
    //这样也不行，目录也可以最后不带/啊，这样约束不住的，别急，下边还有拦截
    if (pathname[strlen(pathname) - 1] == '/') {
        printk("can't open a directory %s\n", pathname);
        return -1;
    }

    ASSERT(flags <= O_RDONLY | O_WRONLY | O_RDWR | O_CREAT);//0+1+2+4=7
    
    int32_t fd = -1;

    //必须清0，也可以初始化成全0，防止上次运行干扰
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    //记录目录深度，用于判断中间某个目录不存在的情况
    uint32_t pathname_depth = path_depth_cnt((char*)pathname);

    //先判断这个文件是否已经创建或打开了，为什么不先去打开队列上查一遍，因为提前不知道inode没法查
    //inode中存一个自己的路径？可不可行？这样找起来更快？
    int inode_no = search_file(pathname, &searched_record);
    bool found = inode_no != -1 ? true : false;

    if (searched_record.file_type == FT_DIRECTORY) {
        printk("can't open a directory with open(), use opendir() to instead\n");
        dir_close(searched_record.parent_dir);//查找到目录后会自动打开，所以这里要关闭
        return -1;
    }

    //这步是为了防止之前说的，如果输入/a/b/c，其中b是文件的情况，这种情况也会查找成功，只不过找到b就停了，但是返回的路径长度是不一样的，需要自己判断
    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
    if (path_searched_depth != pathname_depth) {
        printk("can't access %s: Not a directory, subpath %s isn't exist\n", pathname, searched_record.searched_path);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    //如果没有找到，并且flag不是create，就报错，如果找到最后一个文件没找到，那么输出寻找路径是整个输入路径，所以取最后/后的字符串就是文件名
    if (!found && !(flags & O_CREAT)) {
        printk("in path %s, file %s isn't exist\n", searched_record.searched_path, strrchr(searched_record.searched_path, '/') + 1);
        dir_close(searched_record.parent_dir);
        return -1;
    } else if (found && flags & O_CREAT) {//要创建的文件已存在则直接报错
        printk("%s has already exist!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    switch(flags & O_CREAT) {
        case O_CREAT:
            printk("creating file\n");
            fd = file_create(searched_record.parent_dir, strrchr(pathname, '/') + 1, flags);
            dir_close(searched_record.parent_dir);
            break;
        default:
            //除创建操作都交给file_open处理
            fd = file_open(inode_no, flags);
            break;
    }

    return fd;
}

//根据当前进程fd找到对应的全局文件表下标
static uint32_t fd_local2glocal(uint32_t local_fd)
{
    struct task_struct* cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd > 0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

//关闭文件描述符fd指向的文件
int32_t sys_close(uint32_t fd)
{
    int32_t ret = -1;
    struct task_struct* cur = running_thread();

    if (fd > 2) {
        uint32_t _fd = fd_local2glocal(fd);
        ret = file_close(&file_table[_fd]);
        cur->fd_table[fd] = -1;
    }
    return ret;
}

int32_t sys_write(int32_t fd, const void* buf, uint32_t count)
{
    if (fd < 0) {
        printk("sys_write: fd error!\n");
        return -1;
    }

    //sys_write原功能，printk没有用到sys_write，它直接用put_str
    if (fd == stdout_no) {
        //count超过1024咋办，其他print位置也都没加保护，原来的sys write都没限制这个，原来传str
        char tmp_buf[1025] = {0};
        memcpy(tmp_buf, buf, count);
        console_put_str(tmp_buf);
        return count;
    }

    uint32_t _fd = fd_local2glocal(fd);
    struct file* wr_file = &file_table[_fd];
    if (wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR) {
        uint32_t bytes_written = file_write(wr_file, buf, count);
        return bytes_written;
    } else {
        console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY\n");
        return -1;
    }
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

                    //判断该分区是否存在文件系统，为了方便定位，这里改成不管怎么样都格式化硬盘，创建目录的时候写硬盘会填全0，所以会覆盖之前的目录项导致文件检测不出来之前是否存在
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

    //将当前分区根目录打开
    open_root_dir(cur_part);

    //初始化文件表
    uint32_t fd_idx = 0;
    while (fd_idx < MAX_FILE_OPEN) {
        file_table[fd_idx++].fd_inode = NULL;
    }
}