#!/bin/bash
# 端到端验证：GRUB 手工装盘(无光驱)从硬盘引导 multiboot2 内核
# boot.img -> 扇区0, core.img -> 扇区1, FAT32 分区自 LBA2048,
# 分区内放 /boot/grub/grub.cfg 与 /boot/kernel.bin。
set -e
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
T=/tmp/gboot
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os

echo "== 工具检查 =="
for t in mkfs.fat mkfs.vfat mkdosfs parted mcopy grub-mkimage; do
  if command -v "$t" >/dev/null 2>&1; then echo "$t => OK"; else echo "$t => NO"; fi
done
ls -la /usr/lib/grub/i386-pc/ 2>/dev/null | grep -E 'boot.img|core' || true

cd "$T"

echo "== 1) 生成 core.img =="
grub-mkimage -O i386-pc -d /usr/lib/grub/i386-pc -p /boot/grub \
  -o core.img biosdisk part_msdos fat configfile normal boot multiboot2
echo "core.img size: $(stat -c%s core.img)"

echo "== 2) 创建 64MB 磁盘与 MBR 分区(自 LBA2048) =="
dd if=/dev/zero of=disk.img bs=1M count=64 status=none

# 手工写 MBR：分区1 = FAT32, 起始2048, 大小 = 64MB-2048
python3 - <<'PY'
import struct
img = bytearray(512)
# 引导标志 + CHS(忽略) + 类型 0x0C + 起始LBA 2048 + 大小
pe = 446
img[pe+0]=0x80; img[pe+1]=0xFE; img[pe+2]=0xFF; img[pe+3]=0xFF
img[pe+4]=0x0C; img[pe+5]=0xFE; img[pe+6]=0xFF; img[pe+7]=0xFF
img[pe+8:pe+12]=struct.pack('<I', 2048)
img[pe+12:pe+16]=struct.pack('<I', 131072-2048)
img[510]=0x55; img[511]=0xAA
open('mbr.img','wb').write(img)
PY
dd if=mbr.img of=disk.img bs=512 count=1 conv=notrunc status=none

echo "== 3) boot.img 写扇区0(合并MBR)、core.img 写扇区1 =="
# 保留分区表：取 grub boot.img 前446字节写进 MBR 引导代码区
# 简化：直接先读写合并
cp disk.img disk.bak
dd if=/dev/zero of=sec0.img bs=512 count=1 status=none
python3 - <<'PY'
import struct
mbr = bytearray(open('disk.bak','rb').read(512))
boot = bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
# 分区表(446..510)保留自 mbr，引导代码(0..446)用 boot.img
mbr[0:446] = boot[0:446]
mbr[510]=0x55; mbr[511]=0xAA
open('sec0.img','wb').write(mbr)
PY
dd if=sec0.img of=disk.img bs=512 count=1 conv=notrunc status=none
dd if=core.img of=disk.img bs=512 seek=1 conv=notrunc status=none

echo "== 4) 创建 FAT32 分区并填充 /boot =="
# losetup 需要 root，WSL 可能无权限；改用 mtools 或直接 raw 写不现实。
# 用 sfdisk + mkfs 需要 root。这里探查是否可用 sudo。
if command -v mformat >/dev/null 2>&1; then
  echo "mformat available"
fi

echo "== 5) 检查 MBR 签名 =="
xxd -l 2 -s 510 disk.img
echo "===== finished boot.img + core.img install (分区填充待 root) ====="