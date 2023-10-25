#ifndef _KERNEL_IO_H
#define _KERNEL_IO_H
#include "stdint.h"

//向端口中写入1B
static inline void outb(uint16_t port, uint8_t data)
{
    asm volatile("outb %b0, %w1" : : "a"(data), "Nd"(port));//N约束表示立即数
}

//向端口中连续写入若干word
static inline void outsw(uint16_t port, const void* addr, uint32_t word_cnt)
{
    asm volatile("cld; rep outsw" : "+S"(addr), "+c"(word_cnt) : "d"(port));
}

//从端口中读出1B
static inline uint8_t inb(uint16_t port)
{
    uint8_t data;
    asm volatile("inb %w1, %b0" : "=a"(data) : "Nd"(port));
    return data;
}

//从端口中连续读出若干word
static inline void insw(uint16_t port, void* addr, uint32_t word_cnt)//这里addr要变化 故不能用const修饰
{
    asm volatile("cld; rep insw" : "+D"(addr), "+c"(word_cnt): "d"(port) : "memory");
}

#endif