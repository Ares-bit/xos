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
#include "ioqueue.h"
#include "keyboard.h"
#include "pipe.h"

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
char* path_parse(char* pathname, char* name_store)
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
static int32_t search_file(const char* pathname, struct path_search_record* searched_record)
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
                //为啥这里不关闭父目录？？？应该都放在主调函数做了
                return dir_e.i_no;
            }
        } else {
            //这里应该要关父目录，但不知为何没写
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

    ASSERT(flags <= (O_RDONLY | O_WRONLY | O_RDWR | O_CREAT));//0+1+2+4=7
    
    int32_t fd = -1;

    //必须清0，也可以初始化成全0，防止上次运行干扰
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    //记录目录深度，用于判断中间某个目录不存在的情况
    uint32_t pathname_depth = path_depth_cnt((char*)pathname);

    //先判断这个文件是否已经创建或打开了，为什么不先去打开队列上查一遍，因为提前不知道inode没法查
    //inode中存一个自己的路径？可不可行？这样找起来更快？
    uint32_t inode_no = search_file(pathname, &searched_record);
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
uint32_t fd_local2global(uint32_t local_fd)
{
    struct task_struct* cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

//关闭文件描述符fd指向的文件
int32_t sys_close(int32_t fd)
{
    int32_t ret = -1;
    struct task_struct* cur = running_thread();

    if (fd > 2) {
        uint32_t _fd = fd_local2global(fd);
        if (is_pipe(fd)) {
            //如果没有进程打开管道则释放内存
            if (--file_table[_fd].fd_pos == 0) {
                mfree_page(PF_KERNEL, file_table[_fd].fd_inode, 1);
                file_table[_fd].fd_inode = NULL;
            }
            ret = 0;
        } else {
            ret = file_close(&file_table[_fd]);
        }
        cur->fd_table[fd] = -1;//使该描述符可被别人重新使用
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
    } else if (is_pipe(fd)) {
        return pipe_write(fd, buf, count);
    } else {
        uint32_t _fd = fd_local2global(fd);
        struct file* wr_file = &file_table[_fd];
        if (wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR) {
            uint32_t bytes_written = file_write(wr_file, buf, count);
            return bytes_written;
        } else {
            console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY\n");
            return -1;
        }
    }
}

//从fd指向的文件读取count字节到buf
int32_t sys_read(int32_t fd, void* buf, uint32_t count)
{
    ASSERT(buf != NULL);
    int32_t ret = -1;
    if (fd < 0 || fd == stdout_no || fd == stderr_no) {
        printk("sys_read: fd error!\n");
    } else if (fd == stdin_no) {
        //增加从键盘读入字符
        char* buffer = (char*)buf;
        uint32_t bytes_read = 0;
        while (bytes_read < count) {
            *buffer = ioq_getchar(&kbd_buf);
            bytes_read++;
            buffer++;
        }
        ret = (bytes_read == 0 ? -1 : (int32_t)bytes_read);
    } else if (is_pipe(fd)) {
        ret = pipe_read(fd, buf, count);
    } else {
        uint32_t _fd = fd_local2global(fd);
        ret = file_read(&file_table[_fd], buf, count);
    }
    return ret;
}

//设置文件指针位置，成功时返回新偏移量
int32_t sys_lseek(int32_t fd, int32_t offset, enum whence whence)
{
    if (fd < 0) {
        printk("sys_lseek: fd error!\n");
        return -1;       
    }
    ASSERT(whence > 0 && whence < 4);
    uint32_t _fd = fd_local2global(fd);
    struct file* pf = &file_table[_fd];
    int32_t new_pos = 0;
    int32_t file_size = (int32_t)pf->fd_inode->i_size;
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;//从文件起始处偏移Offset
            break;
        case SEEK_CUR:
            new_pos = (int32_t)pf->fd_pos + offset;//从当前指针位置偏移offet
            break;
        case SEEK_END:
            new_pos = file_size + offset;//从文件最后一字节下一字节偏移offset
            break;
    }
    //新指针位置必须位于文件范围内，如果直接定位到SEEK_END offset = 0 也是返回-1
    if (new_pos < 0 || new_pos > (file_size - 1)) {
        return -1;
    }

    pf->fd_pos = new_pos;
    return pf->fd_pos;
}

//删除文件（非目录）成功返回0
int32_t sys_unlink(const char* pathname)
{
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    //先判断文件是否存在
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    int32_t inode_no = search_file(pathname, &searched_record);

    ASSERT(inode_no != 0);
    if (inode_no == -1) {
        printk("file %s not found!\n", pathname);
        dir_close(searched_record.parent_dir);//这一步又忘了，查完之后会自动打开目录的，一定要关闭
        return -1;
    }
    //中间应该还少一步判断：如果输入是目录中间夹文件的路径，就会返回中间的文件inode，这个要判断返回和输入路径是否一致才行
    //还要看查找目标是否是目录，如果是目录本函数不支持删除
    if (searched_record.file_type == FT_DIRECTORY) {
        printk("can't delete a directory with unlink(), use rmdir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    //检查是否在已打开文件列表(全局文件表)中
    uint32_t file_idx = 0;
    while (file_idx < MAX_FILE_OPEN) {
        if (file_table[file_idx].fd_inode != NULL && file_table[file_idx].fd_inode->i_no == (uint32_t)inode_no) {
            //如果该文件再删除时已被打开，则不允许删除
            break;
        }
        file_idx++;
    }

    if (file_idx < MAX_FILE_OPEN) {
        dir_close(searched_record.parent_dir);
        printk("file %s is in use, not allow to delete!\n", pathname);
        return -1;
    }

    ASSERT(file_idx == MAX_FILE_OPEN);

    void* io_buf = sys_malloc(SECTOR_SIZE * 2);//删除目录项会改变dir inode，要同步Inode，所以要两块
    if (io_buf == NULL) {
        dir_close(searched_record.parent_dir);
        printk("sys_unlink: malloc for io_buf failed!\n");
        return -1;
    }

    struct dir* parent_dir = searched_record.parent_dir;
    delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);
    inode_release(cur_part, inode_no);
    sys_free(io_buf);
    dir_close(searched_record.parent_dir);
    return 0;
}

//创建目录pathname
int32_t sys_mkdir(const char* pathname)
{
    uint8_t rollback_step = 0;
    void* io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL) {
        printk("sys_mkdir: sys_malloc for io_buf failed!\n");
        return -1;
    }

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    int32_t inode_no = -1;
    inode_no = search_file(pathname, &searched_record);
    if (inode_no != -1) {
        //找到同名则报错
        printk("sys_mkdir: file or directory %s exist\n", pathname);
        rollback_step = 1;
        goto rollback;
    } else {
        //如果没找到也不能高兴创建，还要判断是否是中间某个路径断了
        uint32_t pathname_depth = path_depth_cnt((char*)pathname);
        uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
        if (pathname_depth != path_searched_depth) {
            printk("sys_mkdir: cannot access %s: Not a directory, subpath %s isn't exist\n", pathname, searched_record.searched_path);
            rollback_step = 1;
            goto rollback;
        }
    }

    struct dir* parent_dir = searched_record.parent_dir;
    //这里最好使用searched_path它不带末尾的/，因为输入pathname可能带末尾的/，下边这句就找不对位置了
    //获取要创建的目录项名字
    char* dirname = strrchr(searched_record.searched_path, '/') + 1;

    //分配inode，成功后不能马上同步，要到所有异常处理都过去后再同步，如果提前写到硬盘上，还要再改回去
    inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("sys_mkdir: allocate inode failed\n");
        rollback_step = 1;
        goto rollback;
    }

    //初始化新的inode
    struct inode new_dir_inode;
    inode_init(inode_no, &new_dir_inode);

    //分配文件块存储.和.. 因为要同步Bitmap，所以需要获取idx，alloc返回的是地址并非idx
    uint32_t block_bitmap_idx = 0;
    int32_t block_lba = -1;
    block_lba = block_bitmap_alloc(cur_part);//该函数返回Lba
    if (block_lba == -1) {
        printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
        rollback_step = 2;
        goto rollback;
    }
    new_dir_inode.i_sectors[0] = block_lba;
    block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    //bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

    //写入两个目录项
    memset(io_buf, 0, SECTOR_SIZE * 2);
    struct dir_entry* p_de = (struct dir_entry*)io_buf;

    memcpy(p_de->filename, ".", 1);
    p_de->i_no = inode_no;
    p_de->f_type = FT_DIRECTORY;

    p_de++;
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = parent_dir->inode->i_no;
    p_de->f_type = FT_DIRECTORY;
    //目录文件块写入硬盘
    ide_write(cur_part->my_disk, new_dir_inode.i_sectors[0], io_buf, 1);

    new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;

    //在父目录中添加自己的目录项
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(dirname, inode_no, FT_DIRECTORY, &new_dir_entry);
    memset(io_buf, 0, SECTOR_SIZE * 2);
    //将新建目录项写入父目录
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        printk("sys_mkdir: sync_dir_entry to disk failed!\n");
        rollback_step = 3;
        goto rollback;//如果554行还开着，那块block就收不回来，因为下边的rollback没回收它
    }
    //父目录inode大小发生变化，要同步回去
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, parent_dir->inode, io_buf);

    //同步自己的inode
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, &new_dir_inode, io_buf);    

    //同步inode位图
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    //同步block位图
    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

    sys_free(io_buf);
    //关闭父目录，总忘，记住吧只要search file就会打开父目录，得手动关闭
    dir_close(searched_record.parent_dir);
    return 0;

rollback:
    switch (rollback_step) {
        case 3:
            bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);//这个也要加，要不然回收不了block
        case 2:
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
        case 1:
            dir_close(searched_record.parent_dir);
            break;
    }
    sys_free(io_buf);
    return -1;
}

//打开目录成功返回目录指针
struct dir* sys_opendir(const char* name)
{
    ASSERT(strlen(name) < MAX_PATH_LEN);
    //如果传入name是/或/.则返回根目录，根目录是常开不关的
    if (name[0] == '/' && (name[1] == '\0' || name[1] == '.')) {
        return &root_dir;
    }

    struct path_search_record searched_record = {0};

    //查询目录inode是否存在
    int32_t inode_no = search_file(name, &searched_record);
    struct dir* ret = NULL;
    if (inode_no == -1) {
        printk("in %s, subdir %s not exist\n", name, searched_record.searched_path);
    } else {
        //成功了也要判断下深度是否正确，但是这里没写
        if (searched_record.file_type == FT_REGULAR) {
            //如果是文件则直接返回空
            printk("%s is regular file!\n", name);
        } else if (searched_record.file_type == FT_DIRECTORY) {
            //如果目录存在则打开后返回指针
            ret = dir_open(cur_part, inode_no);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

//关闭目录
int32_t sys_closedir(struct dir* dir)
{
    if (dir != NULL) {
        dir_close(dir);
        return 0;
    }
    return -1;
}

//读取dir中的一个目录项并返回其目录项地址
struct dir_entry* sys_readdir(struct dir* dir)
{
    ASSERT(dir != NULL);
    return dir_read(dir);
}

//把目录dir的指针dir_pos置0
void sys_rewinddir(struct dir* dir)
{
    dir->dir_pos = 0;
}

//删除空目录
int32_t sys_rmdir(const char* pathname)
{
    struct path_search_record searched_record = {0};

    //查询目录inode是否存在
    int32_t inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);//不许删除根目录
    int retval = -1;
    if (inode_no == -1) {
        printk("in %s, sub path %s not exist\n", pathname, searched_record.searched_path);
    } else {
        //总是忘这块，可能会查出一个文件来
        if (searched_record.file_type == FT_REGULAR) {
            printk("%s is regular file!\n", pathname);
        } else {
            struct dir* dir = dir_open(cur_part, inode_no);
            if (!dir_is_empty(dir)) {
                //如果目录非空不允许删除
                printk("dir %s is not empty, it is not allowed to delete a nonempty directory\n", pathname);
            } else {
                //如果是空则可以删除
                if (!dir_remove(searched_record.parent_dir, dir)) {
                    retval = 0;
                }
            }
            dir_close(dir);
        }
    }
    dir_close(searched_record.parent_dir);
    return retval;
}

//根据子目录inode获得父目录inode
static uint32_t get_parent_dir_inode_nr(uint32_t child_inode_nr, void* io_buf)
{   
    //传入的是子目录的inode
    struct inode* child_dir_inode = inode_open(cur_part, child_inode_nr);
    //子目录的..目录项中包含父目录inode
    uint32_t block_lba = child_dir_inode->i_sectors[0];
    ASSERT(block_lba >= cur_part->sb->data_start_lba);
    inode_close(child_dir_inode);
    //读取子目录第一个块
    ide_read(cur_part->my_disk, block_lba, io_buf, 1);
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    ASSERT(dir_e[1].i_no < MAX_FILES_OPEN_PER_PROC && dir_e[1].f_type == FT_DIRECTORY);
    return dir_e[1].i_no;//返回..指向的父目录inode
}

//在父目录中查找子目录的inode，并写入path中，因为子目录的名字记录在父目录项中
static int32_t get_child_dir_name(uint32_t p_inode_nr, uint32_t c_inode_nr, char* path, void* io_buf)
{
    struct inode* parent_dir_inode = inode_open(cur_part, p_inode_nr);
    //取出所有地址
    uint8_t block_idx = 0;
    uint32_t all_blocks[12 + 128] = {0}, block_cnt = 12;
    while (block_idx < 12) {
        all_blocks[block_idx] = parent_dir_inode->i_sectors[block_idx];
        block_idx++;
    }

    if (parent_dir_inode->i_sectors[12]) {
        ide_read(cur_part->my_disk, parent_dir_inode->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;
    }
    inode_close(parent_dir_inode);

    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = SECTOR_SIZE / dir_entry_size;

    //遍历所有块查找子目录的inode
    block_idx = 0;
    while (block_idx < block_cnt) {
        if (all_blocks[block_idx]) {
            ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
            uint8_t dir_e_idx = 0;
            //遍历目录项
            while (dir_e_idx < dir_entrys_per_sec) {
                if ((dir_e + dir_e_idx)->i_no == c_inode_nr) {
                    strcat(path, "/");
                    strcat(path, (dir_e + dir_e_idx)->filename);
                    return 0;
                }
                dir_e_idx++;
            }
        }
        block_idx++;
    }
    return -1;
}

char* sys_getcwd(char* buf, uint32_t size)
{
    //如果用户传空，放到用户态去调用malloc分配
    ASSERT(buf != NULL);
    void* io_buf = sys_malloc(SECTOR_SIZE);
    if (io_buf == NULL) {
        return NULL;
    }

    struct task_struct* cur_thread = running_thread();
    int32_t parent_inode_nr = 0;
    int32_t child_inode_nr = cur_thread->cwd_inode_nr;
    ASSERT(child_inode_nr >= 0 && child_inode_nr < MAX_FILES_OPEN_PER_PROC);

    //若当前目录是根目录，填写/后直接返回
    if (child_inode_nr == 0) {
        buf[0] = '/';
        buf[1] = '\0';
        sys_free(io_buf);
        return buf;
    }

    memset(buf, 0, size);
    char full_path_reverse[MAX_PATH_LEN] = {0};//用来做全路径缓冲区

    //从下往上逐层找父目录，直到找到根目录
    while (child_inode_nr != 0) {
        //获取父目录inode
        parent_inode_nr = get_parent_dir_inode_nr(child_inode_nr, io_buf);
        if (get_child_dir_name(parent_inode_nr, child_inode_nr, full_path_reverse, io_buf) == -1) {
            //如果父目录中没找到子目录，正常一定会找到的，因为pnode就是根据cnode得到的
            sys_free(io_buf);
            return NULL;
        }
        //再把父目录当子目录继续往上找
        child_inode_nr = parent_inode_nr;
    }
    //要保证最终路径小于buf长度
    ASSERT(strlen(full_path_reverse) <= size);

    //以上从下往上找，最终得到的路径在full_path_reverse是反的，需要逆序
    char* last_slash;//记录字符串中每个斜杠地址
    //从后往前找到每个/
    while (last_slash = strrchr(full_path_reverse, '/')) {
        uint16_t len = strlen(buf);
        strcpy(buf + len, last_slash);//把"/子目录"拷贝到buf中
        *last_slash = '\0';//把last_slash刚找到的/位置0，当做新的结尾，从此处继续往回找
    }
    sys_free(io_buf);
    return buf;
}

//更改当前工作目录为绝对路径path，工作路径不能为普通文件，必须是目录
int32_t sys_chdir(const char* path)
{
    int32_t ret = -1;
    struct path_search_record searched_record = {0};

    int32_t inode_no = search_file(path, &searched_record);
    if (inode_no != -1) {
        if (searched_record.file_type == FT_DIRECTORY) {
            struct task_struct* cur = running_thread();
            cur->cwd_inode_nr = inode_no;
            ret = 0;
        } else {
            printk("sys_chidr: %s is regular file or other!\n", path);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

//在buf中装填stat信息
int32_t sys_stat(const char* path, struct stat* buf)
{
    //如果是根目录则直接返回数据即可
    if (!strcmp(path, "/") || !strcmp(path, "/.") || !strcmp(path, "/..")) {
        buf->st_ino = 0;
        buf->st_filetype = FT_DIRECTORY;
        buf->st_size = root_dir.inode->i_size;
        return 0;
    }

    int32_t ret = -1;
    struct path_search_record searched_record = {0};
    int32_t inode_no = search_file(path, &searched_record);
    if (inode_no != -1) {
        struct inode* obj_inode = inode_open(cur_part, inode_no);
        buf->st_size = obj_inode->i_size;
        inode_close(obj_inode);
        buf->st_filetype = searched_record.file_type;
        buf->st_ino = inode_no;
        ret = 0;
    } else {
        printk("sys_stat: %s not found\n", path);
    }
    dir_close(searched_record.parent_dir);
    return ret;
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