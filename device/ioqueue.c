#include "ioqueue.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "print.h"

void ioqueue_init(struct ioqueue* ioq)
{
    lock_init(&ioq->lock);
    ioq->producer = ioq->consumer = NULL;
    ioq->head = ioq->tail = 0;
}

static int32_t next_pos(int32_t pos)
{
    return (pos + 1) % bufsize;
}

//如果队列头的下一个位置就是队尾，表示队列满
//队列头的位置应该表示下一处可写的位置，队尾表示下一处可读位置
//还是说这个函数在刚写完，未挪动head前进行判断？
bool ioq_full(struct ioqueue* ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);
    return (next_pos(ioq->head) == ioq->tail);
}

bool ioq_empty(struct ioqueue* ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);
    return (ioq->head == ioq->tail);
}

static void ioq_wait(struct task_struct** waiter)
{
    ASSERT(*waiter == NULL && waiter != NULL);
    *waiter = running_thread();
    put_int(*waiter);
    thread_block(TASK_BLOCKED);
}

static void ioq_wakeup(struct task_struct** waiter)
{
    ASSERT(*waiter != NULL && *waiter == running_thread());
    thread_unblock(*waiter);
    *waiter = NULL;
}

char ioq_getchar(struct ioqueue* ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);
    while (ioq_empty(ioq)) {
        lock_acquire(&ioq->lock);
        ioq_wait(&ioq->consumer);//wait深层的schedule要在锁里面，所以前后加锁
        lock_release(&ioq->lock);
    }

    char byte = ioq->buf[ioq->tail];
    ioq->tail = next_pos(ioq->tail);

    //消费者拿完东西了要唤醒生产者
    if (ioq->producer != NULL) {
        ioq_wakeup(&ioq->producer);
    }

    return byte;
}

void ioq_putchar(struct ioqueue* ioq, char byte)
{   
    while (ioq_full(ioq)) {
        lock_acquire(&ioq->lock);
        ioq_wait(&ioq->producer);
        lock_release(&ioq->lock);
    }

    ioq->buf[ioq->head] = byte;
    ioq->head = next_pos(ioq->head);

    if (ioq->consumer != NULL) {
        ioq_wakeup(&ioq->consumer);
    }
}