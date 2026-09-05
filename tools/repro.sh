#!/bin/bash
# 复现 graphic_main 崩溃：boot 后自动按 '3'(Graphic) 进入图形模式。
cd "$(dirname "$0")/.."
rm -f /tmp/vortex_repro.log /tmp/vm.log
# QEMU: serial→文件, 串口0见; monitor→ tcp 5555 (sendkey)
qemu-system-x86_64 \
  -cdrom vortexos.iso -hda disk.img -m 256M -boot d \
  -machine pc -cpu qemu64 -smp 1 -no-reboot -no-shutdown \
  -display none -serial stdio \
  -monitor tcp:127.0.0.1:5555,server,nowait \
  -device qemu-xhci -device usb-kbd -device usb-mouse \
  > /tmp/vortex_repro.log 2>&1 &
QPID=$!

# 等待系统到主菜单(等 VBE 初始化出现在日志)
for i in $(seq 1 40); do
  if grep -q "\[VBE\] init ok" /tmp/vortex_repro.log; then break; fi
  sleep 0.5
done
sleep 1
# 通过 monitor 发送 Down Down Enter 选中 Graphic 并进入
exec 3<>/dev/tcp/127.0.0.1/5555
printf 'sendkey down\n' >&3
sleep 0.3
printf 'sendkey down\n' >&3
sleep 0.3
printf 'sendkey ret\n' >&3
exec 3<&-
# 让系统进入图形后运行数秒捕获崩溃
for i in $(seq 1 24); do
  sleep 0.5
  if grep -q "EXCEPTION" /tmp/vortex_repro.log; then break; fi
done
# 退出 QEMU
exec 3<>/dev/tcp/127.0.0.1/5555
printf 'quit\n' >&3
exec 3<&-
wait $QPID 2>/dev/null
echo "===== serial log tail ====="
tail -40 /tmp/vortex_repro.log