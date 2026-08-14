#include "fat32.h"
#include <ata.h>
#include <string.h>

static uint8_t sectorBuf[512];

static uint32_t clusterToSector(fat32Volume* vol, uint32_t cluster) {
    return vol->dataStartSector + (cluster - 2) * vol->sectorsPerCluster;
}

static uint32_t readFatEntry(fat32Volume* vol, uint32_t cluster) {
    uint32_t fatOffset = cluster * 4;
    uint32_t fatSector = vol->partitionOffset + vol->reservedSectorCount + (fatOffset / vol->bytesPerSector);
    uint32_t entryOffset = fatOffset % vol->bytesPerSector;

    ataReadSector(fatSector, sectorBuf);
    return *(uint32_t*)(sectorBuf + entryOffset) & 0x0FFFFFFF;
}

bool fat32Init(fat32Volume* vol) {
    ataReadSector(0, sectorBuf);  // MBR

    // 读第一个分区
    uint32_t partitionStart = *(uint32_t*)(sectorBuf + 0x1BE + 8);
    vol->partitionOffset = partitionStart;

    ataReadSector(partitionStart, sectorBuf);
    fat32BootSector* boot = (fat32BootSector*)sectorBuf;

    if (boot->bytesPerSector != 512 || boot->sectorsPerCluster == 0) {
        return false;
    }

    vol->bytesPerSector = boot->bytesPerSector;
    vol->sectorsPerCluster = boot->sectorsPerCluster;
    vol->sectorsPerFat = boot->sectorsPerFat;
    vol->rootCluster = boot->rootCluster;
    vol->reservedSectorCount = boot->reservedSectorCount;
    vol->numFats = boot->numFats;
    vol->dataStartSector = partitionStart + 
        boot->reservedSectorCount + 
        boot->numFats * boot->sectorsPerFat;
    vol->rootDirStartSector = clusterToSector(vol, vol->rootCluster);
    vol->valid = true;
    return true;
}

bool fat32ListDir(fat32Volume* vol, const char* path, char* buf, int bufSize) {
    (void)path;
    if (!vol->valid) return false;

    uint32_t cluster = vol->rootCluster;
    int offset = 0;

    while (cluster < FAT32_CLUSTER_END) {
        uint32_t sector = clusterToSector(vol, cluster);
        for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
            ataReadSector(sector + s, sectorBuf);
            fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;
            for (int i = 0; i < 16; i++) {
                fat32DirEntry* e = &entries[i];
                if (e->name[0] == 0x00) return true;  // 目录结束
                if (e->name[0] == 0xE5) continue;     // 已删除
                if (e->attributes == 0x0F) continue;  // 长文件名

                char name[13];
                int n = 0;
                for (int j = 0; j < 8 && e->name[j] != ' '; j++)
                    name[n++] = e->name[j];
                if (e->name[8] != ' ') {
                    name[n++] = '.';
                    for (int j = 8; j < 11 && e->name[j] != ' '; j++)
                        name[n++] = e->name[j];
                }
                name[n] = '\0';

                if (offset + n + 2 < bufSize) {
                    for (int j = 0; j < n; j++)
                        buf[offset++] = name[j];
                    buf[offset++] = '\n';
                }
            }
        }
        cluster = readFatEntry(vol, cluster);
    }
    buf[offset] = '\0';
    return true;
}