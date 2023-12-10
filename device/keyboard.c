#include "keyboard.h"
#include "print.h"
#include "io.h"
#include "interrupt.h"
#include "stdint.h"
#include "global.h"

#define KBD_BUF_PORT 0x60

//用转义字符定义控制字符
#define esc         '\033'
#define backspace   '\b'
#define tab         '\t'
#define enter       '\r'
#define delete      '\177'

//定义不可见字符，这类字符没有ASCII码，只有字符控制键才有ASCII码
#define char_invisible  0
#define ctrl_l_char     char_invisible
#define ctrl_r_char     char_invisible
#define shift_l_char    char_invisible
#define shift_r_char    char_invisible
#define alt_l_char      char_invisible
#define alt_r_char      char_invisible
#define caps_lock_char  char_invisible

//定义控制字符通断码
#define shift_l_make    0x2a
#define shift_r_make    0x36
#define alt_l_make      0x38
#define alt_r_make      0xe038
#define alt_r_break     0xe0b8
#define ctrl_l_make     0x1d
#define ctrl_r_make     0xe01d
#define ctrl_r_break    0xe09d
#define caps_lock_make  0x3a

//全局变量用于记录以下键是否被按下
static bool ctrl_status, shift_status, caps_lock_status = false, alt_status;

//用于记录是否有0xe0前缀
static bool ext_scancode;

//以通码为索引，左一列表示未与shift组合的ASCII，右一列表示与shift组合后的ASCII
static char keymap[][2] = {
    {0, 0},
    {esc, esc},
    {'1', '!'},
    {'2', '@'},
    {'3', '#'},
    {'4', '$'},
    {'5', '%'},
    {'6', '^'},
    {'7', '&'},
    {'8', '*'},
    {'9', '('},
    {'0', ')'},
    {'-', '_'},
    {'=', '+'},
    {backspace, backspace},
    {tab, tab},
    {'q', 'Q'},
    {'w', 'W'},
    {'e', 'E'},
    {'r', 'R'},
    {'t', 'T'},
    {'y', 'Y'},
    {'u', 'U'},
    {'i', 'I'},
    {'o', 'O'},
    {'p', 'P'},
    {'[', '{'},
    {']', '}'},
    {enter, enter},
    {ctrl_l_char, ctrl_l_char},
    {'a', 'A'},
    {'s', 'S'},
    {'d', 'D'},
    {'f', 'F'},
    {'g', 'G'},
    {'h', 'H'},
    {'j', 'J'},
    {'k', 'K'},
    {'l', 'L'},
    {';', ':'},
    {'\'', '"'},
    {'`', '~'},
    {shift_l_char, shift_l_char},
    {'\\', '|'},
    {'z', 'Z'},
    {'x', 'X'},
    {'c', 'C'},
    {'v', 'V'},
    {'b', 'B'},
    {'n', 'N'},
    {'m', 'M'},
    {',', '<'},
    {'.', '>'},
    {'/', '?'},
    {shift_r_char, shift_r_char},
    {'*', '*'},
    {alt_l_char, alt_l_char},
    {' ', ' '},
    {caps_lock_char, caps_lock_char}
};

static void intr_keyboard_handler(void)
{
    //put_str("I love U ");
    bool ctrl_down_last = ctrl_status;
    bool alt_down_last = alt_status;
    bool shift_down_last = shift_status;
    bool caps_lock_last = caps_lock_status;

    bool break_code;

    //读出扫描码，如果是0xe0前缀则标记
    uint16_t scancode = inb(KBD_BUF_PORT);
    if (scancode == 0xe0) {
        ext_scancode = true;
        return;
    }

    //如果前一个输入是0xe0，则给本次扫描码前加上0xe0前缀，并清标记
    if (ext_scancode) {
        scancode = (0xe000) | scancode;
        ext_scancode = false;
    }
    
    //ctrl shift alt弹起才算关闭，就算长按过程中输入别的键也不清status
    //判断本次输入是通码还是断码，看低字节最高位即可判断
    break_code = (((0x0080) & scancode) != 0);
    if (break_code) {
        //由于keymap中只有通码，所以要把断码转成通码后查表处理
        uint16_t make_code = (scancode &= 0xff7f);
        if (make_code == ctrl_l_make || make_code == ctrl_r_make) {
            ctrl_status = false;
        } else if (make_code == shift_l_make || make_code == shift_r_make) {
            shift_status = false;
        } else if (make_code == alt_l_make || make_code == alt_r_make){
            alt_status = false;
        }
        return;
    //如果本次收到的是通码，则判断范围，仅支持低字节是0x01-0x3a之间的通码
    } else if ((scancode > 0x00 && scancode < 0x3b) || \
                    (scancode == alt_r_make) || \
                    (scancode == ctrl_r_make)) {
        bool shift = false;
        //如果是非字母的可见字符和shift一起按下，则将shift置true，这些字符只有shift才能影响，caps无效
        if ((scancode < 0x0e) || (scancode == 0x29) || (scancode == 0x1a) || \
                    (scancode == 0x1b) || (scancode == 0x2b) || (scancode == 0x27) || \
                    (scancode == 0x28) || (scancode == 0x33) || (scancode == 0x34) || \
                    (scancode == 0x35)) {
            if (shift_down_last == true) {
                shift = true;
            }
        //如果是可见英文字母，则处理shift和caps lock
        } else {
            //如果shift和caps一起按下，则效果抵消，无大写
            if (shift_down_last && caps_lock_last) {
                shift = false;
            } else if (shift_down_last || caps_lock_last) {
                shift = true;
            } else {
                shift = false;
            }
        }
        //rshift和ralt也会走到这里，清掉最高字节，使它们查表索引范围正确，会取出对应的lshift和lalt
        uint8_t index = (scancode &= 0x00ff);

        char cur_char = keymap[index][shift];
        //rshift和ralt也会走到这里，但是它们查表会取出对应的lshift和lalt，它们宏定义的ASCII码0
        if (cur_char) {
            put_char(cur_char);
            return;
        }

        //如果按下ctrl shift alt则置位标记，如果按下caps，则将标志取反，供下次输入使用
        if (scancode == ctrl_l_make || scancode == ctrl_r_make) {
            ctrl_status = true;
        } else if (scancode == shift_l_make || scancode == shift_r_make) {
            shift_status = true;
        } else if (scancode == alt_l_make || scancode == alt_r_make){
            alt_status = true;
        } else if (scancode == caps_lock_make) {
            caps_lock_status = !caps_lock_status;
        }
    } else {
        put_str("unknow key\n");
    }
}

void keyboard_init()
{
    put_str("keyboard_init start\n");
    register_handler(0x21, intr_keyboard_handler);
    put_str("keyboard_init done\n");
}