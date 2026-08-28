#!/bin/bash
# 原型：grub-mkstandalone 自包含 memdisk core.img（内含内核+grub.cfg，不依赖硬盘分区枚举），
# 安装进磁盘 gap 后从硬盘引导，验证能否拉起内核。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gbsa; rm -rf "$T"; mkdir -p "$T"; cd "$T"

cat > grub.cfg <<'EOFGRUB'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
set root=(memdisk)
set prefix=(memdisk)/boot/grub
multiboot2 /boot/kernel.bin
boot
EOFGRUB

grub-mkstandalone -O i386-pc -o sa_core.img \
  --install-modules='iso9660 serial terminal normal boot multiboot2 configfile' \
  --themes= --compression=xz \
  /boot/grub/grub.cfg=grub.cfg \
  /boot/kernel.bin="$SRC/iso/boot/kernel.bin" >sa.err 2>&1
echo "grub-mkstandalone rc=$? size=$(stat -c%s sa_core.img) (cap 491520)"

# ---- 组装磁盘：先 CD 安装（产生 MBR+FAT 分区+系统文件），再用 sa_core.img 覆盖 gap 的 core.img ----
dd if=/dev/zero of=disk.img bs=1M count=64 status=none
cp "$SRC/vortexos.iso" iso.iso
# Phase1 安装（沿用旧 core.img）
rm -f sp.in sp.out; mkfifo sp.in sp.out
( cat < sp.out > p1.cap ) & c1=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -cdrom iso.iso -boot d \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none -no-reboot -no-shutdown \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 &
q1=$!
sleep 12; kill "$c1" 2>/dev/null; kill "$q1" 2>/dev/null; wait 2>/dev/null
grep -E 'INSTALL|GRUB|FONT' p1.cap | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g'

# 用自包含 core.img 覆盖 gap（保留 MBR 分区表）
dd if=sa_core.img of=disk.img bs=512 seek=1 conv=notrunc status=none
echo "sa_core.img sectors: $(( $(stat -c%s sa_core.img) / 512 ))"

# Phase2 从硬盘引导
rm -f sp.in sp.out; mkfifo sp.in sp.out
( cat < sp.out > p2.cap ) & c2=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none -no-reboot -no-shutdown \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 &
q2=$!
sleep 14; kill "$c2" 2>/dev/null; kill "$q2" 2>/dev/null; wait 2>/dev/null
echo "===== Phase2 (memdisk self-contained GRUB) log ====="
cat -v p2.cap | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | grep -av '^$' | tail -60
echo "===== done ====="