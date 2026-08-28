#!/bin/bash
# v22：改用 sfdisk 验证过的真实 CHS 分区项(test)，验证 stable 枚举
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gb22
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
# 真实 CHS(255H/63S 几何): 项1 CHS=32,33,0 ; 结束 CHS=40,32,8
entry=bytes([0x80,0x20,0x21,0x00, 0x0C,0x28,0x20,0x08, 0x00,0x08,0x00,0x00, 0x00,0xF8,0x01,0x00])
boot[446:462]=entry
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
for cmd in ["ls (hd0,msdos1)/","ls (hd0,msdos1)/boot","cat (hd0,msdos1)/boot/grub/grub.cfg"]:
    f.write((cmd+"\r").encode()); f.flush(); time.sleep(1.2)
f.close()
PY
sleep 3; kill "$ca" 2>/dev/null; kill "$qp" 2>/dev/null
echo "===== GRUB ====="
cat -v cap | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'grub>|hd0|msdos|Filesystem|not found|error' | head -40
echo "===== end ====="