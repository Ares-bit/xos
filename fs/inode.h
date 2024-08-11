#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "list.h"
#include "stdint.h"
#include "ide.h"

struct inode {
    uint32_t i_no;//i结点号
    uint32_t i_size;//文件大小
    uint32_t i_open_cnts;//文件被打开的次数
    bool write_deny;//写文件不能并行，进程写文件时检查此
    uint32_t i_sectors[13];//0-11直接块，12存一级间接指针
    struct list_elem inode_tag;//连接inode节点，链表在硬盘里也无用啊
    //怎么区分目录和文件？inode中加个标志不是更好？
};

struct inode* inode_open(struct partition* part, uint32_t inode_no);

#endif