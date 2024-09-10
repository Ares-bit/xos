[bits 32]
extern main
extern exit
section .text
global _start
_start:
    push ebx;压入argv
    push ecx;压入argc
    call main
    ;eax存储main的返回值，将其压入栈中再call exit，exit便可以获取返回值
    push eax
    call exit