#!/bin/bash
# 交互诊断：QEMU 串口用 pipe 双向，向 GRUB 壳发命令并读回输出，定位 prefix/分区/files 问题。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
T=/tmp/gboot8
rm -rf "$T"; mkdir -p "$T"
rm -f /tmp/gr.in /tmp/gr.out
mkfifo /tmp/gr.in /tmp/gr.out 2>/dev/null || true
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cd "$T"

# 嵌入配置仅设串口(保证看到提示符)
cat > embed.cfg <<'CFG'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
CFG

grub-mkimage -O i386-pc -d "$GRUB" -p "(hd0,msdos1)/boot/grub" \
  -c embed.cfg -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal

# 组装磁盘：MBR+core.img@1 + FAT32@2048 含 /boot/kernel.bin
dd if=/dev/zero of=disk.img bs=1M count=64 status=none
python3 - <<'PY'
import struct
boot=bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
struct.pack_into('<I', boot, 0x5c, 1)
pe=446
boot[pe+0]=0x80; boot[pe+1]=0xFE; boot[pe+2]=0xFF; boot[pe+3]=0xFF; boot[pe+4]=0x0C
boot[pe+5]=0xFE; boot[pe+6]=0xFF; boot[pe+7]=0xFF
struct.pack_into('<I', boot, pe+8, 2048)
struct.pack_into('<I', boot, pe+12,131072-2048)
boot[510]=0x55; boot[511]=0xAA
open('sector0.bin','wb').write(boot)
PY
dd if=sector0.bin of=disk.img bs=512 count=1 conv=notrunc status=none
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none
OFFSET=$((2048*512))
mformat -i disk.img@@$OFFSET -F -c 8 -h 255 -s 63 -T $((131072-2048)) ::
mmd -i disk.img@@$OFFSET ::/boot
mcopy -i disk.img@@$OFFSET kernel.bin ::/boot/

# 启动 QEMU，串口挂到 pipe /tmp/gr
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -serial pipe:/tmp/gr -no-reboot -no-shutdown &
QPID=$!
sleep 2

send(){ printf '%s\r' "$1" > /tmp/gr.in; sleep 0.5; }
readall(){ dd if=/tmp/gr.out bs=1 count=2000 2>/dev/null || true; echo; }

echo "===== prompt? ====="
readall

echo "===== ls / ====="; send "ls /"; readall
echo "===== ls (hd0,msdos1)/ ====="; send "set root=(hd0,msdos1)"; readall; send "ls /"; readall
echo "===== ls (hd0,msdos1)/boot ====="; send "ls /boot"; readall
echo "===== devlist ====="; send "ls"; readall
echo "===== prefix,root ====="; send "echo pre=\$prefix root=\$root"; readall
echo "===== multiboot2 ====="; send "multiboot2 /boot/kernel.bin"; readall
echo "===== boot ====="; send "boot"; readall
sleep 3
echo "===== after boot all ====="
dd if=/tmp/gr.out bs=1 count=4000 2>/dev/null || true
kill $QPID 2>/dev/null || true