#!/bin/bash
# 验证路线B：core.img 固化的 prefix 指向分区 (hd0,msdos1)/boot/grub，
# boot.img 补 core.img 起始 LBA=1 写扇区0，FAT32 分区自 LBA2048，
# 无光驱(-boot c) 引导 multiboot2 内核，串口(38400)观察输出。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
set -e
T=/tmp/gboot2
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cp "$SRC/tools/grub_test.cfg" "$T/grub.cfg"
cd "$T"

echo "== 1) core.img(prefix=(hd0,msdos1)/boot/grub, 含serial/terminal) =="
grub-mkimage -O i386-pc -d "$GRUB" -p "(hd0,msdos1)/boot/grub" \
  -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal
echo "core.img: $(stat -c%s core.img) bytes, sectors=$(( ($(stat -c%s core.img)+511)/512 ))"

echo "== 2) 64MB 磁盘 + MBR 分区表 =="
dd if=/dev/zero of=disk.img bs=1M count=64 status=none
python3 - <<'PY'
import struct
boot=bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
# 补 core.img 起始 LBA = 1
struct.pack_into('<I', boot, 0x5c, 1)
pe=446
boot[pe+0]=0x80; boot[pe+1]=0xFE; boot[pe+2]=0xFF; boot[pe+3]=0xFF; boot[pe+4]=0x0C
boot[pe+5]=0xFE; boot[pe+6]=0xFF; boot[pe+7]=0xFF
struct.pack_into('<I', boot, pe+8,  2048)
struct.pack_into('<I', boot, pe+12, 131072-2048)
boot[510]=0x55; boot[511]=0xAA
open('sector0.bin','wb').write(boot)
print('boot.img bytes 0x58..0x63:', boot[0x58:0x64].hex())
PY
dd if=sector0.bin of=disk.img bs=512 count=1 conv=notrunc status=none

echo "== 3) core.img -> 扇区1起 =="
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none

echo "== 4) FAT32@2048 填 /boot =="
OFFSET=$((2048*512))
mformat -i disk.img@@$OFFSET -F -c 8 -h 255 -s 63 -T $((131072-2048)) ::
mmd -i disk.img@@$OFFSET ::/boot
mmd -i disk.img@@$OFFSET ::/boot/grub
mcopy -i disk.img@@$OFFSET grub.cfg ::/boot/grub/
mcopy -i disk.img@@$OFFSET kernel.bin ::/boot/
mdir -i disk.img@@$OFFSET ::/boot/grub

echo "== 5) QEMU 无光驱启动 =="
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -serial file:serial.log -no-reboot -no-shutdown \
  -d guest_errors -D qemu_dbg.log &
QPID=$!
sleep 5
kill $QPID 2>/dev/null || true
echo "===== serial.log ====="
cat serial.log 2>/dev/null | head -80
echo "===== qemu_dbg ====="
head -20 qemu_dbg.log 2>/dev/null
echo "===== end ====="