#ifndef __USERPROG_PROCESS_H
#define __USERPROG_PROCESS_H

#define USER_STACK3_VADDR   (0xc0000000 - 0x1000)
#define DEFAULT_PRIO        31
#define USER_VADDR_START    0x8048000

void process_activate(struct task_struct* p_thread);
void process_execute(void* filename, char* name);
#endif