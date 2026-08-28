#!/bin/bash
# 诊断 v2: 全部引导逻辑塞进 -c embedded.cfg, 不依赖 normal 二次读 grub.cfg。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gbemb; rm -rf "$T"; mkdir -p "$T"/md/boot/grub; cd "$T"

# memdisk 内容: 内核 + grub.cfg(以备 configfile 用, 但引导靠 embedded)
cp "$SRC/iso/boot/kernel.bin" md/boot/kernel.bin
cat > md/boot/grub/grub.cfg <<'EOF'
multiboot2 /boot/kernel.bin
boot
EOF
# 嵌入式完整引导
cat > embedded.cfg <<'EOF'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
set root=(memdisk)
set prefix=(memdisk)/boot/grub
multiboot2 /boot/kernel.bin
boot
EOF
( cd md && tar czf ../memdisk.tar.gz boot )

for MODS in "serial terminal normal boot multiboot2 memdisk" "serial terminal boot multiboot2 memdisk" "serial terminal normal multiboot2 boot"; do
  lbl=$(echo "$MODS" | tr ' ' '_')
  grub-mkimage -O i386-pc -p /boot/grub -c embedded.cfg -o "core_$lbl.img" -m memdisk.tar.gz $MODS >o2 2>e2
  rc=$?; sz=0; [ -f "core_$lbl.img" ] && sz=$(stat -c%s "core_$lbl.img")
  echo "build [$MODS] rc=$rc size=$sz"
  [ $rc -ne 0 ] && tail -1 e2
done

# 组盘并测第一个成功构建
CORE=$(ls core_*.img 2>/dev/null | head -1)
echo "using core: $CORE (size $(stat -c%s "$CORE" 2>/dev/null))"
dd if=/dev/zero of=disk.img bs=512 count=8192 status=none
dd if=/usr/lib/grub/i386-pc/boot.img of=disk.img bs=512 conv=notrunc status=none
dd if="$CORE" of=disk.img bs=512 seek=1 conv=notrunc status=none

rm -f sp.in sp.out; mkfifo sp.in sp.out
( cat < sp.out > sp.cap ) & CAP=$!
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -boot c \
  -machine pc -cpu qemu64 -smp 1 -m 64M -display none \
  -chardev pipe,id=sp0,path=sp -serial chardev:sp0 -no-reboot -no-shutdown &
QP=$!
sleep 12
kill "$CAP" 2>/dev/null || true; kill "$QP" 2>/dev/null || true
echo "=== SERIAL bytes: $(wc -c < sp.cap 2>/dev/null) ==="
cat -v sp.cap 2>/dev/null | sed -E 's/\^\[\[[0-9;]*[A-Za-z]//g' | head -50
echo "=== done ==="