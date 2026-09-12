#!/bin/bash
# 复现真机"无 Bochs dispi"路径：用 cirrus(无 dispi) 引导，GRUB 提供帧缓冲。
cd "$(dirname "$0")/.."
rm -f /tmp/fb_repro.log /tmp/fb_shot.ppm
qemu-system-x86_64 \
  -cdrom vortexos.iso -m 256M -boot d \
  -machine pc -cpu qemu64 -smp 1 -no-reboot -no-shutdown \
  -display none -vga cirrus -serial stdio \
  -monitor tcp:127.0.0.1:5789,server,nowait \
  -device qemu-xhci -device usb-kbd -device usb-mouse \
  > /tmp/fb_repro.log 2>&1 &
QPID=$!

for i in $(seq 1 40); do
  if grep -qE "WIZARD|menu|Menu|Settings|Shell|init failed|bootloader fb" /tmp/fb_repro.log; then break; fi
  sleep 0.5
done
sleep 2
# 抓一帧屏幕后退出
exec 3<>/dev/tcp/127.0.0.1/5789
printf 'screendump /tmp/fb_shot.ppm\n' >&3
sleep 0.3
exec 3<&-

exec 3<>/dev/tcp/127.0.0.1/5789
printf 'quit\n' >&3
exec 3<&-
wait $QPID 2>/dev/null
echo "===== serial tail ====="
tail -50 /tmp/fb_repro.log
echo "===== screenshot size ====="
ls -l /tmp/fb_shot.ppm 2>/dev/null