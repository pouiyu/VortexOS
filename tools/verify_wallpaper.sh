#!/bin/bash
# verify_wallpaper.sh —— 全自动无头验证壁纸 LFN 修复：
#   Phase1: 空盘从 CD 引导 -> 注入 'y' 格式化安装 -> 安装完成自动重启(触发 QEMU 退出)
#   Phase2: 从硬盘引导 -> 主菜单注入 's','s',' ' -> 进入图形模式 -> 捕获 [WALLPAPER] 串口行
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/vwall; rm -rf "$T"; mkdir -p "$T"; cd "$T"
cp "$SRC/vortexos.iso" iso.iso
dd if=/dev/zero of=disk.img bs=1M count=64 status=none

MON=4444
QPID=""; CATA=""

qemu_up() { # $1=label 其余=附加qemu参数
  local label="$1"; shift
  rm -f sp.in sp.out; mkfifo sp.in sp.out
  ( cat < sp.out > "sp_$label.cap" ) & CATA=$!
  qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide,index=0 "${@}" \
    -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
    -chardev pipe,id=sp0,path=sp -serial chardev:sp0 \
    -monitor tcp:127.0.0.1:$MON,server,nowait \
    -no-reboot -no-shutdown &
  QPID=$!
  for i in $(seq 1 20); do
    if (echo > /dev/tcp/127.0.0.1/$MON) 2>/dev/null; then break; fi
    sleep 0.5
  done
}

qemu_key() { echo "sendkey $1" | timeout 2 bash -c 'cat > /dev/tcp/127.0.0.1/'"$MON" 2>/dev/null; sleep 0.4; }

qemu_down() { kill "$CATA" 2>/dev/null || true; kill "$QPID" 2>/dev/null || true; wait "$QPID" 2>/dev/null || true; rm -f sp.in sp.out; }

echo "############ PHASE 1: 空盘安装 ############"
qemu_up p1 -drive file=iso.iso,format=raw,media=cdrom,if=ide,index=2,readonly=on -boot d
# 在安装向导显示期间反复注入 'y'
for i in $(seq 1 20); do
  if ! kill -0 "$QPID" 2>/dev/null; then break; fi
  qemu_key y
done
# 等待"安装完成重启"使 QEMU 退出
for i in $(seq 1 60); do
  if ! kill -0 "$QPID" 2>/dev/null; then break; fi
  sleep 1
done
qemu_down

echo "############ PHASE 2: 硬盘引导 -> 图形模式 ############"
qemu_up p2 -boot c
# 主菜单 graphic 需按 s,s,回车; 期间反复注入 s,s,spc
for i in $(seq 1 10); do
  if ! kill -0 "$QPID" 2>/dev/null; then break; fi
  qemu_key s
done
for i in $(seq 1 10); do
  if ! kill -0 "$QPID" 2>/dev/null; then break; fi
  qemu_key spc
done
sleep 2
qemu_down

echo "================= PHASE 1 (install) ================="
cat -v sp_p1.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'INSTALL|wallpaper|WALLPAPER|Format|fat32|error|fail|Vortex' | head -30
echo "================= PHASE 2 (hdd boot -> graphic) ================="
cat -v sp_p2.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'WALLPAPER|down|ok|FAT32|FONT|VBE|error|fail' | head -50
echo "================= done ================="