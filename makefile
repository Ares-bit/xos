BUILD_DIR = ./build
ENTRY_POINT = 0xc0001500
AS = nasm
CC = gcc
LD = ld
LIBS = -I lib/ -I lib/kernel/ -I lib/user -I device/ -I boot/include/ -I kernel/ -I thread/
ASFLAGS = $(LIBS) -f elf
CFLAGS = -Wall $(LIBS) -c -fno-builtin -W -Wstrict-prototypes \
		 -Wmissing-prototypes
LDFLAGS = -Ttext $(ENTRY_POINT) -e main -Map $(BUILD_DIR)/kernel.map
OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/init.o $(BUILD_DIR)/interrupt.o \
	   $(BUILD_DIR)/timer.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/print.o \
	   $(BUILD_DIR)/debug.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/bitmap.o \
	   $(BUILD_DIR)/string.o $(BUILD_DIR)/thread.o

#C编译
$(BUILD_DIR)/main.o: kernel/main.c lib/kernel/print.h \
	lib/stdint.h kernel/init.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/init.o: kernel/init.c kernel/init.h lib/kernel/print.h \
	lib/stdint.h kernel/interrupt.h device/timer.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/interrupt.o: kernel/interrupt.c kernel/interrupt.h \
	lib/stdint.h kernel/global.h lib/kernel/io.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/timer.o: device/timer.c device/timer.h lib/stdint.h \
	lib/kernel/io.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/debug.o: kernel/debug.c kernel/debug.h lib/kernel/print.h \
	lib/stdint.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/bitmap.o: lib/kernel/bitmap.c lib/stdint.h lib/string.h \
	lib/kernel/print.h kernel/interrupt.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/string.o: lib/string.c kernel/global.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/memory.o: kernel/memory.c kernel/memory.h lib/stdint.h \
	lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/thread.o: thread/thread.c lib/stdint.h lib/string.h kernel/global.h \
	kernel/memory.h
	$(CC) $(CFLAGS) $< -o $@

#汇编编译
$(BUILD_DIR)/kernel.o: kernel/kernel.s
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/print.o: lib/kernel/print.s
	$(AS) $(ASFLAGS) $< -o $@

#链接
$(BUILD_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

.PHONY:
	mk_dir hd clean all

mk_dir:
	if [[ ! -d $(BUILD_DIR) ]];then mkdir $(BUILD_DIR);fi

hd:
	dd if=$(BUILD_DIR)/kernel.bin \
		of=/home/xuxingyuan/bochs/hd60M.img \
		bs=512 count=200 seek=9 conv=notrunc

clean:
	cd $(BUILD_DIR) && rm -f ./*

build: $(BUILD_DIR)/kernel.bin

all: mk_dir build hd