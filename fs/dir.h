#ifndef __FS_DIR_H
#define __FS_DIR_H

#include "fs.h"
#include "inode.h"
#include "stdint.h"

#define MAX_FILE_NAME_LEN   16

//目录结构
struct dir {
    struct inode *inode;
    uint32_t dir_pos;//记录在目录内的偏移
    uint8_t dir_buf[512];//目录的数据缓存
};

//目录项结构
struct dir_entry {
    char filename[MAX_FILE_NAME_LEN];
    uint32_t i_no;
    enum file_types f_type;
};

bool search_dir_entry(struct partition* part, struct dir* pdir, const char* name, struct dir_entry* dir_e);

#endif