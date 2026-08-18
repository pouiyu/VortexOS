#include "fat32.h"
#include <ata.h>
#include <string.h>
#include "path.h"
#include "file.h"

static uint8_t sectorBuf[512];

uint32_t clusterToSector(fat32Volume* vol, uint32_t cluster) {
    return vol->dataStartSector + (cluster - 2) * vol->sectorsPerCluster;
}

uint32_t readFatEntry(fat32Volume* vol, uint32_t cluster) {
    uint32_t fatOffset = cluster * 4;
    uint32_t fatSector = vol->partitionOffset + vol->reservedSectorCount + (fatOffset / vol->bytesPerSector);
    uint32_t entryOffset = fatOffset % vol->bytesPerSector;

    ataReadSector(fatSector, sectorBuf);
    return *(uint32_t*)(sectorBuf + entryOffset) & 0x0FFFFFFF;
}

// 从 UTF-16LE 提取 ASCII 字符
static char utf16ToAscii(uint16_t utf16) {
    if (utf16 < 0x80) {
        return (char)utf16;
    }
    return '?';
}

static int parseLfn(fat32LfnEntry* lfn, char* output) {
    int idx = 0;   // 改成从 0 开始，不管 order

    for (int i = 0; i < 5 && idx < 13; i++) {
        if (lfn->name1[i] == 0x0000 || lfn->name1[i] == 0xFFFF) {
            output[idx] = '\0';
            return idx;
        }
        output[idx++] = utf16ToAscii(lfn->name1[i]);
    }
    for (int i = 0; i < 6 && idx < 13; i++) {
        if (lfn->name2[i] == 0x0000 || lfn->name2[i] == 0xFFFF) {
            output[idx] = '\0';
            return idx;
        }
        output[idx++] = utf16ToAscii(lfn->name2[i]);
    }
    for (int i = 0; i < 2 && idx < 13; i++) {
        if (lfn->name3[i] == 0x0000 || lfn->name3[i] == 0xFFFF) {
            output[idx] = '\0';
            return idx;
        }
        output[idx++] = utf16ToAscii(lfn->name3[i]);
    }

    output[idx] = '\0';
    return idx;
}

bool fat32Init(fat32Volume* vol) {
    ataReadSector(0, sectorBuf);

    // 检查 MBR 签名（0x55AA）
    uint32_t partitionStart = 0;
    if (sectorBuf[510] == 0x55 && sectorBuf[511] == 0xAA) {
        // 有 MBR，读第一个分区起始块
        partitionStart = *(uint32_t*)(sectorBuf + 0x1BE + 8);
    } else {
        // 无分区表，FAT32 从块 0 开始
        partitionStart = 0;
    }
    vol->partitionOffset = partitionStart;

    ataReadSector(partitionStart, sectorBuf);
    fat32BootSector* boot = (fat32BootSector*)sectorBuf;

    if (boot->bytesPerSector != 512 || boot->sectorsPerCluster == 0) {
        vol->valid = false;
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

bool fat32PathToCluster(fat32Volume* vol, const char* path, uint32_t* outCluster) {
    if (pathIsRoot(path)) {
        *outCluster = vol->rootCluster;
        return true;
    }

    char normalized[PATH_MAX];
    pathNormalize(path, normalized);

    uint32_t currentCluster = vol->rootCluster;

    char temp[PATH_MAX];
    strcpy(temp, normalized);

    char* token = strtok(temp, "/");
    while (token) {
        fat32DirEntry entry;
        if (!fat32FindDirEntry(vol, currentCluster, token, &entry)) {
            return false;
        }

        uint32_t nextCluster = ((uint32_t)entry.firstClusterHigh << 16) | entry.firstClusterLow;
        if (nextCluster < 2) {
            return false;
        }

        currentCluster = nextCluster;
        token = strtok(NULL, "/");
    }

    *outCluster = currentCluster;
    return true;
}

// 写 FAT 表项
static bool fat32WriteFatEntry(fat32Volume* vol, uint32_t cluster, uint32_t value) {
    uint32_t fatOffset = cluster * 4;
    uint32_t fatSector = vol->partitionOffset + vol->reservedSectorCount + 
                         (fatOffset / vol->bytesPerSector);
    uint32_t entryOffset = fatOffset % vol->bytesPerSector;

    ataReadSector(fatSector, sectorBuf);
    *(uint32_t*)(sectorBuf + entryOffset) = 
        (*(uint32_t*)(sectorBuf + entryOffset) & 0xF0000000) | (value & 0x0FFFFFFF);
    
    return ataWriteSector(fatSector, sectorBuf) == 0;
}

// 找空闲簇
static uint32_t fat32FindFreeCluster(fat32Volume* vol) {
    uint32_t totalClusters = vol->sectorsPerFat * vol->bytesPerSector / 4;
    for (uint32_t cluster = 2; cluster < totalClusters; cluster++) {
        uint32_t entry = readFatEntry(vol, cluster);
        if (entry == 0) {
            return cluster;
        }
    }
    return 0;
}

// 把短文件名转成 8+3 格式
static void fat32NameTo83(const char* input, char* output) {
    memset(output, ' ', 11);

    int i = 0, j = 0;
    while (input[i] && input[i] != '.' && j < 8) {
        output[j++] = input[i++];
    }

    // 跳过点
    if (input[i] == '.') i++;

    j = 8;
    while (input[i] && j < 11) {
        output[j++] = input[i++];
    }
}

// 在当前目录创建新目录项
static bool fat32CreateDirEntry(fat32Volume* vol, uint32_t dirCluster,
                                 const char* name, bool isDirectory,
                                 uint32_t newCluster) {
    uint32_t cluster = dirCluster;

    while (cluster < FAT32_CLUSTER_END) {
        uint32_t sector = clusterToSector(vol, cluster);
        for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
            ataReadSector(sector + s, sectorBuf);
            fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;

            for (int i = 0; i < 16; i++) {
                fat32DirEntry* e = &entries[i];

                // 空闲或已删除
                if (e->name[0] == 0x00 || e->name[0] == 0xE5) {
                    memset(e, 0, sizeof(fat32DirEntry));

                    char shortName[11];
                    fat32NameTo83(name, shortName);
                    memcpy(e->name, shortName, 11);

                    e->attributes = isDirectory ? FAT32_ATTR_DIRECTORY : FAT32_ATTR_ARCHIVE;
                    e->firstClusterHigh = (uint16_t)(newCluster >> 16);
                    e->firstClusterLow = (uint16_t)(newCluster & 0xFFFF);
                    e->fileSize = 0;

                    // 写回
                    ataWriteSector(sector + s, sectorBuf);
                    return true;
                }
            }
        }
        cluster = readFatEntry(vol, cluster);
    }
    return false;
}

bool fat32CreateEntry(fat32Volume* vol, const char* path, bool isDirectory) {
    if (!vol->valid) return false;

    // 分离目录和文件名
    char dir[PATH_MAX];
    char name[NAME_MAX + 1];
    pathSplit(path, dir, name);

    // 进入目录
    uint32_t dirCluster;
    if (!fat32PathToCluster(vol, dir, &dirCluster)) {
        return false;
    }

    // 检查是否已存在
    fat32DirEntry existing;
    if (fat32FindDirEntry(vol, dirCluster, name, &existing)) {
        return false;  // 已存在
    }

    // 找空闲簇
    uint32_t newCluster = fat32FindFreeCluster(vol);
    if (newCluster < 2) {
        return false;  // 磁盘满
    }

    // 分配簇：FAT 表标记为 END
    if (!fat32WriteFatEntry(vol, newCluster, 0x0FFFFFFF)) {
        return false;
    }

    // 创建目录项
    if (!fat32CreateDirEntry(vol, dirCluster, name, isDirectory, newCluster)) {
        return false;
    }

    // 如果是目录，初始化 . 和 ..
    if (isDirectory) {
        uint32_t sector = clusterToSector(vol, newCluster);
        ataReadSector(sector, sectorBuf);
        memset(sectorBuf, 0, 512);

        fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;
        memset(entries[0].name, ' ', 11);
        entries[0].name[0] = '.';
        entries[0].attributes = FAT32_ATTR_DIRECTORY;
        entries[0].firstClusterHigh = (uint16_t)(newCluster >> 16);
        entries[0].firstClusterLow = (uint16_t)(newCluster & 0xFFFF);

        memset(entries[1].name, ' ', 11);
        entries[1].name[0] = '.';
        entries[1].name[1] = '.';
        entries[1].attributes = FAT32_ATTR_DIRECTORY;
        entries[1].firstClusterHigh = (uint16_t)(dirCluster >> 16);
        entries[1].firstClusterLow = (uint16_t)(dirCluster & 0xFFFF);

        ataWriteSector(sector, sectorBuf);
    }

    return true;
}

bool fat32ListDir(fat32Volume* vol, const char* path, char* buf, int bufSize) {
    if (!vol->valid) return false;

    uint32_t cluster;
    if (!fat32PathToCluster(vol, path, &cluster)) {
        return false;
    }

    int offset = 0;
    char lfnBuffer[NAME_MAX + 1] = {0};
    int lfnLen = 0;
    char lfnSegments[3][14] = {0};

    while (cluster < FAT32_CLUSTER_END) {
        uint32_t sector = clusterToSector(vol, cluster);
        for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
            ataReadSector(sector + s, sectorBuf);
            fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;
            for (int i = 0; i < 16; i++) {
                fat32DirEntry* e = &entries[i];
                
                if (e->name[0] == 0x00) {
                    buf[offset] = '\0';
                    return true;
                }
                if (e->name[0] == 0xE5) {
                    lfnLen = 0;
                    memset(lfnBuffer, 0, sizeof(lfnBuffer));
                    memset(lfnSegments, 0, sizeof(lfnSegments));
                    continue;
                }

                if (e->attributes == 0x0F) {
                    fat32LfnEntry* lfn = (fat32LfnEntry*)e;
                    int order = lfn->order & 0x3F;
                    if (order >= 1 && order <= 3) {
                        parseLfn(lfn, lfnSegments[order - 1]);
                    }

                    // 最后一段的标记是 order == 1，不一定带 0x40
                    if (order == 1) {
                        lfnLen = 0;
                        memset(lfnBuffer, 0, sizeof(lfnBuffer));

                        // 先拼 seg0（后段），再拼 seg1（前段）
                        if (lfnSegments[0][0] != '\0') {
                            strcat(lfnBuffer, lfnSegments[0]);
                        }
                        if (lfnSegments[1][0] != '\0') {
                            strcat(lfnBuffer, lfnSegments[1]);
                        }
                        lfnLen = strlen(lfnBuffer);
                    }
                    continue;
                }

                // 短文件名项
                char name[NAME_MAX + 1];
                if (lfnLen > 0) {
                    strcpy(name, lfnBuffer);
                } else {
                    int n = 0;
                    for (int j = 0; j < 8 && e->name[j] != ' '; j++)
                        name[n++] = e->name[j];
                    if (e->name[8] != ' ') {
                        name[n++] = '.';
                        for (int j = 8; j < 11 && e->name[j] != ' '; j++)
                            name[n++] = e->name[j];
                    }
                    name[n] = '\0';
                }

                lfnLen = 0;
                memset(lfnBuffer, 0, sizeof(lfnBuffer));
                memset(lfnSegments, 0, sizeof(lfnSegments));

                // 跳过 . 和 ..
                if (name[0] == '.') {
                    lfnLen = 0;
                    continue;
                }

                const char* tag = (e->attributes & FAT32_ATTR_DIRECTORY) ? "<D> " : "<F> ";
                int tagLen = 4;
                int nameLen = strlen(name);

                if (offset + tagLen + nameLen + 2 < bufSize) {
                    for (int j = 0; j < tagLen; j++)
                        buf[offset++] = tag[j];
                    for (int j = 0; j < nameLen; j++)
                        buf[offset++] = name[j];
                    buf[offset++] = '\n';
                }

                lfnLen = 0;
            }
        }
        cluster = readFatEntry(vol, cluster);
    }
    buf[offset] = '\0';
    return true;
}

bool fat32FindDirEntry(fat32Volume* vol, uint32_t dirCluster,
                       const char* name, fat32DirEntry* result) {
    uint32_t cluster = dirCluster;
    char lfnBuffer[NAME_MAX + 1];
    int lfnLen = 0;
    char lfnSegments[3][14] = {0};
    memset(lfnBuffer, 0, sizeof(lfnBuffer));
    memset(lfnSegments, 0, sizeof(lfnSegments));

    while (cluster < FAT32_CLUSTER_END) {
        uint32_t sector = clusterToSector(vol, cluster);
        for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
            ataReadSector(sector + s, sectorBuf);
            fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;
            for (int i = 0; i < 16; i++) {
                fat32DirEntry* e = &entries[i];

                if (e->name[0] == 0x00) return false;
                if (e->name[0] == 0xE5) {
                    lfnLen = 0;
                    memset(lfnBuffer, 0, sizeof(lfnBuffer));
                    memset(lfnSegments, 0, sizeof(lfnSegments));
                    continue;
                }

                if (e->attributes == 0x0F) {
                    fat32LfnEntry* lfn = (fat32LfnEntry*)e;
                    int order = lfn->order & 0x3F;

                    if (order >= 1 && order <= 3) {
                        parseLfn(lfn, lfnSegments[order - 1]);
                    }

                    if (order == 1) {
                        lfnLen = 0;
                        memset(lfnBuffer, 0, sizeof(lfnBuffer));

                        if (lfnSegments[0][0] != '\0') {
                            strcat(lfnBuffer, lfnSegments[0]);
                        }
                        if (lfnSegments[1][0] != '\0') {
                            strcat(lfnBuffer, lfnSegments[1]);
                        }
                        lfnLen = strlen(lfnBuffer);
                    }
                    continue;
                }

                char entryName[NAME_MAX + 1];
                if (lfnLen > 0) {
                    strcpy(entryName, lfnBuffer);
                } else {
                    int n = 0;
                    for (int j = 0; j < 8 && e->name[j] != ' '; j++)
                        entryName[n++] = e->name[j];
                    if (e->name[8] != ' ') {
                        entryName[n++] = '.';
                        for (int j = 8; j < 11 && e->name[j] != ' '; j++)
                            entryName[n++] = e->name[j];
                    }
                    entryName[n] = '\0';
                }

                if (entryName[0] == '.' && (entryName[1] == '\0' || 
                    (entryName[1] == '.' && entryName[2] == '\0'))) {
                    lfnLen = 0;
                    memset(lfnBuffer, 0, sizeof(lfnBuffer));
                    memset(lfnSegments, 0, sizeof(lfnSegments));
                    continue;
                }

                if (strcasecmp(entryName, name) == 0) {
                    *result = *e;
                    return true;
                }

                lfnLen = 0;
                memset(lfnBuffer, 0, sizeof(lfnBuffer));
                memset(lfnSegments, 0, sizeof(lfnSegments));
            }
        }
        cluster = readFatEntry(vol, cluster);
    }
    return false;
}

// 释放簇链
static void fat32FreeClusterChain(fat32Volume* vol, uint32_t cluster) {
    while (cluster >= 2 && cluster < FAT32_CLUSTER_END) {
        uint32_t next = readFatEntry(vol, cluster);
        fat32WriteFatEntry(vol, cluster, 0);
        cluster = next;
    }
}

// 递归删除目录内容
static bool fat32RemoveRecursive(fat32Volume* vol, uint32_t cluster, bool isDirectory) {
    if (isDirectory) {
        // 遍历目录内容
        uint32_t currentCluster = cluster;
        while (currentCluster < FAT32_CLUSTER_END) {
            uint32_t sector = clusterToSector(vol, currentCluster);
            for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
                ataReadSector(sector + s, sectorBuf);
                fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;
                for (int i = 0; i < 16; i++) {
                    fat32DirEntry* e = &entries[i];
                    if (e->name[0] == 0x00) break;
                    if (e->name[0] == 0xE5) continue;
                    if (e->name[0] == '.') continue;  // 跳过 . 和 ..
                    if (e->attributes == 0x0F) continue;

                    uint32_t subCluster = ((uint32_t)e->firstClusterHigh << 16) | e->firstClusterLow;
                    bool subIsDir = (e->attributes & FAT32_ATTR_DIRECTORY) != 0;

                    fat32RemoveRecursive(vol, subCluster, subIsDir);
                }
            }
            currentCluster = readFatEntry(vol, currentCluster);
        }
    }

    // 释放簇链
    fat32FreeClusterChain(vol, cluster);
    return true;
}

// 标记目录项为已删除
static bool fat32MarkEntryDeleted(fat32Volume* vol, uint32_t dirCluster, const char* name) {
    uint32_t cluster = dirCluster;
    while (cluster < FAT32_CLUSTER_END) {
        uint32_t sector = clusterToSector(vol, cluster);
        for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
            ataReadSector(sector + s, sectorBuf);
            fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;
            for (int i = 0; i < 16; i++) {
                fat32DirEntry* e = &entries[i];
                if (e->name[0] == 0x00) return false;
                if (e->name[0] == 0xE5) continue;
                if (e->attributes == 0x0F) continue;

                char entryName[NAME_MAX + 1];
                int n = 0;
                for (int j = 0; j < 8 && e->name[j] != ' '; j++)
                    entryName[n++] = e->name[j];
                if (e->name[8] != ' ') {
                    entryName[n++] = '.';
                    for (int j = 8; j < 11 && e->name[j] != ' '; j++)
                        entryName[n++] = e->name[j];
                }
                entryName[n] = '\0';

                if (strcasecmp(entryName, name) == 0) {
                    e->name[0] = 0xE5;
                    ataWriteSector(sector + s, sectorBuf);
                    return true;
                }
            }
        }
        cluster = readFatEntry(vol, cluster);
    }
    return false;
}

bool fat32Remove(fat32Volume* vol, const char* path) {
    if (!vol->valid) return false;

    char dir[PATH_MAX];
    char name[NAME_MAX + 1];
    pathSplit(path, dir, name);

    uint32_t dirCluster;
    if (!fat32PathToCluster(vol, dir, &dirCluster)) {
        return false;
    }

    fat32DirEntry entry;
    if (!fat32FindDirEntry(vol, dirCluster, name, &entry)) {
        return false;
    }

    uint32_t cluster = ((uint32_t)entry.firstClusterHigh << 16) | entry.firstClusterLow;
    bool isDirectory = (entry.attributes & FAT32_ATTR_DIRECTORY) != 0;

    // 递归删除内容
    if (!fat32RemoveRecursive(vol, cluster, isDirectory)) {
        return false;
    }

    // 标记目录项已删除
    return fat32MarkEntryDeleted(vol, dirCluster, name);
}

bool fat32Rename(fat32Volume* vol, const char* oldPath, const char* newName) {
    if (!vol->valid) return false;

    char dir[PATH_MAX];
    char oldName[NAME_MAX + 1];
    pathSplit(oldPath, dir, oldName);

    uint32_t dirCluster;
    if (!fat32PathToCluster(vol, dir, &dirCluster)) {
        return false;
    }

    // 解析新名字为 8+3 格式
    char shortName[11];
    memset(shortName, ' ', 11);

    int i = 0, j = 0;
    while (newName[i] && newName[i] != '.' && j < 8) {
        if (newName[i] >= 'a' && newName[i] <= 'z')
            shortName[j++] = newName[i] - 32;
        else
            shortName[j++] = newName[i];
        i++;
    }

    if (newName[i] == '.') i++;

    j = 8;
    while (newName[i] && j < 11) {
        if (newName[i] >= 'a' && newName[i] <= 'z')
            shortName[j++] = newName[i] - 32;
        else
            shortName[j++] = newName[i];
        i++;
    }

    // 找到旧目录项
    uint32_t cluster = dirCluster;
    while (cluster < FAT32_CLUSTER_END) {
        uint32_t sector = clusterToSector(vol, cluster);
        for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
            ataReadSector(sector + s, sectorBuf);
            fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;
            for (int i = 0; i < 16; i++) {
                fat32DirEntry* e = &entries[i];
                if (e->name[0] == 0x00) return false;
                if (e->name[0] == 0xE5) continue;
                if (e->attributes == 0x0F) continue;

                char entryName[NAME_MAX + 1];
                int n = 0;
                for (int j = 0; j < 8 && e->name[j] != ' '; j++)
                    entryName[n++] = e->name[j];
                if (e->name[8] != ' ') {
                    entryName[n++] = '.';
                    for (int j = 8; j < 11 && e->name[j] != ' '; j++)
                        entryName[n++] = e->name[j];
                }
                entryName[n] = '\0';

                if (strcasecmp(entryName, oldName) == 0) {
                    memcpy(e->name, shortName, 11);
                    ataWriteSector(sector + s, sectorBuf);
                    return true;
                }
            }
        }
        cluster = readFatEntry(vol, cluster);
    }
    return false;
}

// 更新目录项的文件大小
static bool fat32UpdateFileSize(fat32Volume* vol, uint32_t dirCluster,
                                 const char* name, uint32_t size) {
    uint32_t cluster = dirCluster;
    while (cluster < FAT32_CLUSTER_END) {
        uint32_t sector = clusterToSector(vol, cluster);
        for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
            ataReadSector(sector + s, sectorBuf);
            fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;
            for (int i = 0; i < 16; i++) {
                fat32DirEntry* e = &entries[i];
                if (e->name[0] == 0x00) return false;
                if (e->name[0] == 0xE5) continue;
                if (e->attributes == 0x0F) continue;

                // 简单比较短文件名
                char entryName[13];
                int n = 0;
                for (int j = 0; j < 8 && e->name[j] != ' '; j++)
                    entryName[n++] = e->name[j];
                if (e->name[8] != ' ') {
                    entryName[n++] = '.';
                    for (int j = 8; j < 11 && e->name[j] != ' '; j++)
                        entryName[n++] = e->name[j];
                }
                entryName[n] = '\0';

                if (strcasecmp(entryName, name) == 0) {
                    e->fileSize = size;
                    ataWriteSector(sector + s, sectorBuf);
                    return true;
                }
            }
        }
        cluster = readFatEntry(vol, cluster);
    }
    return false;
}

bool fat32WriteFile(fat32Volume* vol, const char* path, 
                     const char* content, bool append) {
    if (!vol->valid) return false;

    // 分离路径
    char dir[PATH_MAX];
    char name[NAME_MAX + 1];
    pathSplit(path, dir, name);

    uint32_t dirCluster;
    if (!fat32PathToCluster(vol, dir, &dirCluster)) {
        return false;
    }

    // 找文件
    fat32DirEntry entry;
    if (!fat32FindDirEntry(vol, dirCluster, name, &entry)) {
        return false;
    }

    uint32_t oldCluster = ((uint32_t)entry.firstClusterHigh << 16) | entry.firstClusterLow;
    uint32_t oldSize = entry.fileSize;

    // 释放旧簇链（覆盖模式）
    if (!append && oldCluster >= 2) {
        fat32FreeClusterChain(vol, oldCluster);
        oldCluster = 0;
        oldSize = 0;
    }

    // 计算新内容
    uint32_t contentLen = strlen(content);
    uint32_t totalSize;

    if (append && oldCluster >= 2) {
        totalSize = oldSize + contentLen;
    } else {
        totalSize = contentLen;
    }

    if (totalSize == 0) {
        // 空文件
        fat32UpdateFileSize(vol, dirCluster, name, 0);
        return true;
    }

    // 分配簇
    uint32_t clusterSize = vol->sectorsPerCluster * vol->bytesPerSector;
    uint32_t neededClusters = (totalSize + clusterSize - 1) / clusterSize;

    uint32_t newCluster = 0;
    uint32_t prevCluster = 0;

    if (append && oldCluster >= 2) {
        // 追加模式：找到最后簇
        newCluster = oldCluster;
        while (readFatEntry(vol, newCluster) < FAT32_CLUSTER_END) {
            prevCluster = newCluster;
            newCluster = readFatEntry(vol, newCluster);
        }
        prevCluster = newCluster;  // 最后簇
        newCluster = fat32FindFreeCluster(vol);
        if (newCluster >= 2 && prevCluster >= 2) {
            fat32WriteFatEntry(vol, prevCluster, newCluster);
            fat32WriteFatEntry(vol, newCluster, 0x0FFFFFFF);
        }
        // 简化：不处理跨簇追加，只支持单簇追加
    } else {
        // 覆盖模式：分配新链
        uint32_t firstCluster = 0;
        uint32_t lastCluster = 0;

        for (uint32_t i = 0; i < neededClusters; i++) {
            uint32_t cluster = fat32FindFreeCluster(vol);
            if (cluster < 2) return false;

            if (firstCluster == 0) {
                firstCluster = cluster;
            } else {
                fat32WriteFatEntry(vol, lastCluster, cluster);
            }
            lastCluster = cluster;
        }

        fat32WriteFatEntry(vol, lastCluster, 0x0FFFFFFF);

        // 写内容
        uint32_t offset = 0;
        uint32_t cluster = firstCluster;
        uint8_t writeBuf[512];

        while (offset < contentLen && cluster < FAT32_CLUSTER_END) {
            uint32_t sector = clusterToSector(vol, cluster);
            memset(writeBuf, 0, 512);

            uint32_t toCopy = contentLen - offset;
            if (toCopy > clusterSize) toCopy = clusterSize;
            if (toCopy > 512) toCopy = 512;

            memcpy(writeBuf, content + offset, toCopy);
            ataWriteSector(sector, writeBuf);

            offset += toCopy;
            cluster = readFatEntry(vol, cluster);
        }

        // 更新目录项
        fat32UpdateFileSize(vol, dirCluster, name, contentLen);

        // 更新 firstCluster
        // 简化：直接写回目录项
        uint32_t updateCluster = dirCluster;
        while (cluster < FAT32_CLUSTER_END) {
            uint32_t sector = clusterToSector(vol, updateCluster);
            for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
                ataReadSector(sector + s, sectorBuf);
                fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;
                for (int i = 0; i < 16; i++) {
                    fat32DirEntry* e = &entries[i];
                    if (strcasecmp((char*)e->name, name) == 0) {
                        e->firstClusterHigh = (uint16_t)(firstCluster >> 16);
                        e->firstClusterLow = (uint16_t)(firstCluster & 0xFFFF);
                        e->fileSize = contentLen;
                        ataWriteSector(sector + s, sectorBuf);
                        return true;
                    }
                }
            }
            updateCluster = readFatEntry(vol, updateCluster);
        }
    }

    return true;
}

static bool fat32WriteByte(fat32Volume* vol, FileHandle* file, char byte) {
    if (!file) return false;

    // 如果当前位置在文件末尾，追加
    if (file->position >= file->size) {
        // 简化：假设文件在单簇内
        uint32_t cluster = file->cluster;
        uint32_t sector = clusterToSector(vol, cluster);
        uint32_t offset = file->position;

        ataReadSector(sector, sectorBuf);
        sectorBuf[offset] = byte;
        ataWriteSector(sector, sectorBuf);

        file->size++;
        file->position++;
        return true;
    }

    // 覆盖
    uint32_t cluster = file->cluster;
    uint32_t clusterSize = vol->sectorsPerCluster * vol->bytesPerSector;
    uint32_t skipClusters = file->position / clusterSize;
    for (uint32_t i = 0; i < skipClusters; i++) {
        cluster = readFatEntry(vol, cluster);
        if (cluster >= FAT32_CLUSTER_END) return false;
    }

    uint32_t offsetInCluster = file->position % clusterSize;
    uint32_t sector = clusterToSector(vol, cluster) + offsetInCluster / vol->bytesPerSector;
    uint32_t offset = offsetInCluster % vol->bytesPerSector;

    ataReadSector(sector, sectorBuf);
    sectorBuf[offset] = byte;
    ataWriteSector(sector, sectorBuf);

    file->position++;
    return true;
}

bool fat32CopyFile(fat32Volume* vol, const char* srcPath, const char* dstPath) {
    if (!vol->valid) return false;

    FileHandle src;
    if (!fsOpen(&src, srcPath)) {
        return false;
    }

    if (!fat32CreateEntry(vol, dstPath, false)) {
        fsClose(&src);
        return false;
    }

    FileHandle dst;
    if (!fsOpen(&dst, dstPath)) {
        fsClose(&src);
        return false;
    }

    char byte;
    uint32_t total = 0;
    while (fsRead(&src, &byte, 1) == 1) {
        if (!fat32WriteByte(vol, &dst, byte)) {
            fsClose(&src);
            fsClose(&dst);
            return false;
        }
        total++;
    }

    // 更新目标文件的目录项大小
    char dir[PATH_MAX];
    char name[NAME_MAX + 1];
    pathSplit(dstPath, dir, name);

    uint32_t dirCluster;
    if (fat32PathToCluster(vol, dir, &dirCluster)) {
        fat32UpdateFileSize(vol, dirCluster, name, total);
    }

    fsClose(&src);
    fsClose(&dst);
    return true;
}