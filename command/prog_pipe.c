#include "stdio.h"
#include "syscall.h"
#include "string.h"

int main(int argc, char** argv)
{
    int32_t fd[2] = {-1};
    pipe(fd);
    int32_t pid = fork();
    if (pid) {
        close(fd[0]);
        write(fd[1], "Hi, my son, I love you!", 24);
        printf("\nI'm father, my pid is %d\n", getpid());
        return 8;
    } else {
        close(fd[1]);
        char buf[32] = {0};
        read(fd[0], buf, 24);
        printf("\nI'm child, my father said to me: \"%s\"\n", buf);
        return 9;
    }
}