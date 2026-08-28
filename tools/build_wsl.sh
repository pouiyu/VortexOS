#!/bin/bash
# build_wsl.sh —— WSL 内一键构建全链(唯一入口，Windows 下用 `wsl -- bash tools/build_wsl.sh`)：
#   0) 在本 WSL 环境内编译内核(gcc -m32/ld elf_i386/nasm，无需 Windows 原生 make)
#   1) CD 引导 eltorito.img + CD grub.cfg
#   2) 硬盘引导自包含 memdisk core.img + hdd_mbr.bin(boot.img+分区表)
#   3) 自动把 system 运行目录(含字体)拷入 CD
#   4) grub-mkrescue 打包整张 ISO
# 前置依赖: WSL 内具备 gcc/ld/nasm/grub/python3, font/font.bin 已存在。
set -e
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

MODS='serial terminal normal boot multiboot2 configfile'
GRUB_MODS='biosdisk iso9660 part_msdos fat configfile search_fs_uuid search normal boot multiboot2 serial terminal'

echo "[0/5] WSL 内编译内核"
make kernel.bin

echo "[1/5] CD eltorito core.img + 同步内核"
mkdir -p iso/boot/grub iso/grub
cp -f kernel.bin iso/boot/kernel.bin
# 必须用 eltorito 专用核心格式: 普通 i386-pc 核心被 SeaBIOS 从 CD 经
# El Torito 加载后无 CD 感知逻辑, 会在 "Booting from DVD/CD..." 处挂死。
# grub-mkrescue 默认即用 i386-pc-eltorito。
grub-mkimage -O i386-pc-eltorito -p /boot/grub -o iso/boot/grub/eltorito.img $GRUB_MODS

echo "[2/5] 硬盘自包含 memdisk core.img (内核烘焙进镜像)"
grub-mkstandalone -O i386-pc -o iso/grub/core.img \
  --themes= --fonts= --locales= \
  --modules="$MODS" --install-modules="$MODS" \
  /boot/grub/grub.cfg=tools/grub_hdd_memdisk.cfg \
  /boot/kernel.bin=iso/boot/kernel.bin
echo "  core.img size=$(stat -c%s iso/grub/core.img) (cap 491520)"

echo "[3/5] hdd_mbr.bin (boot.img + 分区表, 与 fat32Format 一致)"
python3 tools/gen_grub_hdd.py iso/grub/hdd_mbr.bin

echo "[4/5] 拷入 system 运行目录 + 打包 ISO"
cp -f tools/grub_cd.cfg iso/boot/grub/grub.cfg
# 自动把整个 system 运行目录(含字体)拷入 CD，装盘时 installSystem 会整树落盘
rm -rf iso/system
cp -r system iso/system
# 直接调 xorriso 打包：-J 写入 Joliet(SVD, UTF-16BE 长名) 补充卷描述符，
# 供内核 iso9660.c 装盘时取回完整长文件名，避免 8.3 截断碰撞。
# 其余参数复刻 grub-mkrescue 的 CD/混合引导布局(El Torito + 混合 MBR)。
xorriso -as mkisofs \
  -J \
  -o vortexos.iso \
  -b boot/grub/eltorito.img \
  -no-emul-boot -boot-load-size 4 -boot-info-table \
  --grub2-boot-info \
  --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img \
  --protective-msdos-label \
  -r \
  --sort-weight 0 / --sort-weight 1 /boot \
  iso
echo "DONE: vortexos.iso $(stat -c%s vortexos.iso) bytes"
echo "  iso/grub/core.img     = $(stat -c%s iso/grub/core.img) bytes (装盘 CPU)
  iso/grub/hdd_mbr.bin  = $(stat -c%s iso/grub/hdd_mbr.bin) bytes (扇区0)
  iso/system           = $(du -sh iso/system 2>/dev/null | cut -f1)"