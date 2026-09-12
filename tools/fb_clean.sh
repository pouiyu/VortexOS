#!/bin/bash
# 干净复现：无 Bochs dispi 真机路径（cirrus），确认回退后保持文本模式。
pkill -9 -f qemu-system; sleep 1
cd "$(dirname "$0")/.."
rm -f /tmp/fb_repro.log /tmp/fb_shot.ppm
PORT=$((5800 + RANDOM % 300))
qemu-system-x86_64 \
  -cdrom vortexos.iso -m 256M -boot d \
  -machine pc -cpu qemu64 -smp 1 -no-reboot -no-shutdown \
  -display none -vga cirrus -serial stdio \
  -monitor tcp:127.0.0.1:$PORT,server,nowait \
  -device qemu-xhci -device usb-kbd -device usb-mouse \
  > /tmp/fb_repro.log 2>&1 &
QPID=$!
for i in $(seq 1 40); do
  if grep -qE "WIZARD|bootloader fb|init failed" /tmp/fb_repro.log; then break; fi
  sleep 0.5
done
sleep 2
exec 3<>/dev/tcp/127.0.0.1/$PORT
printf 'screendump /tmp/fb_shot.ppm\n' >&3
sleep 0.3
printf 'quit\n' >&3
exec 3<&-
wait $QPID 2>/dev/null
echo "===== VBE 相关 ====="
grep -iE "VBE\]|bootloader|FB\]" /tmp/fb_repro.log
echo "===== 是否使用 bootloader fb(应为 0) ====="
grep -c "bootloader fb" /tmp/fb_repro.log