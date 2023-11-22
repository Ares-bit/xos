#include "sync.h"
#include "stdint.h"
#include "list.h"
#include "interrupt.h"
#include "thread.h"
#include "debug.h"

void sema_init(struct semaphore* psema, uint8_t value)
{
    psema->value = value;
    list_init(&psema->waiters);
}

void lock_init(struct lock* plock)
{
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
   sema_init(&plock->semaphore, 1);  // 信号量初值为1
}

void sema_down(struct semaphore* psema)
{
    enum intr_status old_status = intr_disable();
   	while(psema->value == 0) {	// 若value为0,表示已经被别人持有
        ASSERT(!elem_find(&psema->waiters, &running_thread()->general_tag));
        if (elem_find(&psema->waiters, &running_thread()->general_tag)) {
            PANIC("sema_down: thread blocked has been in waiters_list\n");
        }
        //put_str("append to sema waitlist!\n");
        list_append(&psema->waiters, &running_thread()->general_tag);
        thread_block(TASK_BLOCKED);
    }
    //以上就是线程阻塞自己 被唤醒后直接执行下边这句获取锁的操作
    psema->value--;
   	ASSERT(psema->value == 0);	    
    intr_set_status(old_status);
}

void sema_up(struct semaphore* psema)
{
    enum intr_status old_status = intr_disable();
   	ASSERT(psema->value == 0);	    
    if (!list_empty(&psema->waiters)) {
        struct task_struct* thread_blocked = elem2entry(struct task_struct, general_tag, list_pop(&psema->waiters));
        thread_unblock(thread_blocked);
    }
    psema->value++;
	ASSERT(psema->value == 1);
    intr_set_status(old_status);
}

//lock只操作holder_repeat_nr sema才是实际对队列进行操作的
void lock_acquire(struct lock* plock)
{
    if (plock->holder != running_thread()) {
        sema_down(&plock->semaphore);
        plock->holder = running_thread();
        ASSERT(plock->holder_repeat_nr == 0);
        plock->holder_repeat_nr = 1;
    } else {
        //如果同一个线程重复获取锁 则不阻塞其自己 只是给获取次数加1
        plock->holder_repeat_nr++;
    }
}

void lock_release(struct lock* plock)
{
    ASSERT(plock->holder == running_thread());
    if (plock->holder_repeat_nr > 1) {
        plock->holder_repeat_nr--;
        return;
    }

    ASSERT(plock->holder_repeat_nr == 1);
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_up(&plock->semaphore);//sema_up要最后执行 因为释放锁不是原子的 如果先执行了sema_up被换下处理器
                                //新线程上来获取锁后将holder置为自己，如果再切换回老线程，又可能将holder置空
}