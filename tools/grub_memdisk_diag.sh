#!/bin/bash
# 诊断：memdisk core.img 引导无输出问题, 加长时间+GRUB debug。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
T=/tmp/gbfull
cd "$T"
# 重新组盘确保完整
dd if=/dev/zero of=disk.img bs=512 count=8192 status=none
dd if=/usr/lib/grub/i386-pc/boot.img of=disk.img bs=512 conv=notrunc status=none
dd if=/tmp/gbmi/c_v1.img of=disk.img bs=512 seek=1 conv=notrunc status=none
echo "disk.img size=$(stat -c%s disk.img)"

rm -f sp.in sp.out; mkfifo sp.in sp.out
( cat < sp.out > sp.cap ) & CAP=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 \
  -no-reboot -no-shutdown -d guest_errors 2>qemu.err &
QP=$!
sleep 15
kill "$CAP" 2>/dev/null || true
kill "$QP" 2>/dev/null || true

echo "=== sp.cap raw(bin) size: $(wc -c < sp.cap 2>/dev/null)"
cat -v sp.cap 2>/dev/null | head -40
echo "=== qemu.err ==="; head -20 qemu.err 2>/dev/null
echo done