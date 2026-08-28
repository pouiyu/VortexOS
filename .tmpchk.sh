#!/bin/bash
IMG=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os/disk.img
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os/system/images/wallpaper.bmp
CONF=$HOME/.mtoolsrc
printf 'drive d: file="%s" offset=1048576\n' "$IMG" > "$CONF"
echo "=== disk.img mtime/size ==="
ls -l disk.img 2>/dev/null || ls -l installtest.img
echo "=== images dir ==="
mdir d:/system/images 2>&1
echo "=== copy wallpaper and compare ==="
mcopy -o d:/system/images/wallpape.bmp /tmp/wall_disk.bmp 2>&1
if [ -f /tmp/wall_disk.bmp ]; then
  echo "on-disk size = $(stat -c%s /tmp/wall_disk.bmp)"
  echo "src   size   = $(stat -c%s "$SRC")"
  sha256sum /tmp/wall_disk.bmp "$SRC"
else
  echo "wallpaper MISSING on disk"
fi