#!/bin/bash
# v14：开启 GRUB 内部 debug 复现启动期分区打开失败，定位卡点。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
set -u
T=/tmp/gb14
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

grub-mkimage -O i386-pc -d "$GRUB" -p "(hd0)/boot/grub" \
  -c embed.cfg -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal

dd if=/dev/zero of=disk.img bs=1M count=64 status=none
python3 - <<'PY'
import struct
boot=bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
struct.pack_into('<I', boot, 0x5c, 1)
# 分区项采用 sfdisk 验证过的精确字节(CHS 用真实值而非 FE FF FF)，
# 因为 GRUB part_msdos 在 QEMU/EDD 下不认 FE FF FF。
entry=bytes([0x80,0x20,0x21,0x00, 0x0C,0x28,0x20,0x08, 0x00,0x08,0x00,0x00, 0x00,0xF8,0x01,0x00])
boot[446:462]=entry
boot[510]=0x55; boot[511]=0xAA
open('sec0.bin','wb').write(boot)
print('part1:', boot[446:462].hex())
PY
dd if=sec0.bin of=disk.img bs=512 count=1 conv=notrunc status=none
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none
OFFSET=$((2048*512))
mformat -i disk.img@@$OFFSET -F -c 8 -h 255 -s 63 -T $((131072-2048)) ::
mmd -i disk.img@@$OFFSET ::/boot
mmd -i disk.img@@$OFFSET ::/boot/grub
mcopy -i disk.img@@$OFFSET kernel.bin ::/boot/

rm -f sp.in sp.out
mkfifo sp.in sp.out
qemu-system-x86_64 -drive if=none,id=hd0,file=disk.img,format=raw \
  -device ide-hd,drive=hd0,cyls=130,heads=16,secs=63 \
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
for cmd in ["set debug=all","insmod part_msdos","ls (hd0,msdos1)/","ls"]:
    send(cmd)
c.close()
PY
sleep 2
kill $CAT 2>/dev/null || true
kill $QPID 2>/dev/null || true
echo "===== serial capture ====="
cat -v sp.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'disk|partmap|msdos|Read|FAT|error|hd0|fs:|grub>|Detecting|not found' | head -120
echo "===== end ====="