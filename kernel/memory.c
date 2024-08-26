#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "debug.h"
#include "string.h"
#include "global.h"
#include "thread.h"
#include "sync.h"
#include "interrupt.h"

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

struct arena {
    struct mem_block_desc *desc;
    uint32_t cnt;
    bool large;//large == true时cnt表示页框数，否则表示空闲mem_block数
};

struct mem_block_desc k_block_descs[DESC_CNT];

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
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
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
            //*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
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
   lock_acquire(&kernel_pool.lock);

    void* vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL) {
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    lock_release(&kernel_pool.lock);

    return vaddr;
}

void* get_user_pages(uint32_t pg_cnt)
{
    lock_acquire(&user_pool.lock);
    void* vaddr = malloc_page(PF_USER, pg_cnt);
    if (vaddr != NULL) {
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    lock_release(&user_pool.lock);
    return vaddr;
}

//此函数为申请物理页面，并置位进程虚拟地址位图
//get_a_page只分配一页，没搞明白为什么要弄这么多重复功能的内存分配函数呢？
void* get_a_page(enum pool_flags pf, uint32_t vaddr)
{
    //这是物理地址池，所以所有线程共用一个全局的
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

    lock_acquire(&mem_pool->lock);
    struct task_struct* cur = running_thread();
    int32_t bit_idx = -1;

    if (cur->pgdir != NULL && pf == PF_USER) {
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
    } else if (cur->pgdir == NULL && pf == PF_KERNEL) {
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    } else {
        PANIC("get a page:not allow kernel alloc userspace or user alloc kernelspace by get_a_page");
    }

    void* page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL) {
        lock_release(&mem_pool->lock);
        return NULL;
    }
    page_table_add((void*)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);

    return (void*)vaddr;
}

//虚拟地址映射到物理地址：找到虚拟地址所在页表，找到对应的页表项，其中存储的就是实际物理地址
uint32_t addr_v2p(uint32_t vaddr)
{
    uint32_t* pte = pte_ptr(vaddr);
    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
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
    lock_init(&kernel_pool.lock);

    //user pool初始化
    user_pool.phy_addr_start = up_start;
    user_pool.pool_size = user_free_pages * PG_SIZE;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;
    user_pool.pool_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length);
    lock_init(&user_pool.lock);

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

//初始化7种描述符
void block_desc_init(struct mem_block_desc* desc_array)
{
    uint16_t desc_idx, block_size = 16;
    for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
        desc_array[desc_idx].block_size = block_size;
        desc_array[desc_idx].blocks_per_arena = (PG_SIZE - sizeof(struct arena)) / block_size;
        list_init(&desc_array[desc_idx].free_list);
        block_size *= 2;
    }
}

//返回arena中第idx个内存块地址
static struct mem_block* arena2block(struct arena* a, uint32_t idx)
{
    //到时候会把这块内存地址解释为mem_block类型的地址？mem block有两个元素啊。。。
    return (struct mem_block*)((uint32_t)a + sizeof(struct arena) + idx * a->desc->block_size);
}

//返回内存块b所在的arena地址
static struct arena* block2arena(struct mem_block* b)
{
    //block所在内存页的首地址，如果arena是多页怎么办？
    return (struct arena*)((uint32_t)b & 0xfffff000);
}

void* sys_malloc(uint32_t size)
{
    enum pool_flags PF;
    struct pool* mem_pool;
    uint32_t pool_size;
    struct mem_block_desc* descs;
    struct task_struct* cur_thread = running_thread();

    if (cur_thread->pgdir == NULL) {
        PF = PF_KERNEL;
        pool_size = kernel_pool.pool_size;
        mem_pool = &kernel_pool;
        descs = k_block_descs;
    } else {
        PF = PF_USER;
        pool_size = user_pool.pool_size;
        mem_pool = &user_pool;//用户物理内存，与内核物理内存一起组成全部可用内存
        descs = cur_thread->u_block_descs;        
    }

    if (!(size > 0 && size < pool_size)) {
        return NULL;
    }

    struct arena* a;
    struct mem_block* b;
    lock_acquire(&mem_pool->lock);

    if (size > 1024) {
        //创建第一个arena时要带上12B的头
        uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE);
        a = malloc_page(PF, page_cnt);
        if (a != NULL) {
            memset(a, 0, page_cnt * PG_SIZE);
            //size>1024算大内存，不需要有描述符
            a->desc = NULL;
            a->cnt = page_cnt;
            a->large = true;
            lock_release(&mem_pool->lock);
            return (void*)(a+1);//跨过当前arena，返回剩下的内存
        } else {
            lock_release(&mem_pool->lock);
            return NULL;
        }
    } else {
        uint8_t desc_idx;
        //找到大小合适的描述符
        for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
            if (size <= descs[desc_idx].block_size) {
                break;
            }
        }

        //如果描述符上的free list没有可分配的mem_block，就创建新arena
        if (list_empty(&descs[desc_idx].free_list)) {
            a = malloc_page(PF, 1);
            if (a == NULL) {
                lock_release(&mem_pool->lock);
                return NULL;
            }
            memset(a, 0, PG_SIZE);
            a->desc = &descs[desc_idx];
            a->large = false;
            a->cnt = descs[desc_idx].blocks_per_arena;

            uint32_t block_idx;
            //为什么要关中断？
            enum intr_status old_status = intr_disable();
            //将arena拆成块，添加到free list中
            for (block_idx = 0; block_idx < descs[desc_idx].blocks_per_arena; block_idx++) {
                b = arena2block(a, block_idx);
                ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
                list_append(&a->desc->free_list, &b->free_elem);
            }
            intr_set_status(old_status);
        }

        //开始分配内存块
        b = elem2entry(struct mem_block, free_elem, list_pop(&(descs[desc_idx].free_list)));
        memset(b, 0, descs[desc_idx].block_size);
        a = block2arena(b);
        a->cnt--;
        lock_release(&mem_pool->lock);
        return (void*)b;
    }
}

//将物理地址回收到物理内存池
void pfree(uint32_t pg_phy_addr)
{
    struct pool* mem_pool;
    uint32_t bit_idx = 0;
    if (pg_phy_addr >= user_pool.phy_addr_start) {
        mem_pool = &user_pool;
        bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE;
    } else {
        mem_pool = &kernel_pool;
        bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE;
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0);
}

//解除页表中的映射
static void page_table_pte_remove(uint32_t vaddr)
{
    uint32_t* pte = pte_ptr(vaddr);
    *pte &= ~PG_P_1;
    //刷TLB
    asm volatile("invlpg %0" : : "m"(vaddr) : "memory");
}

static void vaddr_remove(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt)
{
    uint32_t bit_idx_start = 0, vaddr = (uint32_t)_vaddr, cnt = 0;

    if (pf == PF_KERNEL) {
        bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt) {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
        }
    } else {
        struct task_struct* cur_thread = running_thread();
        bit_idx_start = (vaddr - cur_thread->userprog_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt) {
            bitmap_set(&cur_thread->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
        }
    }
}

void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt)
{
    uint32_t pg_phy_addr;
    uint32_t vaddr = (int32_t)_vaddr, page_cnt = 0;
    ASSERT(pg_cnt >=1 && vaddr % PG_SIZE == 0);
    //获取虚拟地址对应的物理地址
    pg_phy_addr = addr_v2p(vaddr);
    //确保释放的物理内存在低端1M+1KB页目录+1KB页表范围外
    ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000);
    if (pg_phy_addr >= user_pool.phy_addr_start) {
        vaddr -= PG_SIZE;
        //把vaddr起始连续pg_cnt个虚拟页都处理了
        while (page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= user_pool.phy_addr_start);
            pfree(pg_phy_addr);
            page_table_pte_remove(vaddr);
            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);
    } else {
        vaddr -= PG_SIZE;
        //把vaddr起始连续pg_cnt个虚拟页都处理了
        while (page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr < user_pool.phy_addr_start \
                                                && pg_phy_addr >= kernel_pool.phy_addr_start);
            pfree(pg_phy_addr);
            page_table_pte_remove(vaddr);
            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);       
    }
}

void sys_free(void *ptr)
{
    ASSERT(ptr != NULL);
    if (ptr != NULL) {
        enum pool_flags PF;
        struct pool* mem_pool;

        if (running_thread()->pgdir == NULL) {
            ASSERT((uint32_t)ptr >= K_HEAP_START);
            PF = PF_KERNEL;
            mem_pool = &kernel_pool;
        } else {
            PF = PF_USER;
            mem_pool = &user_pool;
        }

        lock_acquire(&mem_pool->lock);
        struct mem_block* b = ptr;
        struct arena* a = block2arena(b);

        ASSERT(a->large == 0 || a->large == 1);
        if (a->desc == NULL && a->large == true) {
            //如果里面只用了一块，要把整个arena都还了？
            mfree_page(PF, a, a->cnt);
        } else {
            //将小块内存回收到描述符里
            list_append(&a->desc->free_list, &b->free_elem);
            //如果还回这块时arena已经没人用了就直接释放，这样效率岂不是很低？反复创建arena
            if (++a->cnt == a->desc->blocks_per_arena) {
                uint32_t block_idx;
                for (block_idx = 0; block_idx < a->desc->blocks_per_arena; block_idx++) {
                    struct mem_block* b = arena2block(a, block_idx);
                    ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
                    //将所有block都摘下来，只需要操作前后指针即可，不用管在哪个链表上，其实就是free list
                    list_remove(&b->free_elem);
                }
                mfree_page(PF, a, 1);
            }
        }
        lock_release(&mem_pool->lock);
    }
}

void mem_init() {
    put_str("mem_init start\n");
    uint32_t mem_bytes_total = *(uint32_t*)(0xb00);//之前boot存在这个地址里的
    mem_pool_init(mem_bytes_total);
    block_desc_init(k_block_descs);
    put_str("mem_init done\n");
}