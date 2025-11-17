# 工具链配置
TOOLCHAIN = riscv64-unknown-elf-
CC = $(TOOLCHAIN)gcc
LD = $(TOOLCHAIN)ld
OBJCOPY = $(TOOLCHAIN)objcopy

# 编译选项
CFLAGS = -Wall -O2 -fno-omit-frame-pointer -ggdb
CFLAGS += -march=rv64g -mabi=lp64d
CFLAGS += -mcmodel=medany -ffreestanding -nostdlib
CFLAGS += -Ikernel/

# 源文件
KERNEL_SRCS = \
        kernel/entry.S \
		kernel/kernelvec.S \
	    kernel/trampoline.S \
		kernel/swtch.S \
        kernel/start.c \
        kernel/uart.c \
		kernel/printf.c \
		kernel/console.c \
		kernel/kalloc.c \
		kernel/vm.c \
		kernel/string.c \
		kernel/spinlock.c \
		kernel/trap.c \
		kernel/proc.c \
		kernel/process_api.c \
        kernel/main.c

# 目标文件
OBJS = $(patsubst %.S,%.o,$(patsubst %.c,%.o,$(KERNEL_SRCS)))

# 默认目标
all: kernel.bin

# 编译规则
%.o: %.S
		$(CC) $(CFLAGS) -c $< -o $@	

%.o: %.c
		$(CC) $(CFLAGS) -c $< -o $@

# 链接内核
kernel.elf: $(OBJS) kernel.ld
		$(LD) -T kernel.ld -o $@ $(OBJS)

# 生成原始二进制
kernel.bin: kernel.elf
		$(OBJCOPY) -O binary $< $@

# 清理
clean:
		rm -f kernel.elf kernel.bin $(OBJS)

# 运行QEMU
run: kernel.bin
		qemu-system-riscv64 -machine virt -bios none -kernel kernel.bin -nographic