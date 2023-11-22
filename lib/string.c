#include "string.h"
#include "global.h"
#include "debug.h"

//将dst_起始的size个字节置为value
void memset(void *dst_, uint8_t value, uint32_t size)
{
    ASSERT(dst_ != NULL);
    uint8_t *dst = (uint8_t *)dst_;
    while (size-- > 0) {
        *dst++ = value;
    }
}

//将src_起始的size个字节复制到dst_ 起始要用const修饰
void memcpy(void *dst_, const void *src_, uint32_t size)
{
    ASSERT(src_ != NULL && dst_ != NULL);
    uint8_t *dst = dst_;//这指针直接转不行吗？非得定义个变量？
    const uint8_t *src = src_;
    while (size-- > 0) {
        *dst++ = *src++;
    }
}

int memcmp(const void *a_, const void *b_, uint32_t size)
{
    ASSERT(a_ != NULL && b_ != NULL);
    const uint8_t *a = a_;
    const uint8_t *b = b_;
    while (size-- > 0) {
        if (*a != *b) {
            return *a > *b ? 1 : -1;
        }
        a++;
        b++;
    }

    return 0;
}

char* strcpy(char* dst_, const char* src_)
{
    ASSERT(dst_ != NULL && src_ != NULL);
    char* r = dst_;
    while (*dst_++ = *src_++);//拷贝直到src_结尾\0 \0也会拷贝 因为先执行表达式再判断循环条件
    return r;
}

uint32_t strlen(const char* str)
{
    ASSERT(str != NULL);
    const char *p = str;
    while (*p++);
    return p - str - 1;
}

int8_t strcmp(const char* a, const char* b)
{
    ASSERT(a != NULL && b != NULL);
    while (*a != 0 && *a == *b) {//不管b多长 只要a到头了就比完了 没毛病 可以想象成ab调换
        a++;
        b++;
    }
    return *a < *b ? -1 : *a > *b;//先判断a小于b吗 如果不小于则继续判断大于吗 如果不大于则等于
}

char* strchr(const char* str, const char ch)
{
    ASSERT(str != NULL);
    while (*str != 0) {
        if (*str == ch) {
            return (char*)str;
        }
        str++;//const char*在函数内是副本可以改 但是*p绝不可以改
    }
    return NULL;
}

//太笨了我这写的 还先到尾部再往回找 太笨了 看人家写的
/*
char* strrchr(const char* str, const uint8_t ch)
{
    ASSERT(str != NULL);
    const char* p = str;
    while (*p != 0) {
        p++;
    }
    p--;
    while (p >= str) {
        if (*p == ch) {
            return (char*)p;
        }
        p--;
    }
    return NULL;
}
*/
//太精妙了 从前往后找 每找一个就给last_char 直到找到最后一个为止 这种只需要遍历一遍 我的最坏需要两遍
char* strrchr(const char* str, const uint8_t ch)
{
    ASSERT(str != NULL);
    const char* last_char = NULL;
    while (*str != 0) {
        if (*str == ch) {
            last_char = str;
        }
        str++;
    }
    return (char*)last_char;
}

char* strcat(char* dst_, const char* src_)
{
    ASSERT(dst_ != NULL && src_ != NULL);
    char* str = dst_;
    while (*str++);//因为str最后一个有效字符不是0 但是str还++了 所以指向0 但是到了0又会先执行再判断
    --str;//指回0
    while(*str++ = *src_++);
    return dst_;
}

uint32_t strchrs(const char* str, uint8_t ch)
{
    ASSERT(str != NULL);
    uint32_t ch_cnt = 0;
    const char* p = str;
    while (*p != 0) {
        if (*p == ch) {
            ch_cnt++;
        }
        p++;
    }
    return ch_cnt;
}