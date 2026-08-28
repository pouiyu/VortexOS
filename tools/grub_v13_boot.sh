#!/bin/bash
# 端到端引导验证：手动装盘(boot.img+core.img)无光驱从硬盘引导进内核。
# FIFO 式串口读取，避免 TCP 时序问题。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
set -u
T=/tmp/gb13
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cd "$T"

# grub.cfg: 串口直接进 /boot/kernel.bin（menuentry 内置 boot）
cat > grub.cfg <<'CFG'
set timeout=0
set default=0
menuentry "VortexOS" {
    set root=(hd0,msdos1)
    multiboot2 /boot/kernel.bin
    boot
}
CFG

# embed.cfg: 内嵌串口终端+启动序列。
# 前缀用整盘 (hd0)，避免启动期解析 msdos1 探测失败被缓存。
cat > embed.cfg <<'CFG'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
set root=(hd0)
ls (hd0)
set root=(hd0,msdos1)
ls (hd0,msdos1)/
configfile (hd0,msdos1)/boot/grub/grub.cfg
CFG

# core.img: 前缀指向整盘 /boot/grub（不指向分区）
grub-mkimage -O i386-pc -d "$GRUB" -p "(hd0)/boot/grub" \
  -c embed.cfg -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal
echo "core.img: $(stat -c%s core.img) bytes"

# 建盘 + 手动 MBR(与内核 fat32Format 相同的分区布局)
dd if=/dev/zero of=disk.img bs=1M count=64 status=none
python3 - <<'PY'
import struct
boot=bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
struct.pack_into('<I', boot, 0x5c, 1)       # 指向 core.img=扇区1
pe=446
boot[pe+0]=0x80; boot[pe+1]=0xFE; boot[pe+2]=0xFF; boot[pe+3]=0xFF; boot[pe+4]=0x0C
boot[pe+5]=0xFE; boot[pe+6]=0xFF; boot[pe+7]=0xFF
struct.pack_into('<I', boot, pe+8,  2048)
struct.pack_into('<I', boot, pe+12, 131072-2048)
boot[510]=0x55; boot[511]=0xAA
open('sec0.bin','wb').write(boot)
print('sec0 part start/size:', struct.unpack_from('<I',boot,pe+8)[0], struct.unpack_from('<I',boot,pe+12)[0])
PY
dd if=sec0.bin of=disk.img bs=512 count=1 conv=notrunc status=none
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none

# FAT32 分区自2048，放 /boot/grub/grub.cfg 与 /boot/kernel.bin
OFFSET=$((2048*512))
mformat -i disk.img@@$OFFSET -F -c 8 -h 255 -s 63 -T $((131072-2048)) ::
mmd -i disk.img@@$OFFSET ::/boot
mmd -i disk.img@@$OFFSET ::/boot/grub
mcopy -i disk.img@@$OFFSET grub.cfg ::/boot/grub/
mcopy -i disk.img@@$OFFSET kernel.bin ::/boot/
echo "-- /boot/grub --"; mdir -i disk.img@@$OFFSET ::/boot/grub
echo "-- /boot --"; mdir -i disk.img@@$OFFSET ::/boot

# 无光驱启动，管道串口交互诊断
rm -f sp.in sp.out
mkfifo sp.in sp.out
qemu-system-x86_64 -hda disk.img \
  -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
QPID=$!
sleep 3
# 后台读取输出 FIFO 到文件
( cat < sp.out > sp.cap ) &
CAT=$!
sleep 0.5
python3 - <<'PY'
import time
c=open('sp.in','wb')
time.sleep(0.5)
def send(s):
    c.write((s+"\r").encode()); c.flush(); time.sleep(0.6)
# embed.cfg 已尝试引导，交互仅观察结果
for cmd in ["ls","ls (hd0)","ls (hd0,msdos1)/"]:
    send(cmd)
c.close()
PY
sleep 2
kill $CAT 2>/dev/null || true
kill $QPID 2>/dev/null || true
echo "===== serial capture (sp.cap) ====="
cat -v sp.cap 2>/dev/null | head -80
echo "===== end ====="