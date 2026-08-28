#!/bin/bash
# 用 grub-mkimage -m memdisk.tar.gz 直接构造自包含 core.img，绕过 mkstandalone 的大体积。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gbmi; rm -rf "$T"; mkdir -p "$T"/md/boot/grub; cd "$T"

# 内核与 grub.cfg 放在 tar 的 /boot 下; memdisk 文件系统根即 tar 根
cp "$SRC/iso/boot/kernel.bin" md/boot/kernel.bin
cat > md/boot/grub/grub.cfg <<'EOF'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
set root=(memdisk)
set prefix=(memdisk)/boot/grub
multiboot2 /boot/kernel.bin
boot
EOF
cat > embedded.cfg <<'EOF'
set root=(memdisk)
set prefix=(memdisk)/boot/grub
EOF
( cd md && tar czf ../memdisk.tar.gz boot )
echo "memdisk.tar.gz size: $(stat -c%s memdisk.tar.gz)"

build_mkimage() { # $1=label  $@=modules
  local label="$1"; shift
  rm -f "c_$label.img" e2; :>e2
  grub-mkimage -O i386-pc -p /boot/grub -c embedded.cfg -o "c_$label.img" -m memdisk.tar.gz "$@" >o2 2>e2
  local rc=$?; local sz=0; [ -f "c_$label.img" ] && sz=$(stat -c%s "c_$label.img")
  printf '%-22s rc=%s size=%s cap=491520\n' "$label" "$rc" "$sz"
  [ $rc -ne 0 ] && tail -1 e2
}

build_mkimage v1 serial terminal normal boot multiboot2 configfile
build_mkimage v2 terminal normal boot multiboot2 configfile
echo done