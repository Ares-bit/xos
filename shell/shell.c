#include "shell.h"
#include "syscall.h"
#include "assert.h"
#include "stdio.h"
#include "file.h"
#include "buildin_cmd.h"

#define CMD_LEN     128//命令最长128个字符
#define MAX_ARG_NR  16//算上命令名，一共最多支持16个参数

//存储输入的命令
static char cmd_line[CMD_LEN] = {0};
//存储清洗后的路径
char final_path[MAX_PATH_LEN] = {0};
//用来记录当前目录，是当前目录的缓存，每次执行cd更新此内容
char cwd_cache[MAX_PATH_LEN] = {0};

//argv存储输入参数的首地址
char* argv[MAX_ARG_NR];
//argc记录输入参数个数
int32_t argc = -1;

//输出命令提示符
void print_prompt(void)
{
    printf("[xxy@localhost %s]$ ", cwd_cache);
}

//从键盘缓冲区读字符到buffer
static void readline(char* buf, int32_t count)
{
    assert(buf != NULL && count > 0);
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

//分析字符串cmd_str中以token为分隔符的单次，将各单词的指针存入argv数组
static int32_t cmd_parse(char* cmd_str, char** argv, char token)
{
    assert(cmd_str != NULL);
    int32_t arg_idx = 0;
    //每次分析前清空argv数组
    while (arg_idx < MAX_ARG_NR) {
        argv[arg_idx] = NULL;
        arg_idx++;
    }

    char* next = cmd_str;
    int32_t argc = 0;

    while (*next) {
        //过滤命令中多余的空格
        while (*next == token) {
            next++;
        }
        //如果上边的空格被过滤掉后直接变成了结尾，那就解析结束
        if (*next == '\0') {
            break;
        }
        //让argv[argc]指向对应参数字符串
        argv[argc] = next;

        //经过上面找到一个参数，然后往后移动到token，再重新开始下次参数寻找
        while (*next && *next != token) {
            next++;
        }
        //将当前参数后token置位\0形成单独的字符串
        if (*next) {
            *next++ = '\0';
        }

        if (argc > MAX_ARG_NR) {
            return -1;
        }

        argc++;
    }
    return argc;
}

void my_shell(void)
{
    cwd_cache[0] = '/';//将当前工作目录设置为根
    cwd_cache[1] = '\0';
    while (1) {
        print_prompt();//显示命令提示符
        memset(final_path, 0, MAX_PATH_LEN);
        memset(cmd_line, 0, CMD_LEN);//接受命令前清空命令字符串
        readline(cmd_line, CMD_LEN);//接收命令
        //只输入回车则等待下一条命令输入
        if (cmd_line[0] == '\0') {
            continue;
        }

        argc = -1;
        argc = cmd_parse(cmd_line, argv, ' ');
        if (argc == -1) {
            printf("num of arguments exceed %d\n", MAX_ARG_NR);
            continue;
        }
        
        if (!strcmp("ls", argv[0])) {
            buildin_ls(argc, argv);
        } else if (!strcmp("cd", argv[0])) {
            if (buildin_cd(argc, argv) != NULL) {
                memset(cwd_cache, 0, MAX_PATH_LEN);//cwd一开始长度写的是cmd len，这里memset超了，应该是把rootdir给置成0了
                strcpy(cwd_cache, final_path);
            }
        } else if (!strcmp("pwd", argv[0])) {
            buildin_pwd(argc, argv);
        } else if (!strcmp("ps", argv[0])) {
            buildin_ps(argc, argv);
        } else if (!strcmp("clear", argv[0])) {
            buildin_clear(argc, argv);
        } else if (!strcmp("mkdir", argv[0])) {
            buildin_mkdir(argc, argv);
        } else if (!strcmp("rmdir", argv[0])) {
            buildin_rmdir(argc, argv);
        } else if (!strcmp("rm", argv[0])) {
            buildin_rm(argc, argv);
        } else {
            //外部命令
            int32_t pid = fork();
            if (pid) {
                //卡住父进程让他不要接受下一条命令
                while(1);
            } else {
                //子进程
                make_clear_abs_path(argv[0], final_path);
                argv[0] = final_path;//让argv[0]指向清洗后的路径
                struct stat file_stat = {0};
                //用stat判断文件是否存在也是一种方法也
                if (stat(argv[0], &file_stat) == -1) {
                    printf("my_shell: cannot access %s: No such file or directory\n", argv[0]);
                } else {
                    execv(argv[0], argv);
                }
                while(1);
            }
        }
        //这段不需要，cmd_parse里会清argv，这里是为了在执行外部命令前清除上次输入的argv参数
        int32_t arg_idx = 0;
        while (arg_idx < MAX_ARG_NR) {
            argv[arg_idx] = NULL;
            arg_idx++;
        }
    }
    panic("my_shell: should not be here!");
}