#ifndef __FS_INODE_H
#define __FS_INODE_H

struct inode {
    uint32_t i_no;//i结点号
    uint32_t i_size;//文件大小
    uint32_t i_open_cnts;//文件被打开的次数
    bool write_deny;//写文件不能并行，进程写文件时检查此
    uint32_t i_sectors[13];//0-11直接块，12存一级间接指针
    struct list_elem inode_tag;//连接inode节点
}

#endif