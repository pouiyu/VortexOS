# 编译器与参数
CC = gcc
CFLAGS = -m32 -Wall -Wextra -std=c99 -ffreestanding \
         -nostdlib -nostartfiles -nodefaultlibs \
         -nostdinc \
         -I src/lib \
         -I src/include \
         -I src/drivers \
         -I src/kernel
LD = ld
LDFLAGS = -m elf_i386 -T linker.ld
AS = nasm
ASFLAGS = -f elf32

# 目录结构
SRC_DIR = src
BUILD_DIR = build
ISO_DIR = iso
BOOT_DIR = $(ISO_DIR)/boot
GRUB_DIR = $(BOOT_DIR)/grub

# 自动扫描源文件(src/user 为用户态程序, 独立 ELF, 不链接进内核)
C_SRCS = $(shell find $(SRC_DIR) -name "*.c" ! -path "$(SRC_DIR)/user/*")
ASM_SRCS = $(shell find $(SRC_DIR) -name "*.asm")
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SRCS)) \
       $(patsubst $(SRC_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SRCS))

# ===== 用户态 GUI 程序(独立 ELF)=====
# 链接地址 0x400000(见 user.ld)，[4M,8M) 已在 pmm.c 保留给用户程序。
USER_CFLAGS = -m32 -Wall -Wextra -std=c99 -ffreestanding \
              -fno-pie -fno-pic -fno-stack-protector \
              -nostdlib -nostartfiles -nodefaultlibs \
              -nostdinc \
              -I src/lib \
              -I src/include \
              -I src/drivers \
              -I src/kernel

# ===== 通用用户程序构建: make program NAME=myapp → system/programs/myapp.elf =====
# 任意 src/user/X.c 自动链接 libgui + libwidget(设计器生成的代码也能直接编译)。
# 用法(须在 WSL 内或经 wsl 委托):  make program NAME=hello_ui
$(BUILD_DIR)/user/%.o: $(SRC_DIR)/user/%.c
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

system/programs/%.elf: $(SRC_DIR)/user/%.c $(BUILD_DIR)/user/libgui.o $(BUILD_DIR)/user/libwidget.o
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/user/$*.o
	$(LD) -m elf_i386 -T user.ld $(BUILD_DIR)/user/$*.o $(BUILD_DIR)/user/libgui.o $(BUILD_DIR)/user/libwidget.o -o $@

.PHONY: program
program: system/programs/$(NAME).elf
	@echo "UI_BUILD_OK -> system/programs/$(NAME).elf"

# 目标文件
TARGET = kernel.bin
ISO_TARGET = vortexos.iso

# QEMU参数
QEMU = qemu-system-x86_64
# 用 i440fx 以便光驱/硬盘走 legacy IDE 端口(ATA @0x1F0, ATAPI @0x170)，PIO 方式兼容性最好
QEMU_FLAGS = -cdrom $(ISO_TARGET) -hda disk.img -m 256M -boot d -machine pc -cpu qemu64 -smp 2 -vga std -d int -D qemu.log -serial stdio -accel tcg,thread=multi
QEMU_DEBUG = -s -S -d int -D qemu.log  -no-shutdown  -no-reboot
QEMU_KVM = -enable-kvm -cpu host

# 音频参数 - 输出到 WAV 文件（用于测试）
AUDIO_FLAGS = 

# 默认目标
all: $(ISO_TARGET)

# 编译C文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# 编译汇编文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

# 链接生成内核
$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

# ===== GRUB 引导与 ISO 打包(全部在 WSL 内完成)=====
# 本 Makefile 只在 WSL 内运行(Windows 侧入口统一为 `wsl -- bash tools/build_wsl.sh`)。
# build_wsl.sh 会自举编译内核并打包：CD eltorito.img、硬盘 memdisk core.img、
# hdd_mbr.bin(boot.img+分区表)，以及把整个 system 运行目录(含字体)拷入 CD。
$(ISO_TARGET): $(TARGET)
	bash tools/build_wsl.sh

# 仅生成硬盘引导所需的 core.img + hdd_mbr.bin, 供快速调试(不进 ISO 打包)
grub-hdd:
	cd tools && bash build_wsl.sh

# 清理
clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(ISO_TARGET) $(ISO_DIR)

# 运行（带 ALSA 音频）
run: $(ISO_TARGET)
	$(QEMU) $(QEMU_FLAGS) $(AUDIO_FLAGS)

# 运行（KVM 加速 + 音频）
run-kvm: $(ISO_TARGET)
	$(QEMU) $(QEMU_FLAGS) $(QEMU_KVM) $(AUDIO_FLAGS)

# 运行（调试模式 + 音频）
debug: $(ISO_TARGET)
	$(QEMU) $(QEMU_FLAGS) $(AUDIO_FLAGS) $(QEMU_DEBUG)

# 运行（无音频）
run-noaudio: $(ISO_TARGET)
	$(QEMU) $(QEMU_FLAGS)

# 运行（调试 + KVM）
debug-kvm: $(ISO_TARGET)
	$(QEMU) $(QEMU_FLAGS) $(QEMU_KVM) $(QEMU_DEBUG)

# 运行（无显示，串口输出，用于无头测试）
run-headless: $(ISO_TARGET)
	$(QEMU) -cdrom $(ISO_TARGET) -m 256M -boot d -machine pc -cpu qemu64 -smp 1 -no-reboot -no-shutdown -display none -serial stdio -device qemu-xhci -device usb-kbd -device usb-mouse

.PHONY: all clean run run-kvm debug debug-kvm run-noaudio run-headless grub-hdd