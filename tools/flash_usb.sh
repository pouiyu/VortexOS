#!/bin/bash
# flash_usb.sh —— 把 vortexos.iso(混合引导 ISO)烧写到 USB 启动盘。
#
# 用法:  wsl -- bash tools/flash_usb.sh /dev/sdX
#   其中 /dev/sdX 是 WSL 里看到的你的 U 盘设备(务必先用 lsblk 确认，选错会覆盖其它盘)。
#
# vortexos.iso 是 hybrid ISO(El Torito + 内嵌 MBR)，直接 dd 整盘写入即可，
# 真机 BIOS/UEFI 都能引导；不需要再手动做"ISO 模式 vs DD 模式"的选择。
set -euo pipefail

cd "$(dirname "$0")/.."
ISO="$PWD/vortexos.iso"

if [ $# -ne 1 ]; then
    echo "用法: wsl -- bash tools/flash_usb.sh /dev/sdX"
    echo "先运行以下命令找到 U 盘设备(TRAN=usb 的才是 U 盘):"
    echo "  lsblk -o NAME,SIZE,MODEL,TRAN /dev/sd?"
    exit 1
fi

DEV="$1"
case "$DEV" in
    /dev/sd*|/dev/hd*|/dev/nvme*) ;;
    *) echo "不支持的设备路径: $DEV"; exit 1 ;;
esac

[ -f "$ISO" ] || { echo "未找到 $ISO，请先执行: wsl -- bash tools/build_wsl.sh"; exit 1; }

echo "即将整盘写入以下设备:"
lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINT "$DEV" 2>/dev/null || true
echo "  源 ISO: $ISO ($(stat -c%s "$ISO") 字节)"
echo "  !! 该设备的现有内容将被完全覆盖 !!"
echo
read -rp "确认目标是 U 盘、且可以覆盖？输入 yes 继续: " ans
if [ "$ans" != "yes" ]; then
    echo "已取消。"
    exit 1
fi

# 解除挂载冲突，避免写入期间文件系统被占用
for p in "$DEV"?*; do
    [ -e "$p" ] && umount "$p" 2>/dev/null || true
done

echo "正在写入 $ISO -> $DEV ..."
dd if="$ISO" of="$DEV" bs=4M conv=fsync status=progress
sync
echo "完成。已把 $ISO 烧写到 $DEV，可拔出插入真机引导。"