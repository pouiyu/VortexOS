#!/bin/bash
# v16：抓取 sfdisk 生成分区的 MBR 精确字节 + 完整串口，作为内核 fat32Format 参照。
set -u
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
T=/tmp/gb16
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
cd "$T"
dd if=/dev/zero of=disk.img bs=1M count=64 status=none
printf '2048,129024,0x0C\n' | sfdisk disk.img >/dev/null 2>&1
python3 - <<'PY'
img=open('disk.img','rb').read(512)
print('=== MBR 分区项 hex ===')
for i in range(4):
    p=i*16+446
    e=img[p:p+16]
    print(f'entry{i}: {e.hex()}')
print('label-id(440-444):', img[440:444].hex())
print('sig:', img[510:512].hex())
print('--- 分区1 字段解码 ---')
e=img[446:462]
est,LBA=0,0
print('boot:',hex(e[0]))
print('CHS start:',e[1],e[2],e[3])
print('type:',hex(e[4]))
print('CHS end:',e[5],e[6],e[7])
import struct
print('start LBA:',struct.unpack_from('<I',e,8)[0])
print('size:',struct.unpack_from('<I',e,12)[0])
PY
echo "===== end ====="