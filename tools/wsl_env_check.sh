#!/bin/bash
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
for t in grub-mkimage grub-bios-setup qemu-system-x86_64 mformat mcopy mmd mdir parted python3 sfdisk mtools mkfs.fat; do
  if command -v "$t" >/dev/null 2>&1; then echo "OK  $t => $(command -v $t)"; else echo "NO  $t"; fi
done
echo "---- grub i386-pc 核心文件 ----"
ls -la /usr/lib/grub/i386-pc/ 2>/dev/null | grep -E 'boot.img|core.img|mod' | head || echo "no /usr/lib/grub/i386-pc"
echo "---- 项目 kernel.bin ----"
ls -la "$SRC/kernel.bin" 2>/dev/null || echo "project has no kernel.bin"