#!/bin/bash
cd "$(dirname "$0")/.."
pkill -f qemu-system-x86_64 2>/dev/null
sleep 2
rm -f /tmp/vs.log /tmp/mon.log
qemu-system-x86_64 \
  -cdrom vortexos.iso -hda disk.img -m 256M -boot d \
  -machine pc -cpu qemu64 -smp 1 -no-reboot -no-shutdown \
  -display none -serial file:/tmp/vs.log \
  -monitor tcp:127.0.0.1:5556,server,nowait \
  > /tmp/mon.log 2>&1 &
QPID=$!

for i in $(seq 1 30); do
  if grep -q "Enable Interrupt" /tmp/vs.log; then break; fi
  sleep 0.5
done
sleep 1

exec 3<>/dev/tcp/127.0.0.1/5556
# 读掉欢迎横幅
exec 3<-
exec 3<>/dev/tcp/127.0.0.1/5556
printf 'sendkey s\n' >&3
printf 'sendkey s\n' >&3
printf 'sendkey ret\n' >&3
exec 3<&-

sleep 3
echo "=== vs.log tail (kbd handler prints up/down/ok) ==="
tail -12 /tmp/vs.log
exec 3<>/dev/tcp/127.0.0.1/5556 2>/dev/null || exit 0
printf 'quit\n' >&3 2>/dev/null
exec 3<&-
kill $QPID 2>/dev/null