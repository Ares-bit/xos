#include "ide.h"
#include "stdio.h"
#include "stdio-kernel.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "sync.h"
#include "memory.h"
#include "debug.h"
#include "timer.h"
#include "string.h"

//定义硬盘寄存器端口号
#define reg_data(channel)           (channel->port_base + 0)
#define reg_error(channel)          (channel->port_base + 1)
#define reg_sect_cnt(channel)       (channel->port_base + 2)
#define reg_lba_l(channel)          (channel->port_base + 3)
#define reg_lba_m(channel)          (channel->port_base + 4)
#define reg_lba_h(channel)          (channel->port_base + 5)
#define reg_dev(channel)            (channel->port_base + 6)
#define reg_status(channel)         (channel->port_base + 7)
#define reg_cmd(channel)            reg_status(channel)
#define reg_alt_status(channel)     (channel->port_base + 0x206)
#define reg_ctl(channel)            reg_alt_status(channel)

//reg_alt_status寄存器相关位
#define BIT_ALT_STAT_BSY            0x80
#define BIT_ALT_STAT_DRDY           0x40
#define BIT_ALT_STAT_DRQ            0x8

//reg_dev寄存器相关位
#define BIT_DEV_MBS                 0xa0 //第7位 5位固定为1
#define BIT_DEV_LBA                 0x40
#define BIT_DEV_DEV                 0x10 //0为主盘 1为从盘

//定义硬盘操作指令
#define CMD_IDENTIFY                0xec //硬盘识别指令
#define CMD_READ_SECTOR             0x20 //读扇区
#define CMD_WRITE_SECTOR            0x30 //写扇区

//定义可读写的最大LBA地址 调试专用
#define MAX_LBA                     (162 * 63 * 16 - 1)
//((80 * 1024 * 1024 / 512) - 1)  //只支持80M硬盘163,839 实际应为162*63*16-1=163295

// uint8_t channel_cnt;//按硬盘数计算的通道数
// struct ide_channel channels[2];//有两个ide通道

int32_t ext_lba_base = 0;//用于记录总扩展分区的起始lba，初始为0，partition_scan时以此为标记
uint8_t p_no = 0, l_no = 0;//用于记录硬盘主分区和逻辑分区的下标
struct list partition_list;//分区队列

//分区表表项结构
struct partition_table_entry {
    uint8_t bootable;//此分区是否可引导
    uint8_t start_head;//起始磁头号
    uint8_t start_sec;//起始扇区号
    uint8_t start_chs;//起始柱面号
    uint8_t fs_type;//分区类型
    uint8_t end_head;//结束磁头号
    uint8_t end_sec;//结束扇区号
    uint8_t end_chs;//结束柱面号
    //下面两项要重点关注
    uint32_t start_lba;//本分区起始扇区的lba地址
    uint32_t sec_cnt;//本分区的扇区数目
} __attribute__ ((packed));//保证此结构是16字节大小

//引导扇区512B结构
struct boot_sector {
    uint8_t other[446];//引导代码
    struct partition_table_entry partition_table[4];//前面要求16对齐是因为这要精确512
    uint16_t signature;//引导扇区结束标志0x55 0xaa
} __attribute__ ((packed));

//选择读写的硬盘
static void select_disk(struct disk* hd)
{
    uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
    if (hd->dev_no == 1) {
        reg_device |= BIT_DEV_DEV;
    }
    //写reg dev寄存器高4位 reg dev的低4位没有看懂？
    outb(reg_dev(hd->my_channel), reg_device);
}

//向硬盘控制器写入起始扇区地址及要读写的扇区数，一次最多读写256扇区
static void select_sector(struct disk *hd, uint32_t lba, uint8_t sec_cnt)
{
    ASSERT(lba <= MAX_LBA);
    struct ide_channel *channel = hd->my_channel;

    //cnt = 0表示读写256个扇区 不是硬盘硬件的功能 得软件实现
    outb(reg_sect_cnt(channel), sec_cnt);

    //outb %b0,%wl自动用低8位
    outb(reg_lba_l(channel), lba);
    outb(reg_lba_m(channel), lba >> 8);
    outb(reg_lba_h(channel), lba >> 16);

    //lba第24-27位要写入device 0-3位
    outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA | \
        (hd->dev_no == 1 ? BIT_DEV_DEV : 0) | lba >> 24);
}

//向通道发命令
static void cmd_out(struct ide_channel* channel, uint8_t cmd)
{
    //只要想通道发了命令 就将此位置1
    channel->expecting_intr = true;
    outb(reg_cmd(channel), cmd);
}

//从硬盘读取sec_cnt到buf
static void read_from_sector(struct disk* hd, void* buf, uint8_t sec_cnt)
{
    uint32_t size_in_byte;
    if (sec_cnt == 0) {
        size_in_byte = 256 * 512;
    } else {
        size_in_byte = sec_cnt * 512;
    }
    insw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

//将buf中的sec_cnt数据写入硬盘
static void write2sector(struct disk* hd, void* buf, uint8_t sec_cnt)
{
    uint32_t size_in_byte;
    if (sec_cnt == 0) {
        size_in_byte = 256 * 512;
    } else {
        size_in_byte = sec_cnt * 512;
    }
    outsw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

//等待硬盘30s
static bool busy_wait(struct disk* hd)
{
    struct ide_channel* channel = hd->my_channel;
    uint16_t time_limit = 30 * 1000;
    //不能直接等30ms 中间还得获取硬盘状态 动作完成得随时出来
    while (time_limit -= 10 >= 0) {
        //读取alt status寄存器 如果是busy状态则继续休眠 否则返回drq位状态 以看数据是否准备好
        if (!(inb(reg_status(channel)) & BIT_ALT_STAT_BSY)) {
            return (inb(reg_status(channel)) & BIT_ALT_STAT_DRQ);
        } else {
            mtime_sleep(10);
        }
    }

    return false;
}

//从硬盘lba地址处读取扇区到buf
void ide_read(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt)
{
    ASSERT(lba <= MAX_LBA);
    //此sec_cnt为32位的，传到函数里会被截断成8位
    ASSERT(sec_cnt > 0);

    lock_acquire(&hd->my_channel->lock);
    //选择硬盘
    select_disk(hd);

    uint32_t secs_op;//每次操作的扇区数
    uint32_t secs_done = 0;//已完成的扇区数
    while(secs_done < sec_cnt) {
        if (secs_done + 256 <= sec_cnt) {
            //如果sec_cnt-256还大于等于secs_done，那就说明一次可以读最多的256
            //也可以这样理解：第一次如果sec_cnt-256>=0就表明可以一次读256了
            secs_op = 256;
        } else {
            secs_op = sec_cnt - secs_done;
        }

        //写入待读取的扇区数和起始地址 从上次读完的地址起始，读未读的这么多
        select_sector(hd, lba + secs_done, secs_op);

        //发出读取命令
        cmd_out(hd->my_channel, CMD_READ_SECTOR);
    
        //现在硬盘已经开始忙了，要将自己阻塞
        sema_down(&hd->my_channel->disk_done);

        //从上面阻塞醒来后检查硬盘状态是否可读
        if (!busy_wait(hd)) {
            //超过30s数据还没准备好就报异常
            char error[64];
            sprintf(error, "%s read sector %d failed!!!!!!\n", hd->name, lba);
            PANIC(error);
        }

        //将数据从缓冲区中读出
        //buf地址从上次写完的地址接着写，应该是考虑有大于256的情况，这种情况才会分多次读取
        read_from_sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);
        secs_done += secs_op;
    }

    lock_release(&hd->my_channel->lock);
}

//将buf中的数据写入硬盘
void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt)
{
    ASSERT(lba <= MAX_LBA);
    ASSERT(sec_cnt > 0);

    lock_acquire(&hd->my_channel->lock);
    select_disk(hd);
    uint32_t secs_op;
    uint32_t secs_done;
    while(secs_done < sec_cnt) {
        if (secs_done + 256 <= sec_cnt) {
            secs_op = 256;
        } else {
            secs_op = sec_cnt - secs_done;
        }

        select_sector(hd, lba + secs_done, secs_op);

        cmd_out(hd->my_channel, CMD_WRITE_SECTOR);

        if (!busy_wait(hd)) {
            char error[64];
            sprintf(error, "%s write sector %d failed!!!!!!\n", hd->name, lba);
            PANIC(error);           
        }

        write2sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);

        //写完硬盘应会产生中断，这与读不同，读取应该是数据准备好了会产生中断
        sema_down(&hd->my_channel->disk_done);

        secs_done += secs_op;
    }

    lock_release(&hd->my_channel->lock);
}

//将dst中len个相邻字节交换位置后存入buf
//需要交换前后两字节的原因是：只有一种解释，那就是这个字符串在硬盘中本身存储的顺序就是反的，否则无法解释指令和数据读出来不用翻转直接就能用的现象
static void swap_pairs_bytes(const char* dst, char* buf, uint32_t len)
{
    uint8_t idx;
    for (idx = 0; idx < len; idx += 2) {
        buf[idx + 1] = *dst++;//dst0写到buf[1]
        buf[idx] = *dst++;//dst[1]写到bug[0]
    }
    buf[idx] = '\0';
}

//获取硬盘参数
static void identify_disk(struct disk* hd)
{
    char id_info[512];
    select_disk(hd);
    cmd_out(hd->my_channel, CMD_IDENTIFY);//两个硬盘连到同一个通道，共享一个中断
    //先睡会，一会让中断叫醒我
    sema_down(&hd->my_channel->disk_done);
    //等待数据准备好
    if (!busy_wait(hd)) {
        char error[64];
        sprintf(error, "%s identify failed!!!!!!\n", hd->name);
        PANIC(error);
    }
    //甚至不需要指定起始地址
    read_from_sector(hd, id_info, 1);

    char buf[64];//正好存储所需的64B信息
    //硬盘序列号位于10字偏移量，长20，硬盘型号位于27字偏移量处，长40
    uint8_t sn_start = 10 * 2, sn_len = 20, md_start = 27 * 2, md_len = 40;
    swap_pairs_bytes(&id_info[sn_start], buf, sn_len);
    printk("disk %s info:\n SN:%s\n", hd->name, buf);
    memset(buf, 0, sizeof(buf));
    swap_pairs_bytes(&id_info[md_start], buf, md_len);
    printk(" MODULE:%s\n", buf);
    uint32_t sectors = *(uint32_t*)&id_info[60 * 2];//把最后4字节用32位整型读出来
    printk(" SECTORS:%d\n", sectors);
    printk(" CAPACITY:%dMB\n", sectors * 512 / 1024 / 1024);
}

//扫描硬盘中地址为ext_lba的扇区中的所有分区表
static void partition_scan(struct disk* hd, uint32_t ext_lba)
{
    struct boot_sector* bs = sys_malloc(sizeof(struct boot_sector));//如果使用局部变量则会爆栈
    ide_read(hd, ext_lba, bs, 1);
    uint8_t part_idx = 0;
    //获取扇区中分区表指针
    struct partition_table_entry* p = bs->partition_table;

    while (part_idx++ < 4) {
        //对于扩展分区的处理，如果是0x5就表示遇到了下一个扩展分区，要递归处理
        if (p->fs_type == 0x5) {//0x5表示下一个区域，所以要递归，真正处理下一个区域是在else里面写的
            if (ext_lba_base != 0) {
                //子扩展分区start lba是相对于主引导扇区中的总扩展分区地址
                partition_scan(hd, p->start_lba + ext_lba_base);
            } else {
                //ext_lba_base=0表示第一次读取引导块，也就是主引导分区表
                //记录主扩展分区的起始地址 后面所有子扩展分区都要使用这个作为基地址
                ext_lba_base = p->start_lba;
                partition_scan(hd, p->start_lba);
            }
        } else if (p->fs_type != 0) {//0x83表示主分区 0x66表示当前扩展分区 0x05表示下一个扩展分区 0为无效分区(第2、3项是全0的)
            //对于主分区的处理
            if (ext_lba == 0) {//如果输入起始lba地址为0则表示当前处理的是主引导分区表
                //读取MBR中的主分区表初始化hd主分区的属性
                hd->prim_parts[p_no].start_lba = ext_lba + p->start_lba;
                hd->prim_parts[p_no].sec_cnt = p->sec_cnt;
                hd->prim_parts[p_no].my_disk = hd;
                list_append(&partition_list, &hd->prim_parts[p_no].part_tag);
                //把sda和分区号拼起来形成分区全名
                sprintf(hd->prim_parts[p_no].name, "%s%d", hd->name, p_no + 1);
                p_no++;
                ASSERT(p_no < 4);
            } else {//如果不等于0则表示当前处理的是EBR分区表
                //读取当前子扩展分区表初始化hd扩展分区的属性
                hd->logic_parts[l_no].start_lba = ext_lba + p->start_lba;
                hd->logic_parts[l_no].sec_cnt = p->sec_cnt;
                hd->logic_parts[l_no].my_disk = hd;
                list_append(&partition_list, &hd->logic_parts[l_no].part_tag);
                sprintf(hd->logic_parts[l_no].name, "%s%d", hd->name, l_no + 5);
                l_no++;
                if (l_no >= 8) {
                    return;
                }
            }
        }
        p++;
    }
    sys_free(bs);
}

//打印分区队列上节点的起始lba和扇区数
//因为list_traversal里的回调需要两个参数，但partition_info只需一个，用unused修饰表示未使用
static bool partition_info(struct list_elem* pelem, int arg UNUSED)
{
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    printk(" %s start_lba:0x%x, sec_cnt:0x%x\n", part->name, part->start_lba, part->sec_cnt);
    return false;
}

//硬盘中断处理程序
static void intr_hd_handler(uint8_t irq_no)
{
    //0x20+e为ide0通道的中断号，0x20+f为ide1通道的中断号
    ASSERT(irq_no == 0x2e || irq_no == 0x2f);
    //根据中断号获取通道数组下标来确定通道号
    uint8_t ch_no = irq_no - 0x2e;
    struct ide_channel* channel = &channels[ch_no];
    ASSERT(channel->irq_no == irq_no);
    //由于加锁保护的原因，expecting intr一定对应本次中断
    if (channel->expecting_intr) {
        channel->expecting_intr = false;
        sema_up(&channel->disk_done);
        //读取状态寄存器硬盘即认为此次中断被处理 从而可以执行新的读写
        inb(reg_status(channel));
    }
}

void ide_init(void)
{
    printk("ide_init start\n");
    uint8_t hd_cnt = *(uint8_t*)(0x475);//BIOS会把硬盘数量存放在这里
    ASSERT(hd_cnt > 0);
    channel_cnt = DIV_ROUND_UP(hd_cnt, 2);//根据硬盘数向上取整获得通道数
    struct ide_channel* channel;
    uint8_t channel_no = 0, dev_no = 0;

    list_init(&partition_list);
    //不存在只开channel1的情况吗？这种情况channel_no从0开始岂不是错了？
    //这种情况应该是有问题，只是现在能用就行
    while (channel_no < channel_cnt) {
        channel = &channels[channel_no];
        sprintf(channel->name, "ide%d", channel_no);
        switch (channel_no) {
            case 0:
                channel->port_base = 0x1f0;//通道0从0x1f0开始，通道1从0x170开始
                channel->irq_no = 0x20 + 14;//ide0中断号
                break;
            case 1:
                channel->port_base = 0x170;
                channel->irq_no = 0x20 + 15;
                break;
        }
        channel->expecting_intr = false;
        lock_init(&channel->lock);
        //初始化0是为了驱动第一次请求数据时就要陷入等待
        sema_init(&channel->disk_done, 0);
        register_handler(channel->irq_no, intr_hd_handler);

        //初始化disk
        while (dev_no < 2) {
            struct disk* hd = &channel->devices[dev_no];
            hd->my_channel = channel;
            hd->dev_no = dev_no;
            sprintf(hd->name, "sd%c", 'a' + channel_no * 2 + dev_no);
            identify_disk(hd);
            //dev_no=0为hd60M.img，存储内核的裸硬盘，不处理
            if (dev_no != 0) {
                partition_scan(hd, 0);
            }
            p_no = 0, l_no = 0;
            dev_no++;
        }
        dev_no = 0;
        channel_no++;
    }
    printk("all partition info:\n");
    list_traversal(&partition_list, partition_info, (int)NULL);
    printk("ide_init done\n");
}
