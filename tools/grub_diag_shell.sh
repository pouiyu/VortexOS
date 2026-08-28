#!/bin/bash
# 诊断：从磁盘引导进 GRUB 交互壳后，逐条发命令探测分区/文件可读性。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gbdiag
rm -rf "$T"; mkdir -p "$T"; cd "$T"
cp "$SRC/vortexos.iso" iso.iso
# 若已有安装好的盘则以磁盘引导为主；这里直接生成并安装
dd if=/dev/zero of=disk.img bs=1M count=64 status=none

# Phase 1: 安装
rm -f sp.in sp.out; mkfifo sp.in sp.out
( cat < sp.out > p1.cap ) & cata=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -cdrom iso.iso -boot d \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
qp1=$!
sleep 12; kill "$cata" 2>/dev/null; kill "$qp1" 2>/dev/null; wait 2>/dev/null

echo "=== Phase1 captured ==="; grep -E 'INSTALL|GRUB|FAT32' p1.cap | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | head

# Phase 2: 交互 GRUB 探测
rm -f sp.in sp.out; mkfifo sp.in sp.out
exec 3<>sp.in
( cat < sp.out > p2.cap ) & catb=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
qp2=$!
sleep 2
SEND() { S=$1; S=${S//$'\n'/}; printf '%s\r' "$S" >&3; sleep 1; }
SEND "set pager=1"
SEND "lsmod"
SEND "insmod part_msdos"
SEND "ls"
SEND "ls (hd0)"
SEND "ls (hd0,msdos1)"
SEND 'cat (hd0,msdos1)/boot/grub/grub.cfg'
SEND "set root=(hd0,msdos1)"
SEND "set prefix=(hd0,msdos1)/boot/grub"
SEND "ls /boot/grub/"
SEND "multiboot2 /boot/kernel.bin"
SEND "boot"
sleep 8
kill "$catb" 2>/dev/null; kill "$qp2" 2>/dev/null; wait 2>/dev/null

echo "================= GRUB interaction log ================="
cat -v p2.cap | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -av $'^\r$' | tail -80
echo "================= done ================="