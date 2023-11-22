#include "list.h"
#include "interrupt.h"

void list_init(struct list* list)
{
    list->head.prev = NULL;
    list->head.next = &list->tail;
    list->tail.prev = &list->head;
    list->tail.next = NULL;
}

//elem插到before之前
void list_insert_before(struct list_elem* before, struct list_elem* elem)
{
    //操作时要先获取原中断状态
    enum intr_status old_status = intr_disable();

/* 将before前驱元素的后继元素更新为elem, 暂时使before脱离链表*/ 
   before->prev->next = elem; 

/* 更新elem自己的前驱结点为before的前驱,
 * 更新elem自己的后继结点为before, 于是before又回到链表 */
   elem->prev = before->prev;
   elem->next = before;

/* 更新before的前驱结点为elem */
   before->prev = elem;

   intr_set_status(old_status);
}

//将元素加到队列首
void list_push(struct list* plist, struct list_elem* elem)
{
    list_insert_before(plist->head.next, elem);
}

//将元素追加到队列末尾
void list_append(struct list* plist, struct list_elem* elem)
{
    list_insert_before(&plist->tail, elem);
}

//从链表上摘下来节点，这就不用考虑在哪个前在哪个后了
void list_remove(struct list_elem* pelem)
{
    enum intr_status old_status = intr_disable();

    pelem->prev->next = pelem->next;
	pelem->next->prev = pelem->prev;

    intr_set_status(old_status);
}

//将元素从队首pop出来
struct list_elem* list_pop(struct list* plist)
{
    struct list_elem* elem = plist->head.next;

    list_remove(elem);
    return elem;
}

bool elem_find(struct list* plist, struct list_elem* obj_elem)
{
    struct list_elem* elem = plist->head.next;
    while (elem != &plist->tail) {
        if (elem == obj_elem) {
            return true;
        }
        elem = elem->next;
    }
    return false;
}

//func判断链表中元素是否符合arg条件的，符合则返回
struct list_elem* list_traversal(struct list* plist, function func, int arg)
{
    struct list_elem* elem = plist->head.next;

    if (list_empty(plist)) {
        return NULL;
    }

    while (elem != &plist->tail) {
        if (func(elem, arg)) {
            return elem;
        }
        elem = elem->next;
    }
    return NULL;
}

uint32_t list_len(struct list* plist)
{
    struct list_elem* elem = plist->head.next;
    uint32_t length = 0;
    while (elem != &plist->tail) {
        length++;
        elem = elem->next;
    }
    return length;
}

bool list_empty(struct list* plist)
{
    return (plist->head.next == &plist->tail ? true : false);
}