#!/bin/bash
# 端到端验证前置：检查构建 GRUB 装盘镜像所需的工具
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
for t in mformat mcopy mmd mdir grub-mkimage grub-mkrescue qemu-system-x86_64; do
  command -v "$t" >/dev/null 2>&1 && echo "$t => OK" || echo "$t => NO"
done
echo "---"
ls -la /usr/lib/grub/i386-pc/ 2>/dev/null | grep -E 'boot.img|$(E)' || true