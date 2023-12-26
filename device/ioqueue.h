#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H
#include "stdint.h"
#include "sync.h"
#include "thread.h"

#define bufsize 64

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

#endif