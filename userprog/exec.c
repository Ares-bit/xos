#include "exec.h"
#include "stdint.h"
#include "memory.h"
#include "thread.h"

extern void intr_exit(void);

typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

//32位elf头
struct Elf32_Ehdr {
    unsigned char   e_ident[16];
    Elf32_Half      e_type;
    Elf32_Half      e_machine;
    Elf32_Word      e_version;
    Elf32_Addr      e_entry;
    Elf32_Off       e_phoff;
    Elf32_Off       e_shoff;
    Elf32_Word      e_flags;
    Elf32_Half      e_ehsize;
    Elf32_Half      e_phentsize;
    Elf32_Half      e_phnum;
    Elf32_Half      e_shentsize;
    Elf32_Half      e_shnum;
    Elf32_Half      e_shstrndx;      
};

//程序头表项 32字节
struct Elf32_Phdr {
    Elf32_Word  p_type;
    Elf32_Off   p_offset;
    Elf32_Addr  p_vaddr;
    Elf32_Addr  p_paddr;
    Elf32_Word  p_filesz;
    Elf32_Word  p_memsz;
    Elf32_Word  p_flags;
    Elf32_Word  p_align;
};

//段类型
enum segment_type {
    PT_NULL,    //忽略
    PT_LOAD,    //可加载程序段
    PT_DYNAMIC, //动态加载信息
    PT_INTERP,  //动态加载器名称
    PT_NOTE,    //一些辅助信息
    PT_SHLIB,   //保留
    PT_PHDR     //程序头表
};

//从文件偏移处读取一定大小到指定虚拟地址中
static bool segment_load(int32_t fd, uint32_t offset, uint32_t filesz, uint32_t vaddr)
{   
    //获得指定虚拟地址所在页面首地址
    uint32_t vaddr_first_page = vaddr & 0xfffff000;

    //计算虚拟地址第一页内需要用多少
    uint32_t size_in_first_page = PG_SIZE - (vaddr & 0x00000fff);
    uint32_t occupy_pages = 0;
    if (filesz > size_in_first_page) {
        uint32_t left_pages = filesz - size_in_first_page;
        occupy_pages = DIV_ROUND_UP(left_pages, PG_SIZE) + 1;//算上第一页，本段总共要占多少页面
    } else {
        occupy_pages = 1;
    }

    uint32_t page_idx = 0;
    uint32_t vaddr_page = vaddr_first_page;
    while (page_idx < occupy_pages) {
        //得到当前页面地址在页表中的页目录项和页表项
        uint32_t* pde = pde_ptr(vaddr_page);
        uint32_t* pte = pte_ptr(vaddr_page);

        //要先判断pde页目录项是不是存在，否则访问pte时会缺页错误
        //如果页表项页目录项不存在，则申请一页内存安装到虚拟地址对应的页表中
        if (!(*pde & 0x00000001) && !(*pte & 0x00000001)) {
            if (get_a_page(PF_USER, vaddr_page) == NULL) {
                return false;//如果失败了之前分配的页还没收回呢
            }
        }

        vaddr_page += PG_SIZE;
        page_idx++;
    }
    //内存分配好后，将文件读入vaddr中
    sys_lseek(fd, offset, SEEK_SET);
    sys_read(fd, (void*)vaddr, filesz);
    return true;
}

//将elf文件加载进内存，取其程序段依次读到相应的虚拟地址处即为加载
static int32_t load(const char* pathname)
{
    int32_t ret = -1;
    struct Elf32_Ehdr elf_header = {0};//elf头
    struct Elf32_Phdr prog_header = {0};//程序头表项，用于指示每个段的属性

    //打开pathname指向的文件
    int32_t fd = sys_open(pathname, O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    //读取elf头
    if (sys_read(fd, &elf_header, sizeof(struct Elf32_Ehdr)) != sizeof(struct Elf32_Ehdr)) {
        ret = -1;
        goto done;
    }

    //check elf头
    if (memcmp(elf_header.e_ident, "\177ELF\1\1\1", 7)
        || elf_header.e_type != 2//ET_EXEC
        || elf_header.e_machine != 3//EM_386
        || elf_header.e_version != 1
        || elf_header.e_phnum > 1024
        || elf_header.e_phentsize != sizeof(struct Elf32_Phdr)) {
        ret = -1;
        goto done;
    }

    //获取程序头表偏移地址和每个表项的大小
    Elf32_Off prog_header_offset = elf_header.e_phoff;
    Elf32_Half prog_header_size = elf_header.e_phentsize;

    uint32_t prog_idx = 0;
    //遍历程序头表中每一个段，将其加载进内存
    while (prog_idx < elf_header.e_phnum) {
        memset(&prog_header, 0, prog_header_size);

        sys_lseek(fd, prog_header_offset, SEEK_SET);

        if (sys_read(fd, &prog_header, prog_header_size) != prog_header_size) {
            ret = -1;
            goto done;
        }

        if (PT_LOAD == prog_header.p_type) {
            //将该段读到内存，p_offset指明段到文件头的偏移量
            if (!segment_load(fd, prog_header.p_offset, prog_header.p_filesz, prog_header.vaddr)) {
                ret = -1;
                goto done;
            }
        }

        prog_header_offset += prog_header_size;
        prog_idx++;
    }
    //如果加载成功，则返回elf文件的入口
    ret = elf_header.e_entry;

done:
    sys_close(fd);
    return ret;
}

//execv用新进程覆盖老进程
int32_t sys_execv(const char* path, const char* argv[])
{
    uint32_t argc = 0;
    //遍历argv，统计参数个数
    while (argv[argc]) {
        argc++;
    }

    //将进程加载进内存，返回入口地址
    int32_t entry_point = load(path);
    if (entry_point == -1) {
        return -1;
    }

    //获取当前进程为了接下来的覆盖
    struct task_struct* cur = running_thread();
    //修改进程名为新进程
    memcpy(cur->name, path, TASK_NAME_LEN);
    cur->name[TASK_NAME_LEN - 1] = '\0';

    //修改老进程内核栈
    struct intr_stack* intr_0_stack = (struct intr_stack*)((uint32_t)cur + PG_SIZE - sizeof(struct intr_stack));
    intr_0_stack->ebx = (int32_t)argv;
    intr_0_stack->ecx = argc;
    intr_0_stack->eip = (void*)entry_point;//eip指向进程入口地址
    intr_0_stack->esp = (void*)0xc0000000;//将新进程esp设置为用户栈栈底，因为它第一次执行，栈认为是空的

    //将esp指向内核中断栈后，直接jmp到intr_exit假装从中断返回，然后弹中断栈从而使新进程得到执行
    asm volatile("movl %0, %%esp\n\tjmp intr_exit" : : g(intr_0_stack) : "memory");
    return 0;//make gcc happy
}