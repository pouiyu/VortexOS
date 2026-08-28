#!/bin/bash
# 针对实验：boot.img 偏移0x5c 是 core.img 起始LBA(LE32)。
# 验证：patch 0x5c=1 + boot.img前446字节合并分区表 -> 扇区0，
#       core.img -> 扇区1起，FAT32@2048，无光驱引导。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
set -e
T=/tmp/gboot5
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cp "$SRC/tools/grub_test.cfg" "$T/grub.cfg"
cd "$T"

echo "== 1) 生成 core.img =="
grub-mkimage -O i386-pc -d "$GRUB" -p /boot/grub \
  -o core.img biosdisk part_msdos fat configfile normal boot multiboot2
echo "core.img: $(stat -c%s core.img) bytes, sectors=$(( ($(stat -c%s core.img)+511)/512 ))"

echo "== 2) 建 64MB 磁盘 + MBR分区表 =="
dd if=/dev/zero of=disk.img bs=1M count=64 status=none
python3 - <<'PY'
import struct
# 组装扇区0：分区表放 446.., 引导代码区用 boot.img[0..445] (其中 0x5c 已patch为1)
boot=bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
# patch core.img 起始LBA = 1
struct.pack_into('<I', boot, 0x5c, 1)
pe=446
boot[pe+0]=0x80; boot[pe+1]=0xFE; boot[pe+2]=0xFF; boot[pe+3]=0xFF; boot[pe+4]=0x0C
boot[pe+5]=0xFE; boot[pe+6]=0xFF; boot[pe+7]=0xFF
struct.pack_into('<I', boot, pe+8,  2048)
struct.pack_into('<I', boot, pe+12, 131072-2048)
boot[510]=0x55; boot[511]=0xAA
open('sector0.bin','wb').write(boot)
print('patched 0x5c =', struct.unpack_from('<I',boot,0x5c)[0])
PY
dd if=sector0.bin of=disk.img bs=512 count=1 conv=notrunc status=none

echo "== 3) core.img -> 扇区1起 =="
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none

echo "== 4) FAT32@2048 填充 =="
OFFSET=$((2048*512))
mformat -i disk.img@@$OFFSET -F -c 8 -h 255 -s 63 -T $((131072-2048)) ::
mmd -i disk.img@@$OFFSET ::/boot
mmd -i disk.img@@$OFFSET ::/boot/grub
mcopy -i disk.img@@$OFFSET grub.cfg ::/boot/grub/
mcopy -i disk.img@@$OFFSET kernel.bin ::/boot/
mdir -i disk.img@@$OFFSET ::/boot/grub | tail -5

echo "== 5) QEMU 无光驱启动 =="
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -serial file:serial.log -no-reboot -no-shutdown \
  -d guest_errors -D qemu_dbg.log &
QPID=$!
sleep 5
kill $QPID 2>/dev/null || true
echo "===== serial.log ====="
cat serial.log 2>/dev/null | head -60
echo "===== qemu_dbg (only 1xx lines) ====="
head -20 qemu_dbg.log 2>/dev/null
echo "===== end ====="