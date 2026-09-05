#!/bin/bash
cd "$(dirname "$0")/.."
echo "=== DBG / VBE / EXCEPTION lines ==="
grep -E "DBG|EXCEPTION|\[VBE\]" /tmp/vr.log
echo "=== last 8 lines ==="
tail -8 /tmp/vr.log