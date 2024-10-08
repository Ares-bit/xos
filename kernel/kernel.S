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
    iretd

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
VECTOR 0x0c,ZERO
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
VECTOR 0x18,ERROR_CODE
VECTOR 0x19,ZERO
VECTOR 0x1a,ERROR_CODE
VECTOR 0x1b,ERROR_CODE 
VECTOR 0x1c,ZERO
VECTOR 0x1d,ERROR_CODE
VECTOR 0x1e,ERROR_CODE
VECTOR 0x1f,ZERO

VECTOR 0x20,ZERO    ;时钟中断
VECTOR 0x21,ZERO    ;键盘中断
VECTOR 0x22,ZERO    ;级联
VECTOR 0x23,ZERO    ;串口2
VECTOR 0x24,ZERO    ;串口1
VECTOR 0x25,ZERO    ;并口2
VECTOR 0x26,ZERO    ;软盘
VECTOR 0x27,ZERO    ;并口1
VECTOR 0x28,ZERO    ;实时时钟
VECTOR 0x29,ZERO    ;重定向
VECTOR 0x2a,ZERO    ;保留
VECTOR 0x2b,ZERO    ;保留
VECTOR 0x2c,ZERO    ;ps/2鼠标
VECTOR 0x2d,ZERO    ;fpu浮点单元异常
VECTOR 0x2e,ZERO    ;硬盘
VECTOR 0x2f,ZERO    ;保留

;0x80中断
[bits 32]
extern syscall_table
section .text
global syscall_handler
syscall_handler:
    push 0
    push ds
    push es
    push fs
    push gs
    pushad

    push 0x80
    ;handler不是经过C编译的函数，没有自己的栈，所以可以通过寄存器或一段内存去传递参数
    ;对于C编译的函数，编译器会为其维护栈，只能栈传参
    ;syscall传进来一个寄存器或一段内存，handler将他们压栈后
    ;（因为最多push dword，所以寄存器传参可以直接push，内存的只能push地址）
    ;再调用真正的系统调用sys_xxx，系统调用是C函数，编译后会为其维护栈，都通过内存取参数
    push edx
    push ecx
    push ebx

    call [syscall_table + eax * 4]
    add esp, 12     ;跨过参数

    mov [esp + 8 * 4], eax
    jmp intr_exit

; syscall_handler:
;     push 0
;     push ds
;     push es
;     push fs
;     push gs
;     pushad

;     push 0x80

;     ;获取3级esp
;     mov ebx, [esp + 4 + 48 + 4 + 12]

;     ;取出参数
;     push dword [ebx + 12];第3个参数
;     push dword [ebx + 8];第2个参数
;     push dword [ebx + 4];第1个参数
;     mov edx, [ebx]

;     call [syscall_table + edx * 4]
;     add esp, 12     ;跨过参数

;     mov [esp + 8 * 4], eax
;     jmp intr_exit