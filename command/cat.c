#include "syscall.h"
#include "stdio.h"
#include "string.h"

int main(int argc, char** argv)
{
    if (argc > 2) {
        exit(-2);
    }

    //cat无参数时从标准输入读取，可能是键盘或管道，管道读取会选择最小的数据量读取，所以传入512不一定真读512，所以一定不会休眠
    if (argc == 1) {
        char buf[512] = {0};
        read(0, buf, 512);
        printf("%s",buf);
        exit(0);
    }

    int buf_size = 1024;
    char abs_path[512] = {0};
    void* buf = malloc(buf_size);
    if (buf == NULL) {
        printf("cat: malloc memory failed\n");
        return -1;
    }

    if (argv[1][0] != '/') {
        getcwd(abs_path, 512);
        strcat(abs_path, "/");
        strcat(abs_path, argv[1]);
    } else {
        strcpy(abs_path, argv[1]);
    }

    int fd = open(abs_path, O_RDONLY);
    if (fd == -1) {
        printf("cat: open %s failed\n", argv[1]);
        return -1;
    }

    int read_bytes = 0;
    while (1) {
        read_bytes = read(fd, buf, buf_size);
        if (read_bytes == -1) {
            break;
        }
        write(1, buf, read_bytes);
    }

    free(buf);
    close(fd);
    return 66;
}