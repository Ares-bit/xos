[bits 32]
%define ERROR_CODE  nop
%define ZERO        push 0

extern put_str
extern idt_table

section .data
intr_str db "interrupt occur!",0xa,0
global intr_entry_table

intr_entry_table:

%macro VECTOR 2
section .text
intr%1entry:
    %2

    push ds
    push es
    push fs
    push gs
    pushad

    ;EOI先发 再call中断处理程序
    mov al,0x20 ;中断结束命令EOI
    out 0xa0,al ;对于从片进入的中断 主片从片都要发EOI
    out 0x20,al

    push %1

    call [idt_table + %1 * 4]
    jmp intr_exit

section .data
    dd intr%1entry
%endmacro

section .text
global intr_exit
intr_exit:
    add esp,4;跳过参数
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp,4;跳过error code
    iret

;0-19 CPU
VECTOR 0x00,ZERO
VECTOR 0x01,ZERO
VECTOR 0x02,ZERO
VECTOR 0x03,ZERO
VECTOR 0x04,ZERO
VECTOR 0x05,ZERO
VECTOR 0x06,ZERO
VECTOR 0x07,ZERO
VECTOR 0x08,ERROR_CODE
VECTOR 0x09,ZERO
VECTOR 0x0a,ERROR_CODE
VECTOR 0x0b,ERROR_CODE
VECTOR 0x0c,ERROR_CODE
VECTOR 0x0d,ERROR_CODE
VECTOR 0x0e,ERROR_CODE
VECTOR 0x0f,ZERO
VECTOR 0x10,ZERO
VECTOR 0x11,ERROR_CODE
VECTOR 0x12,ZERO
VECTOR 0x13,ZERO

;20-31 reserved
VECTOR 0x14,ZERO
VECTOR 0x15,ZERO
VECTOR 0x16,ZERO
VECTOR 0x17,ZERO
VECTOR 0x18,ZERO
VECTOR 0x19,ZERO
VECTOR 0x1a,ZERO
VECTOR 0x1b,ZERO
VECTOR 0x1c,ZERO
VECTOR 0x1d,ZERO
VECTOR 0x1e,ZERO
VECTOR 0x1f,ZERO
VECTOR 0x20,ZERO