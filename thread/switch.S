[bits 32]
section .text
global switch_to
switch_to:
    ;保护cur环境
    push esi
    push edi
    push ebx
    push ebp

    mov eax,[esp+5*4];定位cur pcb首地址，调用switch_to时传入cur next，cur参数在esp+5地址处
    mov [eax],esp;把当前cur的栈顶指针esp保存到self_kstack/[eax]中

    ;恢复next环境
    mov eax,[esp+6*4];next pcb参数在esp+6地址处
    mov esp,[eax];将esp指向next pcb首地址/self_kstack/[eax]中

    pop ebp
    pop ebx
    pop edi
    pop esi
    ret