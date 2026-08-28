#!/bin/bash
# v21：复刻 v17 的主机侧造盘(纯 dd/mbr/core/mformat)，确认分区能否枚举；
# 并与内核安装盘磁盘做扇区0字节级对比。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gb21
rm -rf "$T"; mkdir -p "$T"
cd "$T"
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" k.bin
cp "$SRC/grub.cfg" grub.cfg

cat > embed.cfg <<'CFG'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
CFG
grub-mkimage -O i386-pc -d "$GRUB" -p "(hd0,msdos1)/boot/grub" -c embed.cfg \
  -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal

dd if=/dev/zero of=disk.img bs=1M count=64 status=none
python3 - <<'PY'
import struct
boot=bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
struct.pack_into('<I',boot,0x5c,1)
pe=446
boot[pe+0]=0x80;boot[pe+1]=0xFE;boot[pe+2]=0xFF;boot[pe+3]=0xFF;boot[pe+4]=0x0C
boot[pe+5]=0xFE;boot[pe+6]=0xFF;boot[pe+7]=0xFF
struct.pack_into('<I',boot,pe+8,2048); struct.pack_into('<I',boot,pe+12,129024)
boot[510]=0x55;boot[511]=0xAA
open('mbr.bin','wb').write(boot)
PY
dd if=mbr.bin of=disk.img bs=512 count=1 conv=notrunc status=none
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none
mformat -i disk.img@@$((2048*512)) -F -c 8 -h 255 -s 63 -T 129024 ::
mmd -i disk.img@@$((2048*512)) ::/boot
mmd -i disk.img@@$((2048*512)) ::/boot/grub
mcopy -i disk.img@@$((2048*512)) grub.cfg ::/boot/grub/
mcopy -i disk.img@@$((2048*512)) k.bin ::/boot/

echo "===== v21 新鲜盘 MBR 分区项(446) 与内核盘(K=内核安装生成)对比 ====="
xxd -s 446 -l 48 disk.img
echo "-- 参考: /tmp/gb19/disk.img(此前内核安装) --"
xxd -s 446 -l 48 /tmp/gb19/disk.img 2>/dev/null || echo "gb19 gone"

rm -f s.in s.out; mkfifo s.in s.out
( cat < s.out > cap ) &  ca=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=s -serial chardev:sp0 -no-reboot -no-shutdown &
qp=$!
sleep 3
python3 - <<'PY'
import time
f=open('s.in','wb')
for cmd in ["ls","insmod part_msdos","ls (hd0,msdos1)/","cat (hd0,msdos1)/boot/grub/grub.cfg","ks"]:
    f.write((cmd+"\r").encode()); f.flush(); time.sleep(1.2)
f.close()
PY
sleep 3; kill "$ca" 2>/dev/null; kill "$qp" 2>/dev/null
echo "===== GRUB 交互输出 ====="
cat -v cap | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'grub>|hd0|msdos|Filesystem|configfile|cat|multiboot|not found|Vortex' | head -60
echo "===== end ====="