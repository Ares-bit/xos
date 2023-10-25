%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR

GDT_BASE:
    dd 0x00000000
    dd 0x00000000

CODE_DESC:
    dd 0x0000ffff
    dd DESC_CODE_HIGH4

DATA_DESC:
    dd 0x0000ffff
    dd DESC_DATA_HIGH4

VIDEO_DESC:
    dd 0x80000007
    dd DESC_VIDEO_HIGH4

times 60 dq 0

GDT_SIZE equ $-GDT_BASE
GDT_LIMIT equ GDT_SIZE-1

SELECTOR_CODE equ (0x0001<<3) + TI_GDT + RPL0
SELECTOR_DATA equ (0x0002<<3) + TI_GDT + RPL0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

;前有64个8B描述符，共0x200B，加上加载地址0x900，所以total_mem_bytes地址为0xb00
total_mem_bytes dd 0

gdt_ptr:
    dw GDT_LIMIT
    dd GDT_BASE

ards_buf times 244 db 0
ards_nr dw 0

loader_start:
    ;0xe820
    xor ebx,ebx
    mov edx,0x534d4150
    mov di,ards_buf

.e820_mem_get_loop:
    mov eax,0x0000e820
    mov ecx,20
    int 0x15
    jc .e820_failed_so_try_e801
    add di,cx
    inc word [ards_nr]
    cmp ebx,0
    jnz .e820_mem_get_loop

    mov cx,[ards_nr]
    mov ebx,ards_buf
    xor edx,edx
.find_max_mem_area:
    mov eax,[ebx]
    add eax,[ebx+8]
    add ebx,20
    cmp edx,eax
    jge .next_ards
    mov edx,eax
.next_ards:
    add bx,20
    loop .find_max_mem_area
    jmp .mem_get_ok

.e820_failed_so_try_e801:
    mov ax,0xe801
    int 0x15
    jc .e801_failed_so_try88

    mov cx,0x400
    mul cx
    shl edx,16
    and eax,0x0000ffff
    or edx,eax
    add edx,0x100000
    mov esi,edx

    xor eax,eax
    mov ecx,0x10000
    mov ax,bx
    mul ecx
    add esi,eax
    mov esi,edx
    jmp .mem_get_ok

.e801_failed_so_try88:
    mov ah,0x88
    int 0x15
    jc .error_hlt
    and eax,0x0000ffff

    mov cx,0x400
    mul cx
    shl edx,16
    or edx,eax
    add edx,0x100000

.mem_get_ok:
    mov [total_mem_bytes],edx


    in al,0x92
    or al,0000_0010B
    out 0x92,al

    lgdt [gdt_ptr]

    mov eax,cr0
    or eax,0x00000001
    mov cr0,eax

    jmp dword SELECTOR_CODE:p_mode_start

.error_hlt:
    hlt

;没人说不能在实模式下设置页表，只不过实模式下不能用32位寄存器太麻烦
[bits 32]
p_mode_start:
    mov ax,SELECTOR_DATA
    mov es,ax
    mov ds,ax
    mov fs,ax
    mov ss,ax
    mov esp,LOADER_STACK_TOP
    mov ax,SELECTOR_VIDEO
    mov gs,ax

    mov eax,KERNEL_START_SECTOR
    mov ebx,KERNEL_BIN_BASE_ADDR
    mov ecx,200

    call rd_disk_m_32

    call set_up_page

    sgdt [gdt_ptr]

    mov ebx,[gdt_ptr+2]
    or dword [ebx+3*8+4],0xc0000000

    add dword [gdt_ptr+2],0xc0000000

    add esp,0xc0000000

    mov eax,PAGE_DIR_TABLE_POS
    mov cr3,eax

    mov eax,cr0
    or eax,0x80000000
    mov cr0,eax

    lgdt [gdt_ptr]

    ;mov byte [gs:160],'v'
    ;mov byte [gs:162],'i'
    ;mov byte [gs:164],'r'
    ;mov byte [gs:166],'t'
    ;mov byte [gs:168],'u'
    ;mov byte [gs:170],'a'
    ;mov byte [gs:172],'l'

    jmp SELECTOR_CODE:enter_kernel

;==================================================
enter_kernel:
    ;mov byte [gs:320],'k'
    ;mov byte [gs:322],'e'
    ;mov byte [gs:324],'r'
    ;mov byte [gs:326],'n'
    ;mov byte [gs:328],'e'
    ;mov byte [gs:330],'l'

    ;mov byte [gs:480],'w'
    ;mov byte [gs:482],'h'
    ;mov byte [gs:484],'i'
    ;mov byte [gs:486],'l'
    ;mov byte [gs:488],'e'
    ;mov byte [gs:490],'('
    ;mov byte [gs:492],'1'
    ;mov byte [gs:494],')'
    ;mov byte [gs:496],';'

    call kernel_init
    mov esp,0xc009f000
    jmp KERNEL_ENTRY_POINT

kernel_init:
    xor eax,eax
    xor ebx,ebx;ebx记录程序头表地址
    xor ecx,ecx;cx记录程序头表中段数目
    xor edx,edx;dx记录程序头表项大小，e_phentsize

    mov dx,[KERNEL_BIN_BASE_ADDR + 42];e_phentsize两字节，只能用bx装，用ebx就多了
    mov ebx,[KERNEL_BIN_BASE_ADDR + 28];e_phoff表示程序头表中第1项在文件中的偏移量
    add ebx,KERNEL_BIN_BASE_ADDR;e_phoff还要加上kernel.bin在内存中的首地址才是虚拟地址
    mov cx,[KERNEL_BIN_BASE_ADDR + 44];e_phnum两字节，只能用cx装，用ecx就多了

.each_segment:
    cmp byte [ebx + 0],PT_NULL;取出程序头表指向的程序头中的type字段
    je .PTNULL

    push dword [ebx + 16];压入段大小
    mov eax,[ebx + 4];取出段距文件起始的偏移量
    add eax,KERNEL_BIN_BASE_ADDR;得到段加载处的虚拟地址
    push eax;压入源地址
    push dword [ebx + 8];压入目的地址
    call mem_cpy
    add esp,4 * 3;恢复esp为压入参数前的值

.PTNULL:
    add ebx,edx;移动到下一表项
    loop .each_segment
    ret
;====================================================
mem_cpy:
    cld
    push ebp
    mov ebp,esp
    push ecx;外层循环还要用ecx，别忘了存一下
    mov edi,[ebp + 8]
    mov esi,[ebp + 12]
    mov ecx,[ebp + 16]
    rep movsb
    
    pop ecx
    pop ebp
    ret
;====================================================
set_up_page:
    mov ecx,4096
    mov esi,0
.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS+esi],0
    inc esi
    loop .clear_page_dir

.create_pde:
    mov eax,PAGE_DIR_TABLE_POS
    add eax,0x1000
    mov ebx,eax

    or eax,PG_US_U|PG_RW_W|PG_P
    mov [PAGE_DIR_TABLE_POS],eax
    mov [PAGE_DIR_TABLE_POS+0x300*4],eax

    sub eax,0x1000
    mov [PAGE_DIR_TABLE_POS+4092],eax

    mov ecx,256
    mov esi,0
    xor edx,edx
    mov edx,PG_P|PG_RW_W|PG_US_U
.create_pte:
    mov [ebx+esi*4],edx
    add edx,0x1000
    inc esi
    loop .create_pte

    mov eax,PAGE_DIR_TABLE_POS
    or eax,PG_P|PG_RW_W|PG_US_U
    add eax,0x2000
    mov ebx,PAGE_DIR_TABLE_POS
    mov esi,769
    mov ecx,1022-769+1
.create_kernel_pde:
    mov [ebx+esi*4],eax
    add eax,0x1000
    inc esi
    loop .create_kernel_pde
    ret
;==========================================
rd_disk_m_32:
     mov esi,eax
     mov di,cx

     mov dx,0x1f2
     mov al,cl
     out dx,al

     mov eax,esi
     mov dx,0x1f3
     out dx,al

     shr eax,8
     mov dx,0x1f4
     out dx,al

     shr eax,8
     mov dx,0x1f5
     out dx,al

     shr eax,8
     mov dx,0x1f6
     and al,0x0f
     or al,0xe0
     out dx,al

     mov dx,0x1f7
     mov al,0x20
     out dx,al

.not_ready:
     nop
     in al,dx
     and al,0x88
     cmp al,0x08
     jnz .not_ready

     mov ax,di
     mov dx,256
     mul dx
     mov cx,ax

     mov dx,0x1f0

.go_on_read:
     in ax,dx
     mov [ebx],ax
     add ebx,2
     loop .go_on_read
     ret