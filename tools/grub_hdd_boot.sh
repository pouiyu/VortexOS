#!/bin/bash
# 验证路线A：core.img 直接写扇区0 自举 + FAT32 分区(自2048) 无光驱引导 multiboot2 内核
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
set -e
T=/tmp/gboot3
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cp "$SRC/tools/grub_test.cfg" "$T/grub.cfg"
cd "$T"

echo "== 1) core.img =="
grub-mkimage -O i386-pc -d "$GRUB" -p /boot/grub \
  -o core.img biosdisk part_msdos fat configfile normal boot multiboot2
echo "core.img: $(stat -c%s core.img) bytes"

echo "== 2) 建 64MB 磁盘 =="
dd if=/dev/zero of=disk.img bs=1M count=64 status=none

echo "== 3) 写 MBR(分区表) 扇区0 =="
python3 - <<'PY'
import struct
mbr=bytearray(512)
pe=446
mbr[pe+0]=0x80; mbr[pe+1]=0xFE; mbr[pe+2]=0xFF; mbr[pe+3]=0xFF; mbr[pe+4]=0x0C
mbr[pe+5]=0xFE; mbr[pe+6]=0xFF; mbr[pe+7]=0xFF
mbr[pe+8:pe+12]=struct.pack('<I',2048)
mbr[pe+12:pe+16]=struct.pack('<I',131072-2048)
mbr[510]=0x55; mbr[511]=0xAA
open('sector0.bin','wb').write(mbr)
PY
dd if=sector0.bin of=disk.img bs=512 count=1 conv=notrunc status=none

echo "== 4) core.img 写扇区0(覆盖引导代码，保留分区表) =="
python3 - <<'PY'
mbr=bytearray(open('sector0.bin','rb').read(512))
core=bytearray(open('core.img','rb').read())
# core.img 前446字节作为引导代码
for i in range(446): mbr[i]=core[i]
open('core_mbr.bin','wb').write(mbr)
PY
dd if=core_mbr.bin of=disk.img bs=512 count=1 conv=notrunc status=none
# core.img 的后续扇区写到 1..N
echo "== 5) core.img 后续写到扇区1.. =="
python3 - <<'PY'
core=open('core.img','rb').read()
n=(len(core)+511)//512
# 写扇区0 已经做，剩余 1..n-1 的每字节
sects=[core[i*512:(i+1)*512] for i in range(n)]
open('rest.bin','wb').write(b''.join(sects[1:]))
print('total sectors:', n)
PY
dd if=rest.bin of=disk.img bs=512 seek=1 conv=notrunc status=none

echo "== 6) 创建 FAT32 分区(偏移2048) =="
OFFSET=$((2048*512))
mpartition -I -i disk.img 2>/dev/null || true
# mformat FAT32：-F 强制FAT32，-L 卷标，-c 8 每簇扇区，-h/-s 几何，-T 分区扇区数
# 直接指定总扇区
mformat -i disk.img@@$OFFSET -F -c 8 -h 255 -s 63 -T $((131072-2048)) ::
mformat -i disk.img@@$OFFSET -F ::
echo "-- 建 /boot/grub --"
mmd -i disk.img@@$OFFSET ::/boot
mmd -i disk.img@@$OFFSET ::/boot/grub
mcopy -i disk.img@@$OFFSET grub.cfg ::/boot/grub/
mcopy -i disk.img@@$OFFSET kernel.bin ::/boot/
echo "-- 内容 --"
mdir -i disk.img@@$OFFSET ::
mdir -i disk.img@@$OFFSET ::/boot/grub

echo "== 7) QEMU 无光驱启动 =="
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -serial mon:stdio -monitor none -no-reboot -no-shutdown \
  -d guest_errors -D qemu_dbg.log \
  > console.txt 2>&1 < /dev/null &
QPID=$!
sleep 5
kill $QPID 2>/dev/null || true
echo "===== console.txt ====="
cat console.txt 2>/dev/null | head -80
echo "===== serial.log ====="
cat serial.log 2>/dev/null | head -80
echo "===== end ====="