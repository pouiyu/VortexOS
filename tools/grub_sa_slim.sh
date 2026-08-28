#!/bin/bash
# mkstandalone 双精简(--modules+--install-modules) 拼 480KB。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gbsa; rm -rf "$T"; mkdir -p "$T"; cd "$T"
printf x > tiny.pf2
cat > grub.cfg <<'EOF'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
set root=(memdisk)
set prefix=(memdisk)/boot/grub
multiboot2 /boot/kernel.bin
boot
EOF
MODS='serial terminal normal boot multiboot2 configfile'
for v in 1 2 3; do
  rm -f c.img e2
  grub-mkstandalone -O i386-pc -o c.img --themes= --fonts= --locales= \
    --modules="$MODS" --install-modules="$MODS" \
    /boot/grub/grub.cfg=grub.cfg /boot/kernel.bin="$SRC/iso/boot/kernel.bin" \
    /boot/grub/fonts/unicode.pf2=tiny.pf2 >o2 2>e2
  rc=$?; sz=0; [ -f c.img ] && sz=$(stat -c%s c.img)
  echo "v$v rc=$rc size=$sz cap=491520"
  [ $rc -ne 0 ] && tail -1 e2
done
echo done