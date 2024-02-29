#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "debug.h"
#include "string.h"
#include "global.h"
#include "thread.h"

#define PG_SIZE 4096

//虚拟bitmap基地址
#define MEM_BITMAP_BASE 0xc009a000

//跨过低端1MB内核就是堆起始地址
#define K_HEAP_START 0xc0100000

//取出地址中PDE索引和PTE索引
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

//物理内存池
struct pool {
    struct bitmap pool_bitmap;//物理内存位图
    uint32_t phy_addr_start;//内存池管理的物理内存起始地址
    uint32_t pool_size;//内存池字节容量
    struct lock lock;
};

struct pool kernel_pool, user_pool;

struct virtual_addr kernel_vaddr;//内核虚拟内存池

//在pf池中申请pg_cnt个虚拟页
static void* vaddr_get(enum pool_flags pf, uint32_t pg_cnt)
{
    int vaddr_start = 0, bit_idx_start = -1;
    uint32_t cnt = 0;
    if (pf == PF_KERNEL) {
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) {
            return NULL;
        }
        while (cnt < pg_cnt) {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
    } else {
        struct task_struct* cur = running_thread();
        //在用户内存池中搜索连续个页
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr, pg_cnt);
        if (bit_idx_start == -1) {
            return NULL;
        }

        while (cnt < pg_cnt) {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
        //start_process已经在0xc0000000-PG_SIZE处给用户分配了3级栈
        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
    }
    return (void*)vaddr_start;
}

//得到虚拟地址所在的pte的虚拟地址 pte是页表项！
uint32_t* pte_ptr(uint32_t vaddr)
{
    //本质上是将页目录当成物理页去访问 返回指向其中pte物理地址的指针
    //高10位全1找到页目录最后一项把页目录当页表
    //中间10位为原地址的高10位 定位到了页表 将页表当做物理页
    //最后12位找到物理页中偏移量即pte物理地址
    uint32_t* pte = (uint32_t*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);
    return pte;
}

//得到虚拟地址所在的pde的虚拟地址 pde是页目录表项！
uint32_t* pde_ptr(uint32_t vaddr)
{
    uint32_t* pde = (uint32_t*)(0xfffff000 + PDE_IDX(vaddr) * 4);
    return pde;
}

//分配一个物理页
static void* palloc(struct pool* m_pool)
{
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
    if (bit_idx == -1) {
        return NULL;
    }
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);
    uint32_t page_phyaddr = (m_pool->phy_addr_start + bit_idx * PG_SIZE);
    return (void*)page_phyaddr;
}

//向页表中添加虚拟地址与物理地址的映射
static void page_table_add(void* _vaddr, void* _page_phyaddr)
{
    uint32_t vaddr = (uint32_t)_vaddr;
    uint32_t page_phyaddr = (uint32_t)_page_phyaddr;
    uint32_t* pde = pde_ptr(vaddr);
    uint32_t* pte = pte_ptr(vaddr);

    //先判断*pde是否存在 存在才可以访问*pte
    if (*pde & 0x00000001) {
        //如果*pte不存在则报错
        ASSERT(!(*pte & 0x00000001));//表明该虚拟地址已经分配了物理页
        if (!(*pte & 0x00000001)) {//表明该虚拟地址未分配物理页
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        } else {
            PANIC("pte repeat");
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
    } else {
        //如果此虚拟地址的页目录项不存在则为其分配页表
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);
        *pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        //取*pte中物理地址 低12位清0即可
        memset((void*)((int)pte & 0xfffff000), 0, PG_SIZE);
        ASSERT(!(*pte & 0x00000001));
        //将传入物理地址填入对应页表项建立映射
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
    }
}

//分配cnt个页空间
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt)
{
    ASSERT(pg_cnt > 0 && pg_cnt < 3840);//每个pool 15MB = 3840 pages
    //申请虚拟页
    void* vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL) {
        return NULL;
    }

    uint32_t vaddr = (uint32_t)vaddr_start;
    uint32_t cnt = pg_cnt;
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    //申请物理页
    while (cnt-- > 0) {
        void* page_phyaddr = palloc(mem_pool);
        if (page_phyaddr == NULL) {
            return NULL;
        }
        //建立映射
        page_table_add((void*)vaddr, page_phyaddr);
        vaddr += PG_SIZE;
    }
    return vaddr_start;
}

//分配全0内核页
void* get_kernel_pages(uint32_t pg_cnt)
{
    void* vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL) {
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    return vaddr;
}

static void mem_pool_init(uint32_t all_mem) {
    put_str("mem_pool_init start\n");
    //统计物理内存
    //1页页目录表+255页内核页表
    uint32_t page_table_size = PG_SIZE * 256;//也就是说页表并不在低端1MB中
    uint32_t used_mem = page_table_size + 0x100000;//加1MB内核
    uint32_t free_mem = all_mem - used_mem;
    uint16_t all_free_pages = free_mem / PG_SIZE;//32位除法 商16位 用uint16_t存还剩多少页
    uint16_t kernel_free_pages = all_free_pages / 2;
    uint16_t user_free_pages = all_free_pages - kernel_free_pages;//剩的不一定整数个页
    //kernel bitmap长度
    uint32_t kbm_length = kernel_free_pages / 8;//余数不处理会丢最后几页内存 但是现在并不会 因为剩下整30MB kernel 15MB user 15MB
    //user bitmap长度
    uint32_t ubm_length = user_free_pages / 8;//余数不处理会丢最后几页内存
    //kernel pool起始
    uint32_t kp_start = used_mem;
    //user pool起始
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE;

    //kernel pool初始化
    kernel_pool.phy_addr_start = kp_start;
    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
    kernel_pool.pool_bitmap.bits = (void*)MEM_BITMAP_BASE;

    //user pool初始化
    user_pool.phy_addr_start = up_start;
    user_pool.pool_size = user_free_pages * PG_SIZE;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;
    user_pool.pool_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length);

    put_str("kernel_pool_bitmap_start:0x");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str(" kernel_pool_phy_addr_start:0x");
    put_int(kernel_pool.phy_addr_start);
    put_char('\n');
    put_str("user_pool_bitmap_start:0x");
    put_int((int)user_pool.pool_bitmap.bits);
    put_str(" user_pool_phy_addr_start:0x");
    put_int(user_pool.phy_addr_start);
    put_char('\n');
    
    //初始化物理bitmap
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    //初始化内核虚拟bitmap
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;//虚拟bitmap跟物理bitmap大小一致但不必一一对应
    //虚拟bitmap起始地址在user bitmap之后
    kernel_vaddr.vaddr_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    kernel_vaddr.vaddr_start = K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);

    put_str("mem_init done\n");
}

void mem_init() {
    put_str("mem_init start\n");
    uint32_t mem_bytes_total = *(uint32_t*)(0xb00);
    mem_pool_init(mem_bytes_total);
    put_str("mem_init done\n");
}