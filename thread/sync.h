#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H
#include "stdint.h"
#include "list.h"

struct semaphore {
    uint8_t value;
    struct list waiters;
};

struct lock {
    struct task_struct* holder;//锁持有者
    struct semaphore semaphore;
    uint32_t holder_repeat_nr;//锁持有者重复申请锁的次数
};

#endif