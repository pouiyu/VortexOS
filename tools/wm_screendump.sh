#!/bin/bash
# wm_screendump.sh —— 无头跑进图形模式(用户 GUI 程序)并 screendump 抓屏，供肉眼排障。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
WAIT="${1:-6}"
constSER=/tmp/wm_ser.log
rm -f "$constSER" "$ROOT/wm_shot.png"
(
  sleep "$WAIT"
  echo "sendkey y"; sleep 1
  echo "sendkey y"; sleep 1
  echo "sendkey y"; sleep 25
  echo "sendkey s"; sleep 1
  echo "sendkey s"; sleep 1
  echo "sendkey ret"
  sleep 5
  echo "screendump wm_shot.png"
  sleep 1
  echo "quit"
) | qemu-system-x86_64 \
  -cdrom vortexos.iso -hda disk.img -m 256M -boot d \
  -machine pc -cpu qemu64 -smp 1 -vga std \
  -display none -serial file:"$constSER" -monitor stdio >/tmp/wm_mon.log 2>&1

echo "===== SERIAL tail ====="
tail -25 "$constSER"
echo "===== shot ====="
ls -la "$ROOT/wm_shot.png" 2>/dev/null