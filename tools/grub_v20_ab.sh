#!/bin/bash
# v20 A/B：同为内核 MBR+core.img，A=内核格式化的分区，B=用 mformat 重分区。
# 判定 part_msdos 是否因内核 FAT 布局而拒绝枚举分区。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gb20
rm -rf "$T"; mkdir -p "$T"
cd "$T"
cp "$SRC/vortexos.iso" iso.iso
dd if=/dev/zero of=diskA.img bs=1M count=64 status=none

probe() { # $1=disk $2=label
  local d="$1"
  rm -f s.in s.out; mkfifo s.in s.out
  ( cat < s.out > "cap_$2" ) &  local ca=$!
  qemu-system-x86_64 -drive file="$d",format=raw,if=ide -boot c \
    -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
    -chardev pipe,id=sp0,path=s -serial chardev:sp0 -no-reboot -no-shutdown &
  local qp=$!
  sleep 3
  python3 - <<'PY'
import time
try:
    f=open('s.in','wb')
    for cmd in ["ls (hd0)","ls (hd0,msdos1)/boot","ls (hd0,1)/boot"]:
        f.write((cmd+"\r").encode()); f.flush(); time.sleep(1.2)
    f.close()
except FileNotFoundError: pass
PY
  sleep 2; kill "$ca" 2>/dev/null; kill "$qp" 2>/dev/null
  echo "--- $2 ---"
  cat -v "cap_$2" | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'grub>|msdos|hd0|not found| error' | head -40
}

echo "== 先内核安装到 diskA =="
rm -f sp.in sp.out; mkfifo sp.in sp.out
( cat < sp.out > cap_inst ) &
ci=$!
qemu-system-x86_64 -drive file=diskA.img,format=raw,if=ide -cdrom iso.iso -boot d \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
qi=$!
sleep 8; kill "$ci" 2>/dev/null; kill "$qi" 2>/dev/null
echo "install sees: $(cat -v cap_inst | grep -c 'System installed')x System installed"

echo "== 制作 diskB: 同一 MBR+core，分区用 mformat 重格式化 =="
cp diskA.img diskB.img
OFF=$((2048*512))
mformat -i diskB.img@@$OFF -F -c 8 -h 255 -s 63 -T 129024 ::
mmd -i diskB.img@@$OFF ::/boot
echo "diskB 已重格式化分区(保留 MBR+core)"

probe diskA.img "A_kernel_fs"
probe diskB.img "B_mformat_fs"
echo "===== end ====="