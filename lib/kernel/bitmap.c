#include "bitmap.h"
#include "stdint.h"
#include "string.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"

//看这应该是要malloc动态分配bitmap 否则分配在栈上直接{0}初始化就好了
void bitmap_init(struct bitmap* btmp)
{
    memset(btmp->bits, 0, btmp->btmp_bytes_len);
}

//判断位图某位是否为已被占用
bool bitmap_scan_test(struct bitmap* btmp, uint32_t bit_idx)
{
    uint32_t byte_idx = bit_idx / 8;//取出要找的那一位所在字节
    uint32_t bit_odd = bit_idx % 8;//要找的那一位在字节的第几位
    return (btmp->bits[byte_idx] & (BITMAP_MASK << bit_odd));
}

//位图能否跳着分配？那样的话该如何管理呢？
//在位图中连续申请cnt位 成功返回下标 失败返回-1
int bitmap_scan(struct bitmap* btmp, uint32_t cnt)
{
    uint32_t idx_byte = 0;
    //遍历整个bitmap查询是否还有空闲字节
    while (0xff == btmp->bits[idx_byte] && idx_byte < btmp->btmp_bytes_len) {
        idx_byte++;
    }

    ASSERT(idx_byte < btmp->btmp_bytes_len);
    if (idx_byte == btmp->btmp_bytes_len) {
        return -1;
    }

    //从上一步得到的空闲字节中找到第一个空闲的位
    int idx_bit = 0;
    while ((uint8_t)(BITMAP_MASK << idx_bit) & btmp->bits[idx_byte]) {
        idx_bit++;
    }

    int bit_idx_start = idx_byte * 8 + idx_bit;
    if (cnt == 1) {
        return bit_idx_start;
    }

    //从该空闲位的下一位开始连续寻找cnt-1个bit
    uint32_t bit_left = btmp->btmp_bytes_len * 8 - bit_idx_start;
    uint32_t next_bit = bit_idx_start + 1;
    uint32_t count = 1;
    bit_idx_start = -1;
    while (bit_left--) {
        if (!bitmap_scan_test(btmp, next_bit)) {
            count++;
        } else {
            count = 0;
        }
        if (count == cnt) {
            bit_idx_start = next_bit - cnt + 1;
            break;            
        }
        next_bit++;
    }

    return bit_idx_start;
}

void bitmap_set(struct bitmap* btmp, uint32_t bit_idx, uint8_t value)
{
    ASSERT(value == 0 || value == 1);

    uint32_t byte_idx = bit_idx / 8;
    uint32_t bit_odd = bit_idx % 8;

    if (value) {
        btmp->bits[byte_idx] |= (BITMAP_MASK << bit_odd);
    } else {
        btmp->bits[byte_idx] &= ~(BITMAP_MASK << bit_odd);
    }
}