#!/bin/bash
# grub_e2e_hdd.sh —— 完整"脱盘"验证：
#   Phase1 从 CD 引导到空盘 -> installSystem(格式化+写GRUB+拷系统文件+字体)
#   Phase2 拔光驱, 从硬盘引导 -> GRUB(memdisk内核) -> FAT32 就绪 -> 加载字体进 VBE -> Shell
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gbe2e; rm -rf "$T"; mkdir -p "$T"; cd "$T"
cp "$SRC/vortexos.iso" iso.iso
dd if=/dev/zero of=disk.img bs=1M count=64 status=none

run_qemu() { # $1=label $2=pre(秒) $@=附加参数
  local label="$1"; local pre="$2"; shift 2
  rm -f sp.in sp.out
  mkfifo sp.in sp.out
  ( cat < sp.out > "sp_$label.cap" ) & local cata=$!
  qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide "$@" \
    -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
    -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
  local qp=$!
  sleep "$pre"
  kill "$cata" 2>/dev/null || true
  kill "$qp" 2>/dev/null || true
  wait "$qp" 2>/dev/null || true
  rm -f sp.in sp.out
}

echo "########## PHASE 1: 从 CD 安装到空盘 ##########"
run_qemu p1 12 -cdrom iso.iso -boot d

echo "########## PHASE 2: 拔光驱, 从硬盘引导 ##########"
run_qemu p2 14 -boot c

echo "================= PHASE 1 (install) ================="
cat -v sp_p1.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'FAT32|INSTALL|GRUB|FONT|Error|error|fail|\[\*\]|Vortex' | head -60
echo "================= PHASE 2 (hdd boot) ================="
cat -v sp_p2.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -E 'Vortex|GRUB|FAT32|FONT|VBE|INSTALL|error|fail|Shell|\[ok|\(' | head -80
echo "================= done ================="