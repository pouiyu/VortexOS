#!/bin/bash
# Ground truth：用标准 grub-bios-setup 装盘到 raw 镜像（FAT32 分区自 LBA2048 预填 /boot），
# 无光驱(-boot c) 引导 multiboot2 内核串口观察。验证布局可行后，抽取扇区0与core.img供内核复刻。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
set -e
T=/tmp/gboot3
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cp "$SRC/tools/grub_test.cfg" "$T/grub.cfg"
cd "$T"

echo "== 1) 64MB 磁盘 =="
dd if=/dev/zero of=disk.img bs=1M count=64 status=none

echo "== 2) MBR 分区表(FAT32 自LBA2048) =="
python3 - <<'PY'
import struct
mbr=bytearray(512)
pe=446
mbr[pe+0]=0x80; mbr[pe+1]=0xFE; mbr[pe+2]=0xFF; mbr[pe+3]=0xFF; mbr[pe+4]=0x0C
mbr[pe+5]=0xFE; mbr[pe+6]=0xFF; mbr[pe+7]=0xFF
struct.pack_into('<I',mbr,pe+8, 2048)
struct.pack_into('<I',mbr,pe+12,131072-2048)
mbr[510]=0x55; mbr[511]=0xAA
open('sector0.bin','wb').write(mbr)
PY
dd if=sector0.bin of=disk.img bs=512 count=1 conv=notrunc status=none

echo "== 3) 预填 FAT32@2048 /boot/grub 与 /boot/kernel.bin =="
OFFSET=$((2048*512))
mformat -i disk.img@@$OFFSET -F -c 8 -h 255 -s 63 -T $((131072-2048)) ::
mmd -i disk.img@@$OFFSET ::/boot
mmd -i disk.img@@$OFFSET ::/boot/grub
mcopy -i disk.img@@$OFFSET grub.cfg ::/boot/grub/
mcopy -i disk.img@@$OFFSET kernel.bin ::/boot/
mdir -i disk.img@@$OFFSET ::/boot/grub

echo "== 4) grub-bios-setup 装盘 =="
grub-bios-setup -d "$GRUB" disk.img 2>&1 | head -20

echo "== 5) 抽取装盘后的扇区0 与 core 起始信息 =="
dd if=disk.img of=installed_sec0.bin bs=512 count=1 status=none
python3 - <<'PY'
import struct
s=open('installed_sec0.bin','rb').read(512)
print('sector0[0x5c:0x60]=', s[0x5c:0x60].hex(), 'u32=', struct.unpack_from('<I',s,0x5c)[0])
print('sector0[0x58:0x64]=', s[0x58:0x64].hex())
print('first code bytes:', s[:16].hex())
print('0x55AA@510:', hex(s[510]), hex(s[511]))
print('boot_drive@0x1b4:', hex(s[0x1B4]))
print('part entry:', s[446:462].hex())
PY

echo "== 6) QEMU 无光驱启动 =="
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -serial file:serial.log -no-reboot -no-shutdown \
  -d guest_errors -D qemu_dbg.log &
QPID=$!
sleep 5
kill $QPID 2>/dev/null || true
echo "===== serial.log ====="
cat serial.log 2>/dev/null | head -100
echo "===== end ====="