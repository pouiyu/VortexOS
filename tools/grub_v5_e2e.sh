#!/bin/bash
# 端到端：core.img prefix=(hd0,msdos1)/boot/grub + 嵌入"仅设串口"诊断配置，
# 分区 /boot/grub/grub.cfg + /boot/kernel.bin。验证 GRUB 从分区加载配置并引导内核。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
set -e
T=/tmp/gboot5
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cp "$SRC/tools/grub_test.cfg" "$T/grub.cfg"   # 分区上的正式配置(含serial+multiboot)
cd "$T"

# 嵌入探针配置：看 GRUB 执行到哪、能否读 (hd0,msdos1) 分区
cat > diag.cfg <<'CFG'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
echo "== marker1 serial-ok =="
set root=(hd0,msdos1)
ls (hd0,msdos1)/
echo "== marker2 after-ls =="
set timeout=0
set default=0
menuentry "VortexOS" {
    multiboot2 /boot/kernel.bin
    boot
}
CFG

echo "== core.img(prefix分区+嵌入串口诊断配置) =="
grub-mkimage -O i386-pc -d "$GRUB" -p "(hd0,msdos1)/boot/grub" \
  -c diag.cfg \
  -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal
echo "core.img: $(stat -c%s core.img) bytes sectors=$(( ($(stat -c%s core.img)+511)/512 ))"

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
mmd -i disk.img@@$OFFSET ::/boot/grub
mcopy -i disk.img@@$OFFSET grub.cfg ::/boot/grub/
mcopy -i disk.img@@$OFFSET kernel.bin ::/boot/

echo "== QEMU 无光驱 =="
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -serial file:serial.log -no-reboot -no-shutdown \
  -d guest_errors -D qemu_dbg.log &
QPID=$!
sleep 6
kill $QPID 2>/dev/null || true
echo "===== serial.log ====="
cat serial.log 2>/dev/null
echo "===== end ====="