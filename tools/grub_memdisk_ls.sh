#!/bin/bash
# 交互查 (memdisk) 结构: 复用已验证 core。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
T=/tmp/gbcpio
cd "$T"
CORE=$(ls core_*.img 2>/dev/null | head -1)
dd if=/dev/zero of=disk.img bs=512 count=8192 status=none
dd if=/usr/lib/grub/i386-pc/boot.img of=disk.img bs=512 conv=notrunc status=none
dd if="$CORE" of=disk.img bs=512 seek=1 conv=notrunc status=none

rm -f sp.in sp.out; mkfifo sp.in sp.out
( cat < sp.out > sp.cap ) & CAP=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
QP=$!
sleep 6
# 通过 sp.in 发命令
exec 3<> sp.in
printf 'ls (memdisk)\r' >&3; sleep 2
printf 'ls (memdisk)/boot\r' >&3; sleep 2
printf 'ls (memdisk)/\r' >&3; sleep 2
printf 'cat (memdisk)/boot/grub/grub.cfg\r' >&3; sleep 2
printf 'multiboot2 (memdisk)/boot/kernel.bin\r' >&3; sleep 2
exec 3<&-
sleep 3
kill "$CAP" 2>/dev/null || true; kill "$QP" 2>/dev/null || true
echo "=== SERIAL ==="
cat -v sp.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | head -80
echo "=== done ==="