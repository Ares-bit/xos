#ifndef __USERPROG_WAIT_EXIT_H
#define __USERPROG_WAIT_EXIT_H
#include "stdint.h"
#include "thread.h"
void sys_exit(int32_t status);
pid_t sys_wait(int32_t* status);
#endif
