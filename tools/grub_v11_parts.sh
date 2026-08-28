#!/bin/bash
# 对照：用 sfdisk 标准分区 vs 我们的手工MBR，看 GRUB 能否枚举分区
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
run(){ # $1=tag  $2=sec0file
  local tag=$1
  local T=/tmp/gb_$tag
  rm -rf "$T"; mkdir -p "$T"
  GRUB=/usr/lib/grub/i386-pc
  cp /mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os/kernel.bin "$T/kernel.bin"
  cd "$T"
  cat > embed.cfg <<'CFG'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
CFG
  grub-mkimage -O i386-pc -d "$GRUB" -p "/boot/grub" \
    -c embed.cfg -o core.img biosdisk part_msdos fat configfile normal boot multiboot2 serial terminal
  dd if=/dev/zero of=disk.img bs=1M count=64 status=none
  dd if=sec0 of=disk.img bs=512 count=1 conv=notrunc status=none
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
def pump(s,label):
    buf=b""; end=time.time()+0.5
    while time.time()<end:
        r,_,_=select.select([s],[],[],0.1)
        if r:
            try: d=s.recv(4096)
            except: break
            if not d: break
            buf+=d
    txt=buf.decode('latin1','replace').strip().replace('\r','')
    print("[%s]"%label, txt)
s=socket.create_connection(('127.0.0.1',5599),timeout=3); s.settimeout(0.5)
cmds=["ls","insmod part_msdos","ls (hd0)","ls (hd0,1)","set root=(hd0,1)","ls /","multiboot2 /boot/kernel.bin","boot"]
for c in cmds:
    s.sendall((c+"\r").encode()); time.sleep(0.35)
    pump(s,c)
s.close()
PY
  kill $QPID 2>/dev/null || true
}

echo "=========== A) sfdisk 标准分区 ==========="
TA=/tmp/gb_sf; rm -rf "$TA"; mkdir -p "$TA"
dd if=/dev/zero of="$TA/disk.img" bs=1M count=64 status=none
printf '2048,129024,0x0c,*,255,63\n' | /sbin/sfdisk "$TA/disk.img" >/dev/null 2>&1 || \
  printf '2048,129024,0x0c,*,255,63\n' | sfdisk -u S "$TA/disk.img" >/dev/null 2>&1 || true
dd if="$TA/disk.img" of="$TA/sec0" bs=512 count=1 status=none
echo "--- sfdisk sec0 partition entry (446..462) ---"
python3 -c "import sys;d=open('$TA/sec0','rb').read(512);print(d[446:462].hex())"
cp "$TA/sec0" /tmp/sec0_sf
run sf <(cat /tmp/sec0_sf)