#include "shell.h"
#include "syscall.h"
#include "debug.h"
#include "stdio.h"
#include "file.h"

#define CMD_LEN     128//命令最长128个字符
#define MAX_ARG_NR  16//算上命令名，一共最多支持16个参数

//存储输入的命令
static char cmd_line[CMD_LEN] = {0};

//用来记录当前目录，是当前目录的缓存，每次执行cd更新此内容
char cwd_cache[64] = {0};

//输出命令提示符
void print_prompt(void)
{
    printf("[xxy@localhost %s]$ ", cwd_cache);
}

//从键盘缓冲区读字符到buffer
static void readline(char* buf, int32_t count)
{
    ASSERT(buf != NULL && count > 0);
    char* pos = buf;
    //读取键盘输入，直到遇见回车，将之前输入作为一条命令
    while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count) {
        //一个字符一个字符处理
        switch (*pos) {
            case '\n':
            case '\r':
                *pos = '\0';//表示用户输入命令结束，将cmd_line置一个\0，将之前的输入作为一个命令字符串解析
                putchar('\n');//要回显出来，输入的每个字符都要回显
                return;//输入回车表示本地读取结束，直接返回
            case '\b':
            //如果第一个就输入退格，不能让退，退了就把命令提示符删除了
                if (buf[0] != '\b') {
                    --pos;
                    putchar('\b');//回显字符
                }
                break;
            case 'l' - 'a':
                *pos = '\0';
                clear();
                print_prompt();
                printf("%s", buf);
                break;
            case 'u' - 'a':
                while (buf != pos) {
                    putchar('\b');
                    pos--;
                    //*(pos--) = '\0';//这句没必要
                }
                break;
            default:
                putchar(*pos);
                pos++;
                break;//本地没有结束，接着读，直到碰见回车，所以pos还是要往后推的
        }
    }
    printf("readline: can't find enter-key in the cmd line, max num of char is 128\n");
}

void my_shell(void)
{
    cwd_cache[0] = '/';//将当前工作目录设置为根
    while (1) {
        print_prompt();//显示命令提示符
        memset(cmd_line, 0, CMD_LEN);//接受命令前清空命令字符串
        readline(cmd_line, CMD_LEN);//接收命令
        //只输入回车则等待下一条命令输入
        if (cmd_line[0] == '\0') {
            continue;
        }
    }
    PANIC("my_shell: should not be here!");
}