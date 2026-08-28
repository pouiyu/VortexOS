#!/bin/bash
cd /tmp/gboot7 || exit 1
echo "== ls =="
ls -la
echo "== file core.img =="
file core.img
echo "== grep EMBED_ALIVE (raw bytes) =="
grep -abo "EMBED_ALIVE" core.img | head
echo "== readelf sections (text/data/note) =="
readelf -h core.img 2>/dev/null | head -20
readelf -S core.img 2>/dev/null | grep -Ei "\.(config|prefix|bss|data|rodata|sbat|note)" 
echo "== strings tail ==="
strings core.img | tail -30