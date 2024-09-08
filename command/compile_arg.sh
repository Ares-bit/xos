#判断lib和build目录是否存在，.h include时用 .o链接时用
if [[ ! -d "../lib" || ! -d "../build" ]];then
    #` 反引号， shell的引用符号 需要转义
    echo "dependent dir don\`t exist!"
    cwd=$(pwd)
    #${file##*/}：删掉最后一个 /  及其左边的字符串
    cwd=${cwd##*/}
    #${file%/}：删掉最后一个  /
    cwd=${cwd%/}
    #如果当前路径不是command就报错
    if [[ $cwd != "command" ]];then
        #-e开启echo转义
        echo -e "you\`d better in command dir\n"
    fi
    exit
fi

#跟makefile不同，shell这里都要用字符串
BIN="prog_arg"
#-c编译成.o文件
CFLAGS="-Wall -c -fno-builtin -W -Wstrict-prototypes \
    -Wmissing-prototypes -Wsystem-headers"
LIBS="-I ../lib/ -I ../lib/kernel/ -I ../lib/user/ -I ../device/ -I ../kernel/ \
        -I ../thread/ -I ../userprog/ -I ../fs/ -I ../shell/"
OBJS="../build/string.o ../build/syscall.o \
    ../build/stdio.o ../build/assert.o start.o"
DD_IN=$BIN
DD_OUT="/home/xuxingyuan/bochs/hd60M.img"

#将start.S编译为start.o -f指定文件格式为elf
nasm -f elf ./start.S -o ./start.o
#将所有.o打包成静态链接库simple_crt.a ar命令可以用来创建、修改库，也可以从库中提出单个模块。
ar rcs simple_crt.a $OBJS start.o
#-o指定输出文件名
gcc $CFLAGS $LIBS -o $BIN".o" $BIN".c"
#-e指定链接后程序入口 -o指定输出文件名
ld $BIN".o" simple_crt.a -o $BIN
#对编译后的可执行文件执行ls -l
#将结果送入awk，取其第五个参数文件大小加511凑整除以512
#计算文件所占扇区数并打印，将打印结果赋值给SEC_CNT
SEC_CNT=$(ls -l $BIN|awk '{printf("%d", ($5+511)/512)}')

#将可执行文件写入内核所在盘的lba 300处
if [[ -f $BIN ]];then
    dd if=./$DD_IN of=$DD_OUT bs=512 \
    count=$SEC_CNT seek=300 conv=notrunc
fi