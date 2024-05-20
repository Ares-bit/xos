#include "io.h"
#include "print.h"
#include "timer.h"
#include "stdint.h"
#include "thread.h"
#include "debug.h"
#include "interrupt.h"

#define IRQ0_FREQUENCY      100
#define INPUT_FREQUENCY     1193180
#define COUNTER0_VALUE      INPUT_FREQUENCY / IRQ0_FREQUENCY
#define COUNTER0_PORT       0x40
#define COUNTER0_NO         0
#define COUNTER0_MODE       2
#define READ_WRITE_LATCH    3//先写计数初值低8位 后写高8位
#define PIT_CONTROL_PORT    0x43
#define MIL_SECONDS_PER_INTR    (1000 / IRQ0_FREQUENCY)//每多少毫秒发生一次时钟中断

uint32_t ticks;//内核自中断开启以来总共的滴答数

static void frequency_set(uint8_t counter_port, uint8_t counter_no, uint8_t rwl,
                            uint8_t counter_mode, uint16_t counter_value)
{
    //先写0x43控制字
    outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
    //再分两次写计数初值
    outb(counter_port, (uint8_t)counter_value);
    outb(counter_port, (uint8_t)counter_value >> 8);
}

//时钟中断处理函数
static void intr_timer_handler(void)
{
    struct task_struct* cur_thread = (struct task_struct*)running_thread();
    //检查栈溢出
    ASSERT(cur_thread->stack_magic == 0x20001212);

    cur_thread->elapsed_ticks++;
    ticks++;
    if (cur_thread->ticks == 0) {
        schedule();
    } else {
        cur_thread->ticks--;
    }
}

//以ticks为单位的sleep
static void ticks_to_sleep(uint32_t sleep_ticks)
{
    uint32_t start_ticks = ticks;

    //在sleep_ticks时间内一直让出CPU 达到sleep效果
    while (ticks - start_ticks < sleep_ticks) {
        thread_yield();
    }
}

//毫秒为单位的sleep 思路为通过时钟中断将ms与ticks连接起来
void mtime_sleep(uint32_t m_seconds)
{
    //通过计算m_seconds中发生了多少次时钟中断得到要休眠的ticks数（一次时钟中断加一个ticks）
    uint32_t sleep_ticks = DIV_ROUND_UP(m_seconds, MIL_SECONDS_PER_INTR);
    //故最少休眠10ms 比较粗糙
    ASSERT(sleep_ticks > 0);
    ticks_to_sleep(sleep_ticks);
}

void timer_init() {
    put_str("timer_init start\n");
    frequency_set(COUNTER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER0_MODE, COUNTER0_VALUE);
    register_handler(0x20, intr_timer_handler);
    put_str("timer_init done\n");
}