#!/bin/bash
# 用精简 mkstandalone core(330KB) 装盘实测。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
T=/tmp/gbsa
cd "$T"
CORE=c.img
dd if=/dev/zero of=disk.img bs=512 count=8192 status=none
dd if=/usr/lib/grub/i386-pc/boot.img of=disk.img bs=512 conv=notrunc status=none
dd if="$CORE" of=disk.img bs=512 seek=1 conv=notrunc status=none

rm -f sp.in sp.out; mkfifo sp.in sp.out
( cat < sp.out > sp.cap ) & CAP=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
QP=$!
sleep 12
kill "$CAP" 2>/dev/null || true; kill "$QP" 2>/dev/null || true
echo "=== SERIAL bytes: $(wc -c < sp.cap 2>/dev/null) ==="
cat -v sp.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | head -70
echo "=== done ==="