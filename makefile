BUILD_DIR = ./build
ENTRY_POINT = 0xc0001500
AS = nasm
CC = gcc
LD = ld
LIBS = -I lib/ -I lib/kernel/ -I lib/user/ -I device/ -I kernel/ -I thread/ -I userprog/ -I fs/ \
	   -I shell/
ASFLAGS = -f elf
ASBINLIB = -I boot/include/
CFLAGS = -m32 -Wall $(LIBS) -c -fno-builtin -W -Wstrict-prototypes \
		 -Wmissing-prototypes
LDFLAGS = -melf_i386 -Ttext $(ENTRY_POINT) -e main -Map $(BUILD_DIR)/kernel.map
OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/init.o $(BUILD_DIR)/interrupt.o \
	$(BUILD_DIR)/timer.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/print.o \
	$(BUILD_DIR)/debug.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/bitmap.o \
	$(BUILD_DIR)/string.o $(BUILD_DIR)/thread.o $(BUILD_DIR)/list.o \
    $(BUILD_DIR)/switch.o  $(BUILD_DIR)/console.o $(BUILD_DIR)/sync.o \
	$(BUILD_DIR)/keyboard.o $(BUILD_DIR)/ioqueue.o $(BUILD_DIR)/tss.o \
	$(BUILD_DIR)/process.o $(BUILD_DIR)/syscall.o $(BUILD_DIR)/syscall_init.o \
	$(BUILD_DIR)/stdio.o $(BUILD_DIR)/stdio_kernel.o $(BUILD_DIR)/ide.o \
	$(BUILD_DIR)/fs.o $(BUILD_DIR)/inode.o $(BUILD_DIR)/file.o $(BUILD_DIR)/dir.o \
	$(BUILD_DIR)/fork.o $(BUILD_DIR)/shell.o $(BUILD_DIR)/assert.o \
	$(BUILD_DIR)/buildin_cmd.o $(BUILD_DIR)/exec.o $(BUILD_DIR)/wait_exit.o \
	$(BUILD_DIR)/pipe.o

#mbr编译
$(BUILD_DIR)/mbr.bin: boot/mbr.S
	$(AS) $(ASBINLIB) $< -o $@

#loader编译
$(BUILD_DIR)/loader.bin: boot/loader.S
	$(AS) $(ASBINLIB) $< -o $@

#C编译
$(BUILD_DIR)/main.o: kernel/main.c lib/kernel/print.h \
	lib/stdint.h kernel/init.h kernel/debug.h thread/thread.h \
	kernel/interrupt.h device/console.h device/keyboard.h device/ioqueue.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/init.o: kernel/init.c kernel/init.h lib/kernel/print.h \
	lib/stdint.h kernel/interrupt.h device/timer.h thread/thread.h \
	device/console.h device/keyboard.h userprog/tss.h kernel/memory.h \
	userprog/syscall_init.h device/ide.h fs/fs.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/interrupt.o: kernel/interrupt.c kernel/interrupt.h \
	lib/stdint.h kernel/global.h lib/kernel/io.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/timer.o: device/timer.c device/timer.h lib/stdint.h \
	lib/kernel/io.h lib/kernel/print.h kernel/debug.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/debug.o: kernel/debug.c kernel/debug.h lib/kernel/print.h \
	lib/stdint.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/bitmap.o: lib/kernel/bitmap.c lib/kernel/bitmap.h \
	lib/stdint.h lib/string.h lib/kernel/print.h kernel/interrupt.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/string.o: lib/string.c lib/string.h kernel/global.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/memory.o: kernel/memory.c kernel/memory.h lib/stdint.h \
	lib/kernel/print.h thread/sync.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/thread.o: thread/thread.c thread/thread.h lib/stdint.h lib/string.h \
	kernel/global.h kernel/memory.h lib/kernel/list.h kernel/debug.h lib/kernel/print.h \
	userprog/process.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/list.o: lib/kernel/list.c lib/kernel/list.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/console.o: device/console.c device/console.h lib/stdint.h thread/thread.h \
	lib/kernel/print.h thread/sync.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/sync.o: thread/sync.c thread/sync.h lib/stdint.h lib/kernel/list.h \
	kernel/interrupt.h thread/thread.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/keyboard.o: device/keyboard.c device/keyboard.h kernel/interrupt.h \
	lib/kernel/print.h lib/kernel/io.h lib/stdint.h kernel/global.h device/ioqueue.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/ioqueue.o: device/ioqueue.c device/ioqueue.h kernel/global.h kernel/debug.h \
	kernel/interrupt.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/tss.o: userprog/tss.c userprog/tss.h kernel/global.h lib/kernel/print.h lib/stdint.h \
	lib/string.h thread/thread.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/process.o: userprog/process.c userprog/process.h kernel/global.h kernel/memory.h \
	lib/stdint.h lib/string.h thread/thread.h lib/kernel/bitmap.h device/console.h kernel/debug.h \
	kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/syscall.o: lib/user/syscall.c lib/user/syscall.h lib/stdint.h thread/thread.h \
	fs/dir.h fs/fs.h userprog/exec.h userprog/wait_exit.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/syscall_init.o: userprog/syscall_init.c thread/thread.h lib/stdint.h lib/string.h \
	lib/user/syscall.h device/console.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/stdio.o: lib/stdio.c lib/stdio.h kernel/global.h lib/user/syscall.h lib/stdint.h lib/string.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/stdio_kernel.o: lib/kernel/stdio_kernel.c lib/kernel/stdio_kernel.h kernel/global.h \
	device/console.h lib/stdio.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/ide.o: device/ide.c device/ide.h kernel/global.h lib/kernel/stdio_kernel.h kernel/global.h \
	lib/stdio.h lib/stdint.h lib/kernel/io.h thread/sync.h kernel/memory.h device/console.h lib/stdio.h \
	kernel/debug.h device/timer.h lib/string.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/fs.o: fs/fs.c fs/fs.h fs/inode.h fs/dir.h lib/stdint.h kernel/global.h thread/thread.h \
	lib/kernel/stdio_kernel.h device/ide.h lib/string.h kernel/debug.h kernel/memory.h device/keyboard.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/inode.o: fs/inode.c fs/inode.h device/ide.h fs/fs.h lib/stdint.h kernel/debug.h thread/thread.h \
	lib/string.h lib/kernel/list.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/file.o: fs/file.c fs/file.h fs/fs.h lib/kernel/stdio_kernel.h thread/thread.h lib/kernel/bitmap.h \
	kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/dir.o: fs/dir.c fs/dir.h fs/file.h fs/inode.h lib/kernel/stdio_kernel.h device/ide.h \
	lib/string.h kernel/debug.h kernel/memory.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/fork.o: userprog/fork.c userprog/fork.h lib/string.h kernel/debug.h thread/thread.h \
	kernel/memory.h fs/fs.h kernel/interrupt.h userprog/process.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/shell.o: shell/shell.c shell/shell.h lib/string.h lib/user/assert.h lib/user/syscall.h \
	lib/stdio.h fs/file.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/assert.o: lib/user/assert.c lib/user/assert.h lib/stdio.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/buildin_cmd.o: shell/buildin_cmd.c shell/buildin_cmd.h fs/fs.h lib/string.h lib/user/syscall.h \
	lib/user/assert.h lib/stdio.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/exec.o: userprog/exec.c userprog/exec.h lib/stdint.h thread/thread.h kernel/memory.h fs/fs.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/wait_exit.o: userprog/wait_exit.c userprog/wait_exit.h thread/thread.h kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/pipe.o: shell/pipe.c shell/pipe.h lib/stdint.h device/ioqueue.h fs/fs.h fs/file.h
	$(CC) $(CFLAGS) $< -o $@

#汇编编译
$(BUILD_DIR)/kernel.o: kernel/kernel.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/print.o: lib/kernel/print.S
	$(AS) $(ASBINLIB) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/switch.o: thread/switch.S
	$(AS) $(ASFLAGS) $< -o $@

#链接
$(BUILD_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

.PHONY:	mk_dir hd clean all

mk_dir:
	if [ ! -d $(BUILD_DIR) ];then mkdir $(BUILD_DIR);fi

hd:
	dd if=$(BUILD_DIR)/mbr.bin of=/home/xuxingyuan/bochs/hd60M.img \
		bs=512 count=1  conv=notrunc
	dd if=$(BUILD_DIR)/loader.bin of=/home/xuxingyuan/bochs/hd60M.img \
		bs=512 count=4 seek=2 conv=notrunc
	dd if=$(BUILD_DIR)/kernel.bin \
		of=/home/xuxingyuan/bochs/hd60M.img \
		bs=512 count=200 seek=9 conv=notrunc

ctags:
	ctags -R

clean:
	cd $(BUILD_DIR) && rm -f ./*

build: $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/loader.bin

all: mk_dir build hd ctags