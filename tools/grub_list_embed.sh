#!/bin/bash
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
T=/tmp/gblist; rm -rf "$T"; mkdir -p "$T"; cd "$T"
cat > grub.cfg <<'EOF'
serial --unit=0 --speed=38400 --word=8 --parity=no --stop=1
terminal_input serial
terminal_output serial
set root=(memdisk)
set prefix=(memdisk)/boot/grub
multiboot2 /boot/kernel.bin
boot
EOF
grub-mkstandalone -O i386-pc -o c.img --themes= \
  --install-modules='iso9660 serial terminal normal boot multiboot2 configfile' \
  /boot/grub/grub.cfg=grub.cfg /boot/kernel.bin="$SRC/iso/boot/kernel.bin" --verbose >v.log 2>&1
echo "size=$(stat -c%s c.img)"
echo "--- big files in memdisk (size of source assets) ---"
grep -oE '/[^ ]*/[^ <\x27]+\.(mod|pf2|lst|img)[^ ]*' v.log | sed -E 's#.*/##' | sort | uniq -c | sort -rn | head -40
echo "--- any font/pf2 refs ---"
grep -iE 'pf2|font' v.log | head
echo "done"