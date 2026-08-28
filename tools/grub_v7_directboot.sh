#!/bin/bash
# 验证：嵌入配置在顶层直接 multiboot2+boot(不经过 normal/grub.cfg)，
# 设串口便于观察；分区放 kernel.bin。若内核启动(串口出现[FAT32])即方案成立。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
set -e
T=/tmp/gboot7
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cd "$T"

cat > embed.cfg <<'CFG'
echo EMBED_ALIVE_BEFORE_SERIAL
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
echo EMBED_ALIVE_AFTER_SERIAL
set root=(hd0,msdos1)
multiboot2 /boot/kernel.bin
boot
CFG

echo "== core.img(嵌入顶层直接boot配置) =="
grub-mkimage -O i386-pc -d "$GRUB" -p "(hd0,msdos1)/boot/grub" \
  -c embed.cfg -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal

dd if=/dev/zero of=disk.img bs=1M count=64 status=none
python3 - <<'PY'
import struct
boot=bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
struct.pack_into('<I', boot, 0x5c, 1)
pe=446
boot[pe+0]=0x80; boot[pe+1]=0xFE; boot[pe+2]=0xFF; boot[pe+3]=0xFF; boot[pe+4]=0x0C
boot[pe+5]=0xFE; boot[pe+6]=0xFF; boot[pe+7]=0xFF
struct.pack_into('<I', boot, pe+8,  2048)
struct.pack_into('<I', boot, pe+12, 131072-2048)
boot[510]=0x55; boot[511]=0xAA
open('sector0.bin','wb').write(boot)
PY
dd if=sector0.bin of=disk.img bs=512 count=1 conv=notrunc status=none
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none

OFFSET=$((2048*512))
mformat -i disk.img@@$OFFSET -F -c 8 -h 255 -s 63 -T $((131072-2048)) ::
mmd -i disk.img@@$OFFSET ::/boot
mcopy -i disk.img@@$OFFSET kernel.bin ::/boot/

echo "== QEMU 无光驱 =="
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -serial file:serial.log -no-reboot -no-shutdown &
QPID=$!
sleep 7
kill $QPID 2>/dev/null || true
echo "===== serial.log ====="
cat serial.log 2>/dev/null
echo "===== end ====="