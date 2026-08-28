#!/bin/bash
# 交互诊断V2：QEMU 串口挂 TCP server，脚本用 bash /dev/tcp 发送 GRUB 命令并读回输出。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
T=/tmp/gboot9
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

# 用 python 打开的 TCP 连接发送命令并读取
cmds='ls /
var: suffix
set root=(hd0,msdos1)
ls /
ls /boot
multiboot2 /boot/kernel.bin
boot
'
python3 - "$cmds" <<'PY'
import socket,sys,time
cmds=sys.argv[1].splitlines()
try:
    s=socket.create_connection(('127.0.0.1',5599),timeout=3)
except Exception as e:
    print("connect fail:",e); raise SystemExit
s.settimeout(0.4)
buf=b""
def pump(label):
    global buf
    end=time.time()+1.0
    while time.time()<end:
        try:
            d=s.recv(4096)
            if not d: break
            buf+=d
        except socket.timeout:
            break
        except Exception as e:
            break
    print("-----",label,"-----")
    try:
        print(buf.decode('latin1','replace'))
    except: print(buf)
    buf=b""
pump("initial")
for c in cmds:
    c=c.strip()
    if not c: continue
    s.sendall((c+"\r").encode())
    time.sleep(0.4)
    pump("CMD: "+c)
s.close()
PY
kill $QPID 2>/dev/null || true