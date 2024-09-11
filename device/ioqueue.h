#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H
#include "stdint.h"
#include "sync.h"
#include "thread.h"

//从64改为2048，为了管道的大小
#define bufsize 2048

struct ioqueue {
    struct lock lock;
    //记录在此队列上睡眠的生产者
    struct task_struct* producer;
    //记录在此队列上睡眠的消费者
    struct task_struct* consumer;
    char buf[bufsize];
    int32_t head;//队首指针 数组下标
    int32_t tail;//队尾指针 数组下标
};

void ioqueue_init(struct ioqueue* ioq);
bool ioq_full(struct ioqueue* ioq);
bool ioq_empty(struct ioqueue* ioq);
char ioq_getchar(struct ioqueue* ioq);
void ioq_putchar(struct ioqueue* ioq, char byte);
uint32_t ioq_length(struct ioqueue* ioq);
#endif