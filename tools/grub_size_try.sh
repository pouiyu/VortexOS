#!/bin/bash
# 寻找能满足 memdisk core.img <= 480KB(0x78000) 的模块/字体组合。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gbsz; rm -rf "$T"; mkdir -p "$T"; cd "$T"
printf 'placeholder' > tiny.pf2

build() { # $1=label  $@=额外文件名=源 对
  local label="$1"; shift
  local ext=( "$@" )
  rm -f c.img e2 o2
  grub-mkstandalone -O i386-pc -o c.img --themes= \
    --install-modules='serial terminal normal boot multiboot2 configfile' \
    /boot/grub/grub.cfg=grub.cfg "$@" >o2 2>e2
  local rc=$?
  local sz=0
  [ -f c.img ] && sz=$(stat -c%s c.img)
  printf '%-22s rc=%s size=%s cap=491520\n' "$label" "$rc" "$sz"
  [ $rc -ne 0 ] && tail -1 e2
}

# 变体 A：无字体 + 无内核(仅看模块基线)
build A-nokernel-nofont
# 变体 B：无字体 + 内核
build B-nofont-kernel /boot/kernel.bin="$SRC/iso/boot/kernel.bin"
# 变体 C：tiny.pf2 + 内核
build C-tinyfont-kernel /boot/kernel.bin="$SRC/iso/boot/kernel.bin" /boot/grub/fonts/unicode.pf2=tiny.pf2
echo done