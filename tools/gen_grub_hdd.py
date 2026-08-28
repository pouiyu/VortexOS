#!/usr/bin/env python3
# 构建期生成 GRUB 装盘镜像的扇区0(hdd_mbr.bin)：
#   boot.img 引导代码 + core.img 起始LBA(=1) 字段 + MBR 分区表 + 0x55AA 签名。
# 分区表常量必须与内核 fat32Format 一致：type 0x0C, start LBA 2048, size 129024。
# 输出 512 字节，installSystem 会把它写到硬盘扇区 0。
import sys, struct

GRUB_BOOT_IMG = "/usr/lib/grub/i386-pc/boot.img"
CORE_START_LBA = 1          # core.img 写到扇区 1
PART_OFFSET = 2048          # FAT32 分区起始 LBA(与 fat32Format 的 FMT_PART_OFFSET 一致)
PART_SECTORS = 129024       # 131072 - 2048(与 fat32Format 的 partSectors 一致)

out = sys.argv[1]
boot = bytearray(open(GRUB_BOOT_IMG, "rb").read(512))
if len(boot) != 512:
    sys.stderr.write("boot.img not 512 bytes\n"); sys.exit(1)

struct.pack_into("<I", boot, 0x5c, CORE_START_LBA)   # boot.img 的 core 起始扇区

pe = 446                                              # 分区表第一项
boot[pe+0] = 0x80                                     # boot 标志
boot[pe+1] = 0xFE; boot[pe+2] = 0xFF; boot[pe+3] = 0xFF  # CHS(part_msdos 忽略)
boot[pe+4] = 0x0C                                     # FAT32 LBA
boot[pe+5] = 0xFE; boot[pe+6] = 0xFF; boot[pe+7] = 0xFF
struct.pack_into("<I", boot, pe+8,  PART_OFFSET)
struct.pack_into("<I", boot, pe+12, PART_SECTORS)
boot[510] = 0x55; boot[511] = 0xAA

open(out, "wb").write(boot)
sys.stderr.write(f"wrote {out}: {len(boot)} bytes, core LBA={CORE_START_LBA}\n")