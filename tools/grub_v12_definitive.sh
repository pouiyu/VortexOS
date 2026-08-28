#!/bin/bash
# 决定性诊断：确认 part_msdos 是否加载、GRUB 能否枚举分区。
# 用 sfdisk 造"已知正确"分区，boot.img+core.img 手动装盘(扇区0/1)。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
set -u
T=/tmp/gb12
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

# 以 sfdisk 标准分区(避免手工表嫌疑)，core.img 前缀用 hd0 原始设备
grub-mkimage -O i386-pc -d "$GRUB" -p "(hd0)/boot/grub" \
  -c embed.cfg -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal
echo "core.img: $(stat -c%s core.img) bytes"
echo "part_msdos 是否在 core.img: $(strings core.img | grep -c part_msdos)"

# 建盘并 sfdisk 分区
dd if=/dev/zero of=disk.img bs=1M count=64 status=none
printf 'start=2048, size=129024, type=c, bootable\n' | /sbin/sfdisk disk.img >/dev/null 2>&1
echo "--- sfdisk 分区项(446..462) ---"
python3 -c "d=open('disk.img','rb').read(512); print(d[446:462].hex())"

# 合并: boot.img(+分区表)->扇区0, core.img->扇区1
python3 - <<'PY'
import struct
mbr=bytearray(open('disk.img','rb').read(512))        # 保留 sfdisk 分区表
boot=bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
struct.pack_into('<I', boot, 0x5c, 1)                # boot.img 指向 core.img 在扇区1
# 标准做法:引导码区(0..445)用 boot.img,分区表(446..510)用 mbr
boot[446:510]=mbr[446:510]
boot[510]=0x55; boot[511]=0xAA
open('sec0.bin','wb').write(boot)
print('sec0 part1 start/size:', struct.unpack_from('<I',boot,454)[0], struct.unpack_from('<I',boot,458)[0])
PY
dd if=sec0.bin of=disk.img bs=512 count=1 conv=notrunc status=none
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none

qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -machine pc -cpu qemu64 -smp 1 \
  -m 64M -boot c -display none -serial tcp:127.0.0.1:5599,server=on,nowait -no-reboot -no-shutdown &
QPID=$!
sleep 2

python3 - <<'PY'
import socket,time,select
def pump(s,label,wait=0.5):
    buf=b""; end=time.time()+wait
    while time.time()<end:
        r,_,_=select.select([s],[],[],0.1)
        if r:
            try: d=s.recv(4096)
            except: break
            if not d: break
            buf+=d
    print("### "+label); print(buf.decode('latin1','replace').strip().replace('\r',''))
s=socket.create_connection(('127.0.0.1',5599),timeout=3); s.settimeout(0.5)
time.sleep(0.3); pump(s,"banner",0.5)
for c in ["lsmod","ls","parttype (hd0)","insmod part_msdos","lsmod","ls (hd0)",
          "ls (hd0,1)","set root=(hd0,1)","ls /","set root=(hd0,msdos1)","ls /"]:
    s.sendall((c+"\r").encode()); time.sleep(0.4); pump(s,c,0.5)
s.close()
PY
kill $QPID 2>/dev/null || true
echo "===== 磁盘分区(fdisk) ====="
fdisk -l disk.img | grep -E '^Disk |Device|Img1'