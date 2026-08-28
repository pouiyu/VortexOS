#!/bin/bash
# 端到端验证：标准 grub-bios-setup 装盘至磁盘镜像，FAT32 分区(自2048)，
# 无光驱(-boot c)引导 multiboot2 内核，串口(38400)观察输出。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
set -e
T=/tmp/gboot4
rm -rf "$T"; mkdir -p "$T"
SRC=/mnt/c/Users/Administrator/Documents/OperatingSystem/vortex-os
GRUB=/usr/lib/grub/i386-pc
cp "$SRC/kernel.bin" "$T/kernel.bin"
cp "$SRC/tools/grub_test.cfg" "$T/grub.cfg"
cd "$T"

echo "== 1) 建 64MB 磁盘 =="
dd if=/dev/zero of=disk.img bs=1M count=64 status=none

echo "== 2) 写 MBR 分区表(单 FAT32 分区自 LBA2048) =="
python3 - <<'PY'
import struct
mbr=bytearray(512)
pe=446
mbr[pe+0]=0x80; mbr[pe+1]=0xFE; mbr[pe+2]=0xFF; mbr[pe+3]=0xFF; mbr[pe+4]=0x0C
mbr[pe+5]=0xFE; mbr[pe+6]=0xFF; mbr[pe+7]=0xFF
mbr[pe+8:pe+12]=struct.pack('<I',2048)
mbr[pe+12:pe+16]=struct.pack('<I',131072-2048)
mbr[510]=0x55; mbr[511]=0xAA
open('sector0.bin','wb').write(mbr)
PY
dd if=sector0.bin of=disk.img bs=512 count=1 conv=notrunc status=none

echo "== 3) mtools 创建 FAT32(偏移2048) 并填 /boot =="
OFFSET=$((2048*512))
mformat -i disk.img@@$OFFSET -F -c 8 -h 255 -s 63 -T $((131072-2048)) ::
mmd -i disk.img@@$OFFSET ::/boot
mmd -i disk.img@@$OFFSET ::/boot/grub
mcopy -i disk.img@@$OFFSET grub.cfg ::/boot/grub/
mcopy -i disk.img@@$OFFSET kernel.bin ::/boot/
echo "-- 内容 --"
mdir -i disk.img@@$OFFSET ::/boot/grub
mdir -i disk.img@@$OFFSET ::/boot

echo "== 4) 标准 grub-bios-setup 装盘(注意：会重写MBR引导代码，需先留存分区表) =="
# grub-bios-setup 需要一个已有的 boot.img:core 组合。它读 core.img 文件，
# 生成 boot.img 写扇区0，core.img 写扇区1起。
cp /usr/lib/grub/i386-pc/core.img core_min.empty 2>/dev/null || true
# 先用 grub-mkimage 生成带模块的 core.img
grub-mkimage -O i386-pc -d "$GRUB" -p /boot/grub \
  -o core.img biosdisk part_msdos fat configfile normal boot multiboot2
echo "core.img size: $(stat -c%s core.img)"

echo "== 5) 手工仿照安装器：boot.img 写扇区0(合并分区表) + core.img 写扇区1.. =="
# GRUB boot.img 的 core 起始地址字段在偏移 0x1c0-0x1c3? 直接观察其值:
python3 - <<'PY'
b=open('/usr/lib/grub/i386-pc/boot.img','rb').read(512)
import struct
for off in (0x1C0,0x1B8,0x1B0,0x18,0x1C):
    print(f'0x{off:x} u32=0x{struct.unpack_from("<I",b,off)[0]:08x}')
print('boot_span@0x5c?', b[0x5c])
print('devar': 'tail2')
PY

echo "== 6) 构造装盘镜像 install.img：扇区0=boot.img+分区表, 1..=core.img =="
python3 - <<'PY'
import struct
# boot.img 完整是512字节：0..0x1BD是引导代码+数据，0x1BE..0x1FD是分区表占位
# 标准安装器会把 core.img 起始 LBA 写入 boot.img 偏移 0x1B0-0x1B7(8字节: 首块扇区数+位置)
boot=bytearray(open('/usr/lib/grub/i386-pc/boot.img','rb').read(512))
core=open('core.img','rb').read()
print('boot tail 1b0:', boot[0x1B0:0x1C0].hex())
print('boot tail 1c0:', boot[0x1C0:0x1D0].hex())
# 建筑磁盘镜像：boot.img 到扇区0
img=bytearray()
img+=boot
n=(len(core)+511)//512
for i in range(n):
    img+=core[i*512:(i+1)*512].ljust(512,b'\x00')
# 分区表在扇区0的0x1BE地址(446)，而 boot.img 那里可能是其代码。若 boot.img 该区非零则冲突。
print('boot 0x1BE..0x1BF:', boot[0x1BE:0x1C0].hex(), 'nonzero if boot code')
open('install.img','wb').write(bytes(img))
print('install.img size:', len(img), 'garb')
PY
echo "== done building =="
ls -la install.img 2>&1