#include "buildin_cmd.h"
#include "assert.h"
#include "string.h"
#include "syscall.h"
#include "fs.h"

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