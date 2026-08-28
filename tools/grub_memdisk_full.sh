#!/bin/bash
# 端到端：boot.img + 自包含 memdisk core.img 装进硬盘，无光驱从硬盘引导。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gbfull; rm -rf "$T"; mkdir -p "$T"/src/boot/grub; cd "$T"

BOOT=$(ls /usr/lib/grub/i386-pc/boot.img)
CORE=/tmp/gbmi/c_v1.img   # 已验证的 memdisk core.img(202KB)

if [ ! -s "$CORE" ]; then
  echo "core.img 缺失, 先跑 grub_mkimage_proto.sh"; exit 1
fi

# 组盘: 扇区0=boot.img, 扇区1起=core.img
dd if=/dev/zero of=disk.img bs=512 count=4096 status=none
dd if="$BOOT" of=disk.img bs=512 conv=notrunc status=none
# boot.img 里 core LBA 字段默认=1, 不用改
dd if="$CORE" of=disk.img bs=512 seek=1 conv=notrunc status=none

# 无头串口捕获(Pipe)
rm -f sp.in sp.out; mkfifo sp.in sp.out
( cat < sp.out > sp.cap ) & CAP=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 \
  -no-reboot -no-shutdown &
QP=$!
sleep 8
kill "$CAP" 2>/dev/null || true
kill "$QP" 2>/dev/null || true

echo "================= SERIAL (GRUB->kernel) ================="
cat -v sp.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | head -60
echo "================= done ================="