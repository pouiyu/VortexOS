#!/bin/bash
# 最简验证：嵌入只设串口+一个菜单项，观察菜单是否出现在串口(判断嵌入配置是否执行)
# 不填分区，只测 boot.img->core.img->嵌入配置->菜单 这一链路。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
set -e
T=/tmp/gboot6
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cd "$T"

cat > embed.cfg <<'CFG'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
set timeout=8
set default=0
menuentry "TEST_MENU_ENTRY_XYZ" { echo hi }
CFG

echo "== core.img 嵌入 embed.cfg =="
grub-mkimage -O i386-pc -d "$GRUB" -p "(hd0,msdos1)/boot/grub" \
  -c embed.cfg -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal echo

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

echo "== QEMU 无光驱 =="
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -serial file:serial.log -no-reboot -no-shutdown &
QPID=$!
sleep 5
kill $QPID 2>/dev/null || true
echo "===== serial.log ====="
cat serial.log 2>/dev/null
echo "===== end ====="