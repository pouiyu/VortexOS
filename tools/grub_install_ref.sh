#!/bin/bash
# 对照组：标准 grub-install 生成的盘，在完全相同 QEMU 下探测 part_msdos 能否枚举 msdos1。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
T=/tmp/gbref
rm -rf "$T"; mkdir -p "$T"; cd "$T"
dd if=/dev/zero of=ref.img bs=1M count=64 status=none
# 单个 FAT32 分区自 2048 起（与我们的布局一致）
echo '2048, +, c' | sfdisk --force ref.img >/dev/null 2>&1
LOOP=$(losetup --find --show -P ref.img)
echo "loop=$LOOP"
mkfs.vfat -F 32 "${LOOP}p1" >/dev/null 2>&1
mkdir -p mnt
mount "${LOOP}p1" mnt
grub-install --target=i386-pc --boot-directory=mnt/boot "${LOOP}" >grubinstall.log 2>&1
echo "grub-install rc=$?"
ls -la mnt/boot/grub/ | head
umount mnt
losetup -d "$LOOP"

rm -f sp.in sp.out; mkfifo sp.in sp.out
exec 4<>sp.in
( cat < sp.out > o.txt ) & cb=$!
qemu-system-x86_64 -drive file=ref.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
qq=$!
sleep 3
printf 'insmod part_msdos\r' >&4
printf 'insmod serial\r' >&4   # 保险
printf 'terminal_output serial\r' >&4
printf 'ls\r' >&4
printf 'ls (hd0)\r' >&4
printf 'ls (hd0,msdos1)\r' >&4
sleep 5
kill "$cb" 2>/dev/null; kill "$qq" 2>/dev/null; wait 2>/dev/null
echo "===== reference disk GRUB log ====="
cat -v o.txt | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -av '^$' | tail -60
echo "===== done ====="