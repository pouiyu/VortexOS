#!/bin/bash
# v15：完全用 SFDISK + MKFS 造盘（最贴近真实磁盘），QEMU 不带显式 geometry，
# grub.cfg 完整拉内核。验证一个"真实分区" GRUB 是否稳定枚举并引导。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
T=/tmp/gb15
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cd "$T"

cat > embed.cfg <<'CFG'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
CFG

grub-mkimage -O i386-pc -d "$GRUB" -p "(hd0,msdos1)/boot/grub" \
  -c embed.cfg -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal

dd if=/dev/zero of=disk.img bs=1M count=64 status=none
# sfdisk 创建类型 0x0C 分区，自动填 CHS
printf '2048,129024,0x0C\n' | sfdisk disk.img >/dev/null 2>&1
echo "---- sfdisk 实际写入的分区项 ----"
sfdisk -d disk.img

LO=$(losetup -f --show -P disk.img 2>/dev/null)
if [ -n "$LO" ]; then
  mkfs.fat -F 32 "$LO"p1 >/dev/null 2>&1
  mkdir -p mnt; mount "$LO"p1 mnt 2>/dev/null
  if [ -d mnt ]; then
    mkdir -p mnt/boot/grub
    cp "$SRC/grub.cfg" mnt/boot/grub/grub.cfg
    cp kernel.bin mnt/boot/kernel.bin
    umount mnt; rmdir mnt
  else
    echo "!! mount failed, fallback to mformat"
    OFF=$((2048*512))
    mformat -i disk.img@@$OFF -F -c 8 -h 64 -s 32 -T 129024 ::
    mmd -i disk.img@@$OFF ::/boot
    mmd -i disk.img@@$OFF ::/boot/grub
    mcopy -i disk.img@@$OFF "$SRC/grub.cfg" ::/boot/grub/
    mcopy -i disk.img@@$OFF kernel.bin ::/boot/
  fi
  losetup -d "$LO"
else
  echo "!! no losetup, mformat only"
  OFF=$((2048*512))
  mformat -i disk.img@@$OFF -F -c 8 -h 64 -s 32 -T 129024 ::
  mmd -i disk.img@@$OFF ::/boot
  mmd -i disk.img@@$OFF ::/boot/grub
  mcopy -i disk.img@@$OFF "$SRC/grub.cfg" ::/boot/grub/
  mcopy -i disk.img@@$OFF kernel.bin ::/boot/
fi

# 装 boot.img(写 LBA 字段)+core.img
python3 - <<'PY'
import struct
boot=bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
struct.pack_into('<I', boot, 0x5c, 1)
boot[510]=0x55; boot[511]=0xAA
open('sec0.bin','wb').write(boot)
PY
# 保留 sfdisk 分区表：只合并引导代码区
python3 - <<'PY'
disk=open('disk.img','rb').read()
mbr=bytearray(disk[:512])
boot=open('sec0.bin','rb').read(512)
mbr[0:446]=boot[0:446]
open('disk.img','r+b').write(mbr)
PY
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none

rm -f sp.in sp.out
mkfifo sp.in sp.out
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide \
  -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
QPID=$!
sleep 3
( cat < sp.out > sp.cap ) &
CAT=$!
sleep 0.3
python3 - <<'PY'
import time
c=open('sp.in','wb')
time.sleep(0.5)
def send(s):
    c.write((s+"\r").encode()); c.flush(); time.sleep(0.7)
for cmd in ["ls (hd0)","ls (hd0,msdos1)/","ls (hd0,1)/","reboot"]:
    send(cmd)
c.close()
PY
sleep 3
kill $CAT 2>/dev/null || true
kill $QPID 2>/dev/null || true
echo "===== serial capture ====="
cat -v sp.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'grub>|hd0|msdos|Not found|Entering|boot|kernel|error|Partition|Multiboot|Starting|Vortex' | head -120
echo "===== end ====="