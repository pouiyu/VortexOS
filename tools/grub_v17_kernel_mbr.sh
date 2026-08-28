#!/bin/bash
# v17：用内核 fat32Format 的精确 MBR 字节造盘，QEMU 自动 geometry，
# 无光驱从硬盘 boot.img->core.img->grub.cfg->multiboot2 引导进内核。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
T=/tmp/gb17
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cp "$SRC/grub.cfg" "$T/grub.cfg"
cd "$T"

cat > embed.cfg <<'CFG'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
CFG

grub-mkimage -O i386-pc -d "$GRUB" -p "(hd0,msdos1)/boot/grub" \
  -c embed.cfg -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal

dd if=/dev/zero of=disk.img bs=1M count=64 status=none
# 复刻内核 MBR（boot=0x80, CHS=FE FF FF, type=0x0C, start LBA 2048, size 129024）
python3 - <<'PY'
mbr=bytearray(512)
pe=446
mbr[pe+0]=0x80; mbr[pe+1]=0xFE; mbr[pe+2]=0xFF; mbr[pe+3]=0xFF; mbr[pe+4]=0x0C
mbr[pe+5]=0xFE; mbr[pe+6]=0xFF; mbr[pe+7]=0xFF
import struct
struct.pack_into('<I',mbr,pe+8,2048)
struct.pack_into('<I',mbr,pe+12,129024)
mbr[510]=0x55; mbr[511]=0xAA
open('mbr.bin','wb').write(mbr)
PY
dd if=mbr.bin of=disk.img bs=512 count=1 conv=notrunc status=none
# 合并 boot.img 引导代码区，保留分区表
python3 - <<'PY'
img=bytearray(open('disk.img','rb').read(512))
boot=bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
import struct
struct.pack_into('<I',boot,0x5c,1)
img[0:446]=boot[0:446]
open('disk.img','r+b').write(img)
PY
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none
# FAT32 分区（SPC=8, h=255, s=63，贴近内核参数）
OFF=$((2048*512))
mformat -i disk.img@@$OFF -F -c 8 -h 255 -s 63 -T 129024 ::
mmd -i disk.img@@$OFF ::/boot
mmd -i disk.img@@$OFF ::/boot/grub
mcopy -i disk.img@@$OFF grub.cfg ::/boot/grub/
mcopy -i disk.img@@$OFF kernel.bin ::/boot/
# 内核文件系统读不到签名时会给每字符首字节0x00，用2秒时延后解析；这里直接核对内容
echo "-- /boot 内容 --"; mdir -i disk.img@@$OFF ::/boot; mdir -i disk.img@@$OFF ::/boot/grub

rm -f sp.in sp.out
mkfifo sp.in sp.out
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide \
  -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
QPID=$!
sleep 6
( cat < sp.out > sp.cap ) &
CAT=$!
sleep 2
kill $CAT 2>/dev/null || true
kill $QPID 2>/dev/null || true
echo "===== serial capture (raw) ====="
cat -v sp.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | head -150
echo "===== end ====="