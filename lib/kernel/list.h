#ifndef _LIB_KERNEL_LIST_H
#define _LIB_KERNEL_LIST_H
#include "global.h"

#define offset(struct_type, member) (int)(&((struct_type*)0)->member)
#define elem2entry(struct_type, struct_member_name, elem_ptr) \
            (struct_type*)((int)elem_ptr - offset(struct_type, struct_member_name))

struct list_elem {
    struct list_elem* prev;
    struct list_elem* next;
};

//双向链表，head.next是第一个元素
struct list {
    struct list_elem head;
    struct list_elem tail;
}

typedef bool (function)(struct list_elem* elem, int arg);

void list_init(struct list* list);
void list_insert_before(struct list_elem* before, struct list_elem* elem);
void list_push(struct list* plist, struct list_elem* elem);
void list_append(struct list* plist, struct list_elem* elem);
void list_remove(struct list_elem* pelem);
struct list_elem* list_pop(struct list* plist);
bool elem_find(struct list* plist, struct list_elem* obj_elem);
struct list_elem* list_traversal(struct list* plist, function func, int arg);
uint32_t list_len(struct list* plist);
bool list_empty(struct list* plist);
#endif