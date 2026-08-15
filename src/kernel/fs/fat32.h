#ifndef _KERNEL_FS_FAT32_H
#define _KERNEL_FS_FAT32_H

#include <stdint.h>
#include <stdbool.h>

#define SECTOR_SIZE      512
#define FAT32_CLUSTER_END  0x0FFFFFF8
#define FAT32_CLUSTER_FREE 0x00000000
#define FAT32_CLUSTER_BAD  0x0FFFFFF7

#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20

typedef struct {
    uint8_t  jmpBoot[3];
    uint8_t  oemName[8];
    uint16_t bytesPerSector;
    uint8_t  sectorsPerCluster;
    uint16_t reservedSectorCount;
    uint8_t  numFats;
    uint16_t rootEntryCount;
    uint16_t totalSectors16;
    uint8_t  mediaType;
    uint16_t sectorsPerFat16;
    uint16_t sectorsPerTrack;
    uint16_t numHeads;
    uint32_t hiddenSectors;
    uint32_t totalSectors32;

    // FAT32 扩展
    uint32_t sectorsPerFat;
    uint16_t extFlags;
    uint16_t fsVersion;
    uint32_t rootCluster;
    uint16_t fsInfo;
    uint16_t backupBootSector;
    uint8_t  reserved[12];
    uint8_t  driveNumber;
    uint8_t  reserved1;
    uint8_t  bootSignature;
    uint32_t volumeId;
    uint8_t  volumeLabel[11];
    uint8_t  fsType[8];
} __attribute__((packed)) fat32BootSector;

typedef struct {
    uint8_t  name[11];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  creationTimeTenths;
    uint16_t creationTime;
    uint16_t creationDate;
    uint16_t lastAccessDate;
    uint16_t firstClusterHigh;
    uint16_t lastWriteTime;
    uint16_t lastWriteDate;
    uint16_t firstClusterLow;
    uint32_t fileSize;
} __attribute__((packed)) fat32DirEntry;

typedef struct {
    bool valid;                  // FAT32 是否初始化成功
    uint16_t bytesPerSector;     // 每扇区字节数（通常 512）
    uint8_t  sectorsPerCluster;  // 每簇扇区数（如 8）
    uint32_t sectorsPerFat;      // FAT 表占用扇区数
    uint32_t rootCluster;        // 根目录起始簇号（通常 2）
    uint32_t dataStartSector;    // 数据区起始扇区
    uint32_t rootDirStartSector; // 根目录起始扇区
    uint32_t partitionOffset;    // 分区在磁盘上的起始扇区
    uint16_t reservedSectorCount;// 保留扇区数
    uint8_t  numFats;            // FAT 表份数（通常 2）
} fat32Volume;

bool fat32Init(fat32Volume* vol);
bool fat32OpenFile(fat32Volume* vol, const char* path, void** data, uint32_t* size);
bool fat32ListDir(fat32Volume* vol, const char* path, char* buf, int bufSize);

extern fat32Volume fsVolume;

#endif