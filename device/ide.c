#include "ide.h"
#include "stdio.h"
#include "stdio-kernel.h"
#include "stdint.h"
#include "global.h"
#include "io.h"

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
#define CMD_READ_SECTOR             0x20
#define CMD_WRITE_SECTOR            0x10

//定义可读写的最大LBA地址 调试专用
#define MAX_LBA                     (162 * 63 * 16 - 1)
//((80 * 1024 * 1024 / 512) - 1)  //只支持80M硬盘163,839 实际应为162*63*16-1=163295

uint8_t channel_cnt;//按硬盘数计算的通道数
struct ide_channel channels[2];//有两个ide通道

//选择读写的硬盘
static void select_disk(struct disk* hd)
{
    uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
    if (hd->dev_no) {
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

//等待30s
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

void ide_init()
{
    printk("ide_init start\n");
    uint8_t hd_cnt = *(uint8_t*)(0x475);//BIOS会把硬盘数量存放在这里
    ASSERT(hd_cnt > 0);
    channel_cnt = DIV_ROUND_UP(hd_cnt / 2);//根据硬盘数向上取整获得通道数

    struct ide_channel* channel;
    uint8_t channel_no = 0;
    //不存在只开channel1的情况吗？这种情况channel_no从0开始岂不是错了？
    //这种情况应该是有问题，只是现在能用就行
    while (channel_no < channel_cnt) {
        channel = &channel[channel_no];
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
        channel_no++;
    }
    printk("ide_init done\n");
}
