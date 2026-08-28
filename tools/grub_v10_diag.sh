#!/bin/bash
# 诊断：逐条发命令，先看设备列表与分区是否存在
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
T=/tmp/gbootA
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cd "$T"
cat > embed.cfg <<'CFG'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
CFG
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
struct.pack_into('<I', boot, pe+8, 2048)
struct.pack_into('<I', boot, pe+12,131072-2048)
boot[510]=0x55; boot[511]=0xAA
open('sector0.bin','wb').write(boot)
print('MRB part1 start/size:', struct.unpack_from('<I',boot,pe+8)[0], struct.unpack_from('<I',boot,pe+12)[0])
PY
dd if=sector0.bin of=disk.img bs=512 count=1 conv=notrunc status=none
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none
OFFSET=$((2048*512))
mformat -i disk.img@@$OFFSET -F -c 8 -h 255 -s 63 -T $((131072-2048)) ::
mmd -i disk.img@@$OFFSET ::/boot
mcopy -i disk.img@@$OFFSET kernel.bin ::/boot/

qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -serial tcp:127.0.0.1:5599,server=on,nowait -no-reboot -no-shutdown &
QPID=$!
sleep 2

python3 - <<'PY'
import socket,time,select
cmds=["ls","ls (hd0)","ls (hd0,1)","ls (hd0,msdos1)","set root=(hd0,1)","ls /","set root=(hd0,msdos1)","ls /"]
def pump(s,label):
    buf=b""; end=time.time()+0.6
    while time.time()<end:
        r,_,_=select.select([s],[],[],0.1)
        if r:
            try: d=s.recv(4096)
            except: break
            if not d: break
            buf+=d
    print("### "+label)
    print(buf.decode('latin1','replace').strip().replace('\r',''))
s=socket.create_connection(('127.0.0.1',5599),timeout=3)
s.settimeout(0.5)
pump(s,"banner")
for c in cmds:
    s.sendall((c+"\r").encode()); time.sleep(0.4)
    pump(s,c)
s.close()
PY
kill $QPID 2>/dev/null || true
echo "---- fdisk 验证磁盘分区 ----"
fdisk -l "$T/disk.img" 2>&1 | head -20