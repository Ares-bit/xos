#ifndef __FS_FS_H
#define __FS_FS_H

#define MAX_FILES_PER_PART  4096//每个分区最大创建文件数
#define BITS_PER_SECTOR     4096//每个扇区位数
#define SECTOR_SIZE         512//扇区字节数
#define BLOCK_SIZE          SECTOR_SIZE//块字节数

struct partition* cur_part;//默认情况下操作的是哪个分区

enum file_types {
    FT_UNKNOWN,
    FT_REGULAR,//普通文件
    FT_DIRECTORY//目录
};

void filesys_init();

#endif