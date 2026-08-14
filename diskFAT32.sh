# 创建 64MB FAT32 镜像
dd if=/dev/zero of=disk.img bs=1M count=64
mkfs.fat -F 32 disk.img
