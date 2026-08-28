#!/bin/bash
# GRUB 手工装盘机制的脱险验证：boot.img 写扇区0、core.img 写扇区1，QEMU 无头串口启动
set -e
T=/tmp/gtest
rm -rf "$T"; mkdir -p "$T"
cd "$T"

# 1) 生成 core.img（含 part_msdos, fat, configfile, normal, boot, multiboot2）
grub-mkimage -O i386-pc -d /usr/lib/grub/i386-pc -p /boot/grub \
  -o core.img biosdisk part_msdos fat configfile normal boot multiboot2
echo "core.img size: $(stat -c%s core.img) bytes"

# 2) 建一张 64MB 磁盘
dd if=/dev/zero of=disk.img bs=1M count=64 status=none

# 3) boot.img 写扇区0，core.img 写扇区1
dd if=/usr/lib/grub/i386-pc/boot.img of=disk.img bs=512 count=1 conv=notrunc status=none
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none

# 4) 检查 MBR 签名
xxd -l 2 -s 510 disk.img

# 5) QEMU 无头启动，串口记录
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -serial file:serial.log -no-reboot -no-shutdown &
QPID=$!
sleep 2
kill $QPID 2>/dev/null || true

echo "===== serial.log ====="
cat serial.log
echo "===== end ====="