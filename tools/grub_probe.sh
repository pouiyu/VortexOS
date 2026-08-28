#!/bin/bash
for t in grub-install grub-mkimage grub-bios-setup grub-mkrescue objcopy ld i686-elf-gcc gcc qemu-system-x86_64; do
  if command -v "$t" >/dev/null 2>&1; then
    echo "$t => $(command -v "$t")"
  else
    echo "$t => NOT FOUND"
  fi
done
echo '--- /usr/lib/grub ---'
ls /usr/lib/grub 2>/dev/null
uname -a