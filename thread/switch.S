[bits 32]
section .text
global switch_to
switch_to:
    ;保护cur环境
    push esi
    push edi
    push ebx
    push ebp

    mov eax,[esp+5*4];定位cur pcb首地址
    mov [eax],esp;

    ;恢复next环境
    mov eax,[esp+6*4]
    mov esp,[eax]

    pop ebp
    pop ebx
    pop edi
    pop esi
    ret