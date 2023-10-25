#ifndef __LIB_KERNEL_PRINT_H
#define __LIB_KERNEL_PRINT_H
#include "stdint.h"
void put_char(uint8_t char_asci);
void put_str(char* message);
void put_int(uint32_t num);//还是得uint 我们put_int就是按uint处理的 因为你传-5的话打印就不是-5了
#endif