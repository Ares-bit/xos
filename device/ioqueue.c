#include "ioqueue.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"

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

//如果队列头跑到最右边，队尾还在左边，表示队列满
//队列头的位置应该表示下一处可写的位置，这样是否浪费最后一个单元
//还是说这个函数在刚写完，未挪动head前进行判断？
bool ioq_full(struct ioqueue* ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);
    return (next_pos(ioq->head) == ioq->tail);
}