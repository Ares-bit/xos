#include "ide.h"
#include "stdio.h"
#include "stdio-kernel.h"
#include "stdint.h"
#include "global.h"

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
#define max_lba                     (162 * 63 * 16 - 1)
//((80 * 1024 * 1024 / 512) - 1)  //只支持80M硬盘163,839 实际应为162*63*16-1=163295

uint8_t channel_cnt;//按硬盘数计算的通道数
struct ide_channel channels[2];//有两个ide通道

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
