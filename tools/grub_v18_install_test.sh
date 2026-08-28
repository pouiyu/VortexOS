#!/bin/bash
# v18：端到端验证"脱盘"安装。
#   Phase1 从 CD 引导到空盘 -> 内核检测 FAT32 失败 -> installSystem(格式化+GRUB+系统文件+字体)
#   Phase2 拔光驱，从硬盘(-boot c)引导 -> GRUB -> 内核 -> FAT32 已就绪 -> 加载字体进 VBE
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gb18
rm -rf "$T"; mkdir -p "$T"
cd "$T"
cp "$SRC/vortexos.iso" iso.iso
dd if=/dev/zero of=disk.img bs=1M count=64 status=none

run_qemu() { # $1=label $2=pre-wait(秒) $@=qemu 附加参数
  local label="$1"; local pre="$2"; shift 2
  rm -f sp.in sp.out
  mkfifo sp.in sp.out
  ( cat < sp.out > "sp_$label.cap" ) &
  local cata=$!
  qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide "$@" \
    -machine pc -cpu qemu64 -smp 1 -m 64M \
    -display none -chardev pipe,id=sp0,path=sp -serial chardev:sp0 \
    -no-reboot -no-shutdown &
  local qp=$!
  sleep "$pre"
  kill "$cata" 2>/dev/null || true
  kill "$qp" 2>/dev/null || true
}

echo "########## PHASE 1: 从 CD 安装到空盘 ##########"
run_qemu p1 10 -cdrom iso.iso -boot d

echo "########## PHASE 2: 拔光驱，从硬盘引导 ##########"
run_qemu p2 12 -boot c

echo "================= PHASE 1 (install) ================="
cat -v sp_p1.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'FAT32|INSTALL|GRUB|FONT|Vortex' | head -60
echo "================= PHASE 2 (hdd boot) 完整原始输出 ================="
cat -v sp_p2.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | head -120
echo "================= done ================="