#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"

#define PIC_M_CTRL  0x20
#define PIC_M_DATA  0x21
#define PIC_S_CTRL  0xa0
#define PIC_S_DATA  0xa1

#define IDT_DESC_CNT 0x81 //支持的总中断数

#define EFLAGS_IF   0x00000200
#define GET_EFLAGS(EFLAGS_VAR)  asm volatile("pushfl\n\t popl %0\n\t" : "=g"(EFLAGS_VAR))

//中断门描述格式
struct gate_desc {
    uint16_t    func_offset_low_word;//中断处理程序偏移地址低16位
    uint16_t    selector;//段选择子
    uint8_t     dcount;//门描述符高32位的低8位 固定全0
    uint8_t     attribute;//门描述符P+DPL+S+TYPE共8位
    uint16_t    func_offset_high_word;//中断处理程序偏移地址高16位
};

static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function_offset);

//中断门描述符表
static struct gate_desc idt[IDT_DESC_CNT];

//中断名
char* intr_name[IDT_DESC_CNT];

//C中断处理程序地址
intr_handler idt_table[IDT_DESC_CNT];
extern uint32_t syscall_handler(void);

//在汇编中导出的中断处理程序入口表
extern intr_handler intr_entry_table[IDT_DESC_CNT];


//初始化8259A
static void pic_init(void)
{
    //初始化主片
    outb(PIC_M_CTRL, 0x11);//ICW1 边沿触发 级联8259 需要ICW4
    outb(PIC_M_DATA, 0x20);//ICW2 起始中断向量号为0x20
    outb(PIC_M_DATA, 0x04);//ICW3 IR2接从片
    outb(PIC_M_DATA, 0x01);//ICW4 8086模式 手动EOI

    //初始化从片
    outb(PIC_S_CTRL, 0x11);//ICW1 边沿触发 级联8259 需要ICW4
    outb(PIC_S_DATA, 0x28);//CW2 起始中断向量号为0x28
    outb(PIC_S_DATA, 0x02);//ICW3 连接到主片IR2
    outb(PIC_S_DATA, 0x01);//ICW4 8086模式 手动EOI

    //打开主片IR0 IR1 屏蔽主从其他所有中断 OCW1
    outb(PIC_M_DATA, 0xf8);//开键盘+时钟+主片IRQ2以响应从片的从盘中断
    outb(PIC_S_DATA, 0xbf);//打开从片IRQ14

    put_str("pic_init done\n");
}

//创建中断门描述符
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function_offset)
{
    p_gdesc->func_offset_low_word = (uint32_t)function_offset & 0x0000ffff;
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcount = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t)function_offset & 0xffff0000) >> 16;
}

//初始化idt
static void idt_desc_init(void) {
    int i;

    for (i = 0; i < IDT_DESC_CNT; i++) {
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    //0x80是syscall中断 它需要让用户程序访问
    make_idt_desc(&idt[IDT_DESC_CNT - 1], IDT_DESC_ATTR_DPL3, syscall_handler);
    put_str("idt_desc_init done\n");
}

//通用中断处理函数 用于前20个中断
static void general_intr_handler(uint8_t vec_nr)
{
    if (vec_nr == 0x27 || vec_nr == 0x2f) {//IRQ7和IRQ15会产生伪中断 无需处理
        return;
    }

    //清空前4行内容以输出异常信息
    set_cursor(0);
    int cursor_pos = 0;
    while (cursor_pos < 320) {
        put_char(' ');
        cursor_pos++;
    }

    set_cursor(0);
    put_str("-------  exception message begin  -------\n");
    set_cursor(88);
    put_str(intr_name[vec_nr]);
    if (vec_nr == 14) {//PAGEFAULT
        int page_fault_vaddr = 0;
        asm ("movl %%cr2, %0" : "=r" (page_fault_vaddr));
        put_str("\npage fault addr is 0x");
        put_int(page_fault_vaddr);
    }
    put_str("\n-------   exception message end   -------\n");
    //能进中断处理程序就表示已经处在关中断情况下 所以不会再调度线程 while会一直卡住
    while(1);
}

//注册前0x21个中断
static void exception_init(void)
{
    int i;

    for (i = 0; i < IDT_DESC_CNT; i++) {
        idt_table[i] = general_intr_handler;
        intr_name[i] = "unknown";
    }

    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    //intr_name[15] = "";//15保留
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

//256个中断 8B能表示完
void register_handler(uint8_t vector_no, intr_handler function)
{
    idt_table[vector_no] = function;
}

//开中断并返回开中断前的状态
enum intr_status intr_enable() {
    enum intr_status old_status;
    if (INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        return old_status;
    } else {
        old_status = INTR_OFF;
        asm volatile("sti");
        return old_status;
    }
}

//关中断并返回关中断前的状态
enum intr_status intr_disable() {
    enum intr_status old_status;
    if (INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        asm volatile("cli": : : "memory");
        return old_status;
    } else {
        old_status = INTR_OFF;
        return old_status;
    }
}

enum intr_status intr_set_status(enum intr_status status) {
    return status & INTR_ON ? intr_enable() : intr_disable();
}

enum intr_status intr_get_status() {
    uint32_t eflags = 0;
    GET_EFLAGS(eflags);
    return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF;
}

void idt_init(void) {
    put_str("idt_init start\n");
    idt_desc_init();
    exception_init();
    pic_init();

    //加载idt
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0" : : "m"(idt_operand));
    put_str("idt_init done\n");
}