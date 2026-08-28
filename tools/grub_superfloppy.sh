#!/bin/bash
# hd0=GRUB 诊断核, hd1=无分区 superfloppy FAT32(带标记文件)。
# 若 ls (hd1) 能挂载 FAT 并列出标记文件 => biosdisk 读 LBA0 正常 => part_msdos 拒绝我们的条目；
# 若 unknown/读不到 => biosdisk 读 LBA0 不可靠。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
DBG=/tmp/gbdbg/dbgcore.img
T=/tmp/gbsf
rm -rf "$T"; mkdir -p "$T"; cd "$T"

# hd0: GRUB 引导
dd if=/dev/zero of=grub.img bs=1M count=4 status=none
dd if="$SRC/iso/grub/hdd_mbr.bin" of=grub.img conv=notrunc status=none
dd if="$DBG" of=grub.img bs=512 seek=1 conv=notrunc status=none

# hd1: superfloppy FAT32, 无分区表
dd if=/dev/zero of=sf.img bs=1M count=64 status=none
mkfs.vfat -F 32 sf.img >/dev/null 2>&1
MT=/tmp/gbsf/mt; mkdir -p "$MT"
export MTOOLSRC=/dev/null
mcopy -i sf.img <(echo 'HELLO-SUPERFLOPPY-OK') ::/MARKER.TXT 2>/dev/null || true
# 直接写标记到镜像某扇区(FS 根目录)较麻烦，改用 mcopy 若上述失败则跳过

rm -f sp.in sp.out; mkfifo sp.in sp.out
exec 4<>sp.in
( cat < sp.out > o.txt ) & cb=$!
qemu-system-x86_64 \
  -drive file=grub.img,format=raw,if=ide \
  -drive file=sf.img,format=raw,if=ide \
  -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
qq=$!
sleep 2
Q(){ printf '%s\r' "$1" >&4; sleep 1; }
Q "insmod part_msdos"
Q "insmod fat"
Q "ls"
Q "ls (hd1)"
Q "ls (hd1) 2>/dev/null"
Q "set root=(hd1)"
Q "ls /"
Q "cat /MARKER.TXT"
sleep 4
kill "$cb" 2>/dev/null; kill "$qq" 2>/dev/null; wait 2>/dev/null
echo "===== log ====="
cat -v o.txt | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -av '^$' | tail -60
echo "===== done ====="