#ifndef __FS_DIR_H
#define __FS_DIR_H

#include "fs.h"
#include "inode.h"
#include "stdint.h"

struct dir root_dir;

//目录结构 只存在内存中
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

struct dir_entry* dir_read(struct dir* dir);
void dir_close(struct dir* dir);
struct dir* dir_open(struct partition* part, uint32_t inode_no);
bool search_dir_entry(struct partition* part, struct dir* pdir, const char* name, struct dir_entry* dir_e);
bool delete_dir_entry(struct partition* part, struct dir* pdir, uint32_t inode_no, void* io_buf);
bool sync_dir_entry(struct dir* parent_dir, struct dir_entry* p_de, void* io_buf);
#endif