#include "pipe.h"
#include "ioqueue.h"
#include "fs.h"
#include "file.h"

extern struct file file_table[MAX_FILE_OPEN];
//判断一个描述符是否为管道，便于执行管道操作
bool is_pipe(uint32_t local_fd)
{
    uint32_t global_fd = fd_local2global(local_fd);
    return file_table[global_fd].fd_flag == PIPE_FLAG;
}

//创建Pipe
int32_t sys_pipe(int32_t pipefd[2])
{
    //在全局文件表中获取空位
    int32_t global_fd = get_free_slot_in_global();

    //分配一页内核空间作为pipe
    file_table[global_fd].fd_inode = get_kernel_pages(1);

    //将申请的内存强转为环形队列类型并初始化
    ioqueue_init((struct ioqueue*)file_table[global_fd].fd_inode);
    if (file_table[global_fd].fd_inode == NULL) {
        return -1;
    }

    file_table[global_fd].fd_flag = PIPE_FLAG;
    //pipe打开次数置为2，一个读一个写
    file_table[global_fd].fd_pos = 2;

    pipefd[0] = pcb_fd_install(global_fd);
    pipefd[1] = pcb_fd_install(global_fd);

    return 0;
}

//读pipe
uint32_t pipe_read(int32_t fd, void* buf, uint32_t count)
{
    char* buffer = buf;
    uint32_t bytes_read = 0;
    uint32_t global_fd = fd_local2global(fd);

    //获取管道页面环形缓冲区
    struct ioqueue* ioq = (struct ioqueue*)file_table[global_fd].fd_inode;

    uint32_t ioq_len = ioq_length(ioq);//读pipe这样没问题，因为读没之后再读一个才会休眠
    //选小的数据量读取，避免阻塞
    uint32_t size = ioq_len > count ? count : ioq_len;
    while (bytes_read < size) {
        *buffer = ioq_getchar(ioq);
        bytes_read++;
        buffer++;
    }
    return bytes_read;
}

//写Pipe
uint32_t pipe_write(int32_t fd, const void* buf, uint32_t count)
{
    uint32_t bytes_write = 0;
    uint32_t global_fd = fd_local2global(fd);
    struct ioqueue* ioq = (struct ioqueue*)file_table[global_fd].fd_inode;

    //选小的写
    //不妨假设这样一种场景：如果你的buffer容量为2，此时第一次写，长度为0，我让你写入2个字符
    //你在写第二个的时候队列就已经满了，这不就休眠了？那你怎么敢说的能保证生产者永不休眠的？
    uint32_t ioq_left = bufsize - ioq_length(ioq) - 1;//减1才对，比如我让你写2，队列总长就是2，只有写2-1=1才不会导致休眠
    uint32_t size = ioq_left > count ? count : ioq_left;

    const char* buffer = buf;
    while (bytes_write < size) {
        ioq_putchar(ioq, *buffer);
        bytes_write++;
        buffer++;
    }
    return bytes_write;
}