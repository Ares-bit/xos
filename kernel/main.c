#include "print.h"
#include "init.h"
#include "debug.h"
#include "stdint.h"
#include "memory.h"

int main(void) {
    put_str("I am kernel\n");
    init_all();
    void* addr = get_kernel_pages(3);
    put_str("\nget_kernel_pages start vaddr is:0x");
    put_int((uint32_t)addr);
    put_char('\n');
    ASSERT(1==2);
    return 0;
}