#!/bin/bash
# wm_headless_test.sh —— 无头 sanity for the window system.
#   boot -> (update wizard if disk differs: press y) -> main menu
#   -> DOWN DOWN ENTER (select Graphic) -> expect "[WM] window created" in serial, no EXCEPTION.
# 用法: wsl -- bash tools/wm_headless_test.sh <boot-wait>
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

WAIT="${1:-6}"
constSER=/tmp/wm_serial.log
constMON=/tmp/wm_mon.log
rm -f "$constSER" "$constMON"

# 生产者: 先等引导; 连发 'y' 应答可能出现的"更新系统"向导; 再等更新+重启;
# 然后 DOWN x2 + ENTER 进入 Graphic; 留时间打印 [WM]。
(
  sleep "$WAIT"
  echo "sendkey y"
  sleep 1
  echo "sendkey y"
  sleep 1
  echo "sendkey y"
  sleep 25            # 若处向导/磁盘更新+重启
  echo "sendkey s"
  sleep 1
  echo "sendkey s"
  sleep 1
  echo "sendkey ret"
  sleep 5             # 留给用户 GUI 程序创建窗口
  echo "quit"         # 关 QEMU, 结束测试
) | qemu-system-x86_64 \
  -cdrom vortexos.iso -hda disk.img -m 256M -boot d \
  -machine pc -cpu qemu64 -smp 1 -vga std \
  -display none -serial file:"$constSER" -monitor stdio \
  >"$constMON" 2>&1

echo "===== MONITOR (input echo/errors) ====="
cat "$constMON" 2>/dev/null
echo "===== SERIAL ====="
cat "$constSER" 2>/dev/null
echo "===== END ====="

if grep -q "<<< EXCEPTION" "$constSER"; then
  echo "RESULT: FAIL (exception found)"
  exit 1
fi
if ! grep -q "\[WM\] window created" "$constSER"; then
  echo "RESULT: FAIL (user GUI program did not create a window)"
  exit 1
fi
echo "RESULT: OK"