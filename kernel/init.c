#include "interrupt.h"
#include "print.h"
#include "timer.h"
#include "stdint.h"
#include "init.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"
#include "syscall_init.h"
#include "ide.h"
#include "fs.h"

void init_all()
{
    put_str("init_all\n");
    idt_init();
    mem_init();
	thread_init();
    timer_init();
    console_init();
    keyboard_init();
    tss_init();
    syscall_init();
    intr_enable();    //之前一直少这句，怎么跑成功的？ide_init需要打开中断
    ide_init();
    filesys_init();
}