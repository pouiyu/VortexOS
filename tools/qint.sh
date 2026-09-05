#!/bin/bash
cd "$(dirname "$0")/.."
echo "=== guest serial exception ==="
tail -4 /tmp/vr.log
echo ""
echo "=== qemu NMI / exception / reset events ==="
grep -inE "exception_|check_exception|nmi|DMA|triple|reset|machine" /tmp/qint.log | tail -40