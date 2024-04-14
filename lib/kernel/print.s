;%include "../../../boot/include/boot.inc"
%include "boot.inc"

SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0

[bits 32]
section .text
global put_str
;put_str打印以\0为结尾的字符串
put_str:
    push ebx
    push ecx
    
    xor ecx,ecx
    mov ebx,[esp + 3 * 4]
.goon:
    mov cl,[ebx]
    cmp cl,0
    je .str_over
    push ecx
    call put_char
    add esp,4
    inc ebx
    jmp .goon
.str_over:
    pop ecx
    pop ebx
    ret

;==========================================
global put_char;导出put_char符号

put_char:
    pushad

    mov ax,SELECTOR_VIDEO
    mov gs,ax

    ;获取当前光标位置高8位
    mov dx,0x3d4
    mov al,0x0e
    out dx,al
    mov dx,0x3d5
    in al,dx
    mov ah,al

    ;获取当前光标位置低8位
    mov dx,0x3d4
    mov al,0x0f
    out dx,al
    mov dx,0x3d5
    in al,dx
    
    ;光标位置存bx
    mov bx,ax

    ;取出参数
    mov ecx,[esp + 36];可以使用ebp获取参数 与esp获取参数没有区别 但是建议还是用ebp

    ;回车
    cmp cl,0x0d
    jz .is_carriage_return

    ;换行
    cmp cl,0x0a
    jz .is_line_feed

    ;退格
    cmp cl,0x08
    jz .is_backspace

    ;其余字符均当做可见字符
    jmp .put_other

.is_backspace:
    dec bx
    shl bx,1
    mov byte [gs:bx],0x20
    inc bx
    mov byte [gs:bx],0x07
    shr bx,1
    jmp .set_cursor

.put_other:
    shl bx,1
    mov [gs:bx],cl
    inc bx
    mov byte [gs:bx],0x07
    shr bx,1
    inc bx
    cmp bx,2000
    jl .set_cursor

;打印完第2000个other char后 按照回车换行处理
;换行光标平移到下一行
.is_line_feed:
;回车回到本行行首
.is_carriage_return:
    xor dx,dx
    mov ax,bx
    mov si,80
    ;减去光标离本行行首的偏移字符个数
    div si
    sub bx,dx
.is_carriage_return_end:
    add bx,80
    cmp bx,2000
.is_line_feed_end:
    jl .set_cursor

;滚屏    
.roll_screen:
    cld
    mov ecx,960
    mov esi, 0xc00b80a0
    mov edi, 0xc00b8000
    rep movsd

    ;最后一行清空
    mov ecx,80
    mov ebx,3840
.cls:
    mov word [gs:ebx],0x0720
    add ebx,2
    loop .cls
    mov bx,1920

.set_cursor:
    mov dx,0x03d4
    mov al,0x0e
    out dx,al
    mov dx,0x03d5
    mov al,bh
    out dx,al

    mov dx,0x03d4
    mov al,0x0f
    out dx,al
    mov dx,0x03d5
    mov al,bl
    out dx,al

.put_char_done:
    popad
    ret

;=========================================
section .data
put_int_buffer dq 0

section .text
global put_int
;打印32位整型
put_int:
    pushad

    mov ebp,esp

    mov eax,[ebp + 9 * 4]
    mov edx,eax

    mov ebx,put_int_buffer
    mov edi,7

    mov ecx,8

.16based_4bits:
    and edx,0x0000000f

    cmp edx,9
    jg .is_A2F
    add edx,'0'
    jmp .store

.is_A2F:
    sub edx,10
    add edx,'A'

.store:
    mov [ebx+edi],dl
    dec edi
    shr eax,4
    mov edx,eax
    loop .16based_4bits

.ready_to_print:
    inc edi
;核心思想是edi始终指向'0'
.skip_prefix_0:
    cmp edi,8
    je .full0

.go_on_skip:
   mov cl, [put_int_buffer+edi]
    inc edi
    cmp cl,'0'
    je .skip_prefix_0
    dec edi
    jmp .put_each_num

.full0:
    mov cl,'0'

.put_each_num:
    push ecx
    call put_char
    add esp,4
    inc edi
    mov cl,[put_int_buffer+edi]
    cmp edi,8
    jl .put_each_num
    popad
    ret

global set_cursor
set_cursor:
    pushad

    mov bx,[esp + 9*4]

    mov dx,0x03d4
    mov al,0x0e
    out dx,al
    mov dx,0x03d5
    mov al,bh
    out dx,al

    mov dx,0x03d4
    mov al,0x0f
    out dx,al
    mov dx,0x03d5
    mov al,bl
    out dx,al

    popad
    ret