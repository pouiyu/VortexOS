#!/bin/bash
# v19诊断：审查 installSystem 落盘结果 + 交互验证
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gb19
rm -rf "$T"; mkdir -p "$T"
cd "$T"
cp "$SRC/vortexos.iso" iso.iso
dd if=/dev/zero of=disk.img bs=1M count=64 status=none

# ---- Phase 1 install ----
rm -f sp.in sp.out; mkfifo sp.in sp.out
( cat < sp.out > cap_p1 ) &
CAT=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -cdrom iso.iso -boot d \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
QP=$!
sleep 9; kill $CAT 2>/dev/null; kill $QP 2>/dev/null

echo "===== install 日志 ====="
cat -v cap_p1 | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'GRUB|INSTALL|FONT|FAT32|Vortex' | head -40

echo "===== 磁盘扇区 0 (前48字节+boot code首6字节+MBR签名/分区项) ====="
xxd -l 16 -s 0 disk.img
xxd -l 16 -s 446 disk.img
echo "-- signature --"; xxd -l 2 -s 510 disk.img
echo "===== 扇区 1 前 16 字节(core.img) ====="
xxd -l 16 -s 512 disk.img
echo "===== 分区内容(FAT32 @2048) ====="
mdir -i disk.img@@$((2048*512)) ::/boot 2>&1 | head
mdir -i disk.img@@$((2048*512)) ::/boot/grub 2>&1 | head

echo "===== Phase2 交互: 从硬盘引导, 输入 GRUB/串口命令 ====="
rm -f s2.in s2.out; mkfifo s2.in s2.out
( cat < s2.out > cap_p2 ) &
CAT2=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=s2 -serial chardev:sp0 -no-reboot -no-shutdown &
QP2=$!
sleep 3
python3 - <<'PY'
import time
try:
    f=open('s2.in','wb')
    for cmd in ["ls","ls (hd0,msdos1)/boot","cat (hd0,msdos1)/boot/grub/grub.cfg"]:
        f.write((cmd+"\r").encode()); f.flush(); time.sleep(1.2)
    f.close()
except FileNotFoundError:
    pass
PY
sleep 3; kill $CAT2 2>/dev/null; kill $QP2 2>/dev/null
echo "===== Phase2 raw ====="
cat -v cap_p2 | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | head -100
echo "===== end ====="