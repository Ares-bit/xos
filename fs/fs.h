#ifndef __FS_FS_H
#define __FS_FS_H
#include "stdint.h"

#define MAX_FILES_PER_PART  4096//每个分区最大创建文件数
#define BITS_PER_SECTOR     4096//每个扇区位数
#define SECTOR_SIZE         512//扇区字节数
#define BLOCK_SIZE          SECTOR_SIZE//块字节数

#define MAX_PATH_LEN        512//路径最大长度
#define MAX_FILE_OPEN       32
#define MAX_FILE_NAME_LEN   16

struct partition* cur_part;//默认情况下操作的是哪个分区
extern struct dir root_dir;

enum file_types {
    FT_UNKNOWN,
    FT_REGULAR,//普通文件
    FT_DIRECTORY//目录
};

//打开文件选项
enum oflags {
    O_RDONLY,
    O_WRONLY,
    O_RDWR,
    O_CREAT = 4//如果是3就是0x11，没法跟其他的或在一起用了，所以是4 0x100
};

//用来记录查找文件过程中走过的地方，记录最先找不到的路径，即路径断裂的位置
struct path_search_record {
    char searched_path[MAX_PATH_LEN];//查找过程中的父路径
    struct dir* parent_dir;//文件或目录所在的直接父目录
    enum file_types file_type;//找到的是普通文件还是目录，找不到的为未知类型
};

int32_t sys_open(const char* pathname, enum oflags flags);
int32_t sys_close(uint32_t fd);
void filesys_init();

#endif