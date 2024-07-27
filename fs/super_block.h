#ifndef __FS_SUPER_BLOCK_H
#define __FS_SUPER_BLOCK_h

struct super_block {
    uint32_t magic;//指明文件系统类型

    uint32_t sec_cnt;//本分区总共有多少扇区
    uint32_t inode_cnt;//本分区inode数

    uint32_t part_lba_base;//本分区的起始Lba地址，lba只有绝对地址

    uint32_t block_bitmap_lba;//块位图起始地址+大小
    uint32_t block_bitmap_sects;

    uint32_t inode_bitmap_lba;//inode位图起始地址+大小
    uint32_t inode_bitmap_sects;

    uint32_t inode_table_lba;//inode数组起始地址+大小
    uint32_t inode_table_sects;

    uint32_t data_start_lba;//数据区开始的第一个扇区号

    uint32_t root_inode_no;//根目录inode结点号
    uint32_t dir_entry_size;//目录项大小

    uint8_t pad[460];//加460字节凑够512B一个扇区，磁盘最少要写512B，如果不够那就取到外边的数据写进去了
} __attribute__ ((packed));//确保编译后结构体大小为512B

#endif