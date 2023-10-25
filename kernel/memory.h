#ifndef _KERNEL_MEMORY_H
#define _KERNEL_MEMORY_H
#include "stdint.h"
#include "bitmap.h"
//虚拟地址池
struct virtual_addr {
    struct bitmap vaddr_bitmap;//虚拟内存位图
    uint32_t vaddr_start;//起始虚拟地址
    //虚拟池没有pool_size 因为相对于物理池32MB来说其大小是无限的 故不需要pool_size
};

//申请内存时得判断从哪个池子分配
enum pool_flags {
    PF_KERNEL = 1,
    PF_USER = 2
};

//页表属性 分配页时修改页表页目录属性使用
#define PG_P_1 1
#define PG_P_0 0
#define PG_RW_R 0
#define PG_RW_W 2
#define PG_US_S 0
#define PG_US_U 4

extern struct pool kernel_pool, user_pool;

void mem_init(void);

void* get_kernel_pages(uint32_t pg_cnt);

#endif