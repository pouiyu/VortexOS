# 编译器与参数
CC = gcc
CFLAGS = -m32 -Wall -Wextra -std=c99 -ffreestanding \
         -nostdlib -nostartfiles -nodefaultlibs \
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

# 自动扫描源文件
C_SRCS = $(shell find $(SRC_DIR) -name "*.c")
ASM_SRCS = $(shell find $(SRC_DIR) -name "*.asm")
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SRCS)) \
       $(patsubst $(SRC_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SRCS))

# 目标文件
TARGET = kernel.bin
ISO_TARGET = vortexos.iso

# QEMU参数
QEMU = qemu-system-x86_64
QEMU_FLAGS = -cdrom $(ISO_TARGET) -hda disk.img -m 256M -boot d -cpu qemu64 -smp 1 -vga std -no-reboot
QEMU_DEBUG = -s -S -d int -D qemu.log
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

# 创建 eltorito 镜像
$(ISO_DIR)/boot/grub/eltorito.img:
	@mkdir -p $(GRUB_DIR)
	grub-mkimage -O i386-pc -p /boot/grub -o $@ \
		biosdisk iso9660 part_msdos fat configfile \
		search_fs_uuid search_label search \
		normal boot minicmd ls cat echo test

# 构建ISO镜像
$(ISO_TARGET): $(TARGET) $(ISO_DIR)/boot/grub/eltorito.img
	@mkdir -p $(GRUB_DIR)
	cp $(TARGET) $(BOOT_DIR)/
	cp grub.cfg $(GRUB_DIR)/
	grub-mkrescue -o $@ $(ISO_DIR)

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

.PHONY: all clean run run-kvm debug debug-kvm run-noaudio