#include "buildin_cmd.h"
#include "assert.h"
#include "string.h"
#include "syscall.h"
#include "fs.h"
#include "stdio.h"

extern char final_path[MAX_PATH_LEN];
//将old中的.和..转换成绝对路径
static void wash_path(char* old_abs_path, char* new_abs_path)
{
    assert(old_abs_path[0] == '/');
    char name[MAX_FILE_NAME_LEN] = {0};
    char* sub_path = old_abs_path;
    //将第一级目录名解析出来
    sub_path = path_parse(sub_path, name);
    if (name[0] == '\0') {
        //如果只输入/，那么肯定没有一级目录名，则直接返回/
        new_abs_path[0] = '/';
        new_abs_path[1] = '\0';
        return;
    }
    //如果第一级目录存在，就在new前面加一个/，new前置0是为了防止传入的不干净，否则strcat不会拼到第一位
    new_abs_path[0] = '\0';
    strcat(new_abs_path, "/");
    while (name[0]) {
        //如果遇到..则去除最后一级 /a/b遇到..变成/a
        if (!strcmp("..", name)) {
            char* slash_ptr = strrchr(new_abs_path, '/');
            if (slash_ptr != new_abs_path) {//如果new不只有/则可以去掉当前级相当于回退
                *slash_ptr = '\0';
            } else {
                *(slash_ptr + 1) = '\0';//如果new只有一个/：/或/a，则在/后结尾
            }
        } else if (strcmp(".", name)) {
            //如果不是.就拼接到new，如果是.就啥也不做
            if (strcmp(new_abs_path, "/")) {
                //防止出现//的情况
                strcat(new_abs_path, "/");
            }
            strcat(new_abs_path, name);
        }
        memset(name, 0, MAX_FILE_NAME_LEN);
        if (sub_path) {
            sub_path = path_parse(sub_path, name);
        }
    }
}

//将绝对或相对的path处理为不含.和..的绝对路径
void make_clear_abs_path(char* path, char* final_path)
{
    char abs_path[MAX_PATH_LEN] = {0};
    //如果输入path是绝对路径，则直接走到wash，如果是相对路径，则与当前工作路径拼接后走到wash
    if (path[0] != '/') {
        memset(abs_path, 0, MAX_PATH_LEN);
        if (getcwd(abs_path, MAX_PATH_LEN) != NULL) {
            if (!((abs_path[0] == '/') && (abs_path[1] == '\0'))) {
                //如果当前工作路径不是/则加上/
                strcat(abs_path, "/");
            }
        }
    }
    //将输入路径和当前工作路径拼接后一起处理
    strcat(abs_path, path);
    wash_path(abs_path, final_path);
}

void buildin_pwd(uint32_t argc, char** argv UNUSED)
{
    if (argc != 1) {
        printf("pwd: no argument support!\n");
        return;
    } else {
        if (NULL != getcwd(final_path, MAX_PATH_LEN)) {
            printf("%s\n", final_path);
        } else {
            printf("pwd: get current work directory failed.\n");
        }
    }
}

char* buildin_cd(uint32_t argc, char** argv)
{
    if (argc > 2) {
        printf("cd: only support 1 argument!\n");
        return NULL;
    } else {
        //如果只输入cd，则回到根目录
        if (argc == 1) {
            final_path[0] = '/';
            final_path[1] = '\0';
        } else {
            //如果有两个参数，就把参数1拿出来清洗
            make_clear_abs_path(argv[1], final_path);
        }
        //将当前shell所在进程init的当前工作目录设置为输入的这个final_path
        if (chdir(final_path) == -1) {
            printf("cd: no such directory %s\n", final_path);
            return NULL;
        }
    }
    //最后返回清洗后的final path可能还有后用
    return final_path;
}

void buildin_ls(uint32_t argc, char** argv)
{
    char* pathname = NULL;
    struct stat file_stat = {0};
    bool long_info = false;//是否要打印文件stat
    uint32_t arg_path_nr = 0;
    uint32_t arg_idx = 1;//跨过argv[0]=ls

    //参数解析
    while (arg_idx < argc) {
        if (argv[arg_idx][0] == '-') {
            if (!strcmp(argv[arg_idx], "-l")) {
                long_info = true;
            } else if (!strcmp(argv[arg_idx], "-h")) {
                printf("usage: -l list all information about the file.\n-h for help\nlist all files in the current directory if no option\n");
                return;
            } else {
                //仅支持-l -h
                printf("ls: invalid option %s\nTry 'ls -h' for more information.\n", argv[arg_idx]);
                return;
            }
        } else {
            if (arg_path_nr == 0) {
                //如果ls后接路径，则列出路径中的文件
                pathname = argv[arg_idx];//记录将要ls的路径
                arg_path_nr = 1;
            } else {
                printf("ls: only support one path\n");
                return;
            }
        }
        arg_idx++;
    }

    //如果没有输入ls+路径 则列出当前工作路径下的文件
    if (pathname == NULL) {
        if (NULL != getcwd(final_path, MAX_PATH_LEN)) {
            pathname = final_path;
        } else {
            printf("ls: getcwd for default path failed\n");
            return;
        }
    } else {
        //如果输入路径，将其清洗后作为目的
        make_clear_abs_path(pathname, final_path);
        pathname = final_path;
    }

    //获取路径详细属性，看用户输入的是个目录还是文件
    if (stat(pathname, &file_stat) == -1) {
        printf("ls: cannot access %s: No such file or directory\n", pathname);
        return;
    }

    if (file_stat.st_filetype == FT_DIRECTORY) {
        struct dir* dir = opendir(pathname);
        struct dir_entry* dir_e = NULL;
        char sub_pathname[MAX_PATH_LEN] = {0};
        uint32_t pathname_len = strlen(pathname);
        uint32_t last_char_idx = pathname_len - 1;
        memcpy(sub_pathname, pathname, pathname_len);

        //将路径拷贝给subpahtname，并且给末尾补充上/
        if (sub_pathname[last_char_idx] != '/') {
            sub_pathname[pathname_len] = '/';//这不会直接把最后一个目录名给改了么
            pathname_len++;
        }
        rewinddir(dir);
        if (long_info) {
            char ftype;
            printf("total: %d\n", file_stat.st_size);
            while (dir_e = readdir(dir)) {
                ftype = 'd';
                if (dir_e->f_type == FT_REGULAR) {
                    ftype = '-';
                }
                sub_pathname[pathname_len] = '\0';
                strcat(sub_pathname, dir_e->filename);
                memset(&file_stat, 0, sizeof(struct stat));
                //拼接成完整路径以获取每个文件的详细属性
                if (stat(sub_pathname, &file_stat) == -1) {
                    printf("ls: cannot access %s: No such file or directory\n", dir_e->filename);
                    return;
                }
                printf("%c  %d  %d  %s\n", ftype, dir_e->i_no, file_stat.st_size, dir_e->filename);
            }
        } else {
            //如果不打印详细信息就只打印名
            while (dir_e = readdir(dir)) {
                printf("%s  ", dir_e->filename);
            }
            printf("\n");
        }
        closedir(dir);
    } else {
        //如果用户输入文件，则只列出此文件的详细信息
        if (long_info) {
            printf("-   %d  %d  %s\n", file_stat.st_ino, file_stat.st_size, pathname);
        } else {
            printf("%s\n", pathname);
        }
    }
}

void buildin_ps(uint32_t argc, char** argv UNUSED)
{
    if (argc != 1) {
        printf("ps: no argument support!\n");
        return;
    }
    ps();
}

void buildin_clear(uint32_t argc, char** argv UNUSED)
{
    if (argc != 1) {
        printf("clear: no argument support!\n");
        return;
    }
    clear();    
}

int32_t buildin_mkdir(uint32_t argc, char** argv)
{
    int32_t ret = -1;
    if (argc != 2) {
        printf("mkdir: only support 1 argument!\n");
    } else {
        make_clear_abs_path(argv[1], final_path);
        if (strcmp("/", final_path)) {
            if (mkdir(final_path) == 0) {
                ret = 0;
            } else {
                printf("mkdir: create directory %s failed.\n", argv[1]);
            }
        }
    }
    return ret;
}

int32_t buildin_rmdir(uint32_t argc, char** argv)
{
    int32_t ret = -1;
    if (argc != 2) {
        printf("rmdir: only support 1 argument!\n");
    } else {
        make_clear_abs_path(argv[1], final_path);
        if (strcmp("/", final_path)) {
            if (rmdir(final_path) == 0) {
                ret = 0;
            } else {
                printf("rmdir: remove directory %s failed.\n", argv[1]);
            }
        }
    }
    return ret;
}

int32_t buildin_rm(uint32_t argc, char** argv)
{
    int32_t ret = -1;
    if (argc != 2) {
        printf("rm: only support 1 argument!\n");
    } else {
        make_clear_abs_path(argv[1], final_path);
        if (strcmp("/", final_path)) {
            if (unlink(final_path) == 0) {
                ret = 0;
            } else {
                printf("rm: delete %s failed.\n", argv[1]);
            }
        }
    }
    return ret;
}