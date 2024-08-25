#ifndef __FS_FILE_H
#define __FS_FILE_H

#include "inode.h"
#include "fs.h"

struct file {
    uint32_t fd_pos;//记录当前文件操作偏移地址
    enum oflags fd_flag;//标识文件可读可写
    struct inode* fd_inode;//文件指向的inode
};

enum std_fd {
    stdin_no,   //0标准输入
    stdout_no,  //1标准输出
    stderr_no   //2标准错误
};

enum bitmap_type {
    INODE_BITMAP,   //inode位图
    BLOCK_BITMAP    //块位图
};

int32_t inode_bitmap_alloc(struct partition* part);
int32_t block_bitmap_alloc(struct partition* part);
void bitmap_sync(struct partition* part, uint32_t bit_idx, enum bitmap_type btmp_type);
int32_t file_create(struct dir* parent_dir, char* filename, enum oflags flag);
int32_t file_close(struct file* file);
int32_t file_read(struct file* file, void* buf, uint32_t count);
int32_t file_write(struct file* file, const void* buf, uint32_t count);
#endif
