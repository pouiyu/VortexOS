#include "fat32.h"
#include <ata.h>
#include <string/string.h>
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
    if (ataReadSector(0, sectorBuf) != 0) {
        vol->valid = false;
        return false;
    }

    // 检查 MBR 签名（0x55AA）
    uint32_t partitionStart = 0;
    if (sectorBuf[510] == 0x55 && sectorBuf[511] == 0xAA) {
        // 有 0x55AA：可能是真 MBR，也可能是无分区(superfloppy)的 VBR。
        // 仅当第一个分区表条目有效(类型非 0 且起始 LBA 合法)时才按分区解析，
        // 否则视为 superfloppy —— FAT32 直接从块 0 开始。
        uint8_t  partType = sectorBuf[0x1BE + 4];
        uint32_t partStart = *(uint32_t*)(sectorBuf + 0x1BE + 8);
        partitionStart = (partType != 0 && partStart >= 1) ? partStart : 0;
    }
    vol->partitionOffset = partitionStart;

    if (ataReadSector(partitionStart, sectorBuf) != 0) {
        vol->valid = false;
        return false;
    }
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
    vol->nextFreeCluster = 2;                     /* 空闲簇扫描续查点 */
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

    /* 手动路径解析，不使用 strtok(避免与 fat32MkDirs 的 strtok 冲突) */
    const char* p = normalized;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char seg[NAME_MAX + 1];
        int si = 0;
        while (*p && *p != '/' && si < NAME_MAX) {
            seg[si++] = *p++;
        }
        seg[si] = '\0';
        if (si == 0) break;

        fat32DirEntry entry;
        if (!fat32FindDirEntry(vol, currentCluster, seg, &entry)) {
            return false;
        }

        uint32_t nextCluster = ((uint32_t)entry.firstClusterHigh << 16) | entry.firstClusterLow;
        if (nextCluster < 2) {
            return false;
        }

        currentCluster = nextCluster;
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

// 找空闲簇。
// 注意：找到后立即把该簇标记为已占用(暂置 END)，否则同一循环里再次调用
// 会返回同一簇，导致多簇文件/目录被错误地折叠成单个簇而数据截断。
// 用 vol->nextFreeCluster 记录上次找到的位置，顺序分配时从续查点继续，
// 避免每个簇都从簇 2 重新扫描(FAT 表 O(n^2) 慢)。
static uint32_t fat32FindFreeCluster(fat32Volume* vol) {
    uint32_t totalClusters = vol->sectorsPerFat * vol->bytesPerSector / 4;
    if (totalClusters <= 2) return 0;

    uint32_t span = totalClusters - 2;
    uint32_t start = vol->nextFreeCluster;
    if (start < 2 || start >= totalClusters) start = 2;

    for (uint32_t i = 0; i < span; i++) {
        uint32_t cluster = 2 + (start - 2 + i) % span;
        uint32_t entry = readFatEntry(vol, cluster);
        if (entry == 0) {
            fat32WriteFatEntry(vol, cluster, 0x0FFFFFFF);
            vol->nextFreeCluster = cluster + 1;   /* 下次从下一个簇续查 */
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
// 计算短文件名校验和
static uint8_t fat32ChecksumShortName(const char* shortName) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        // 校验和算法：sum = (sum >> 1) | ((sum & 1) << 7) + byte
        sum = (sum >> 1) | ((sum & 1) << 7);
        sum += (uint8_t)shortName[i];
    }
    return sum;
}

static bool fat32CreateDirEntry(fat32Volume* vol, uint32_t dirCluster,
                                 const char* name, bool isDirectory,
                                 uint32_t newCluster) {
    uint32_t cluster = dirCluster;
    int nameLen = strlen(name);

    // 如果文件名需要 LFN（超过 8+3 或大小写混合），预留 LFN 条目空间
    int lfnEntries = 0;
    if (nameLen > 11) {
        lfnEntries = (nameLen + 12) / 13;  // 每个 LFN 条目存 13 个字符
    }

    while (cluster < FAT32_CLUSTER_END) {
        uint32_t sector = clusterToSector(vol, cluster);
        for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
            ataReadSector(sector + s, sectorBuf);
            fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;

            // 查找足够的连续空闲条目
            int start = -1;
            int needSlots = lfnEntries + 1;  // LFN条目 + 短文件名条目
            int freeCount = 0;

            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                    freeCount++;
                    if (start == -1) start = i;
                    if (freeCount == needSlots) break;
                } else {
                    start = -1;
                    freeCount = 0;
                }
            }

            if (freeCount < needSlots) {
                // 这扇区不够，继续找下一扇区
                continue;
            }

            // 找到了足够位置：start 开始共 needSlots 个槽位
            int currentSlot = start;

            // 如果需要，写入 LFN 条目（按倒序排列，第一段编号 1）
            if (lfnEntries > 0) {
                char shortName[11];
                fat32NameTo83(name, shortName);
                uint8_t checksum = fat32ChecksumShortName(shortName);

                // 倒序写入：编号最大的 LFN 条目写在最前面(离 8.3 条目最远)，编号 1 带 0x40 标记
                for (int entry = lfnEntries; entry >= 1; entry--) {
                    fat32LfnEntry* lfn = (fat32LfnEntry*)&entries[currentSlot];
                    memset(lfn, 0, sizeof(fat32LfnEntry));

                    uint8_t order = (uint8_t)entry;
                    if (entry == 1) {
                        order |= 0x40;  // 最后一段标记(最靠近 8.3 条目)
                    }
                    lfn->order = order;
                    lfn->attr = 0x0F;
                    lfn->type = 0;
                    lfn->checksum = checksum;
                    lfn->firstCluster = 0;

                    // 编号 entry 的条目存 name[(entry-1)*13 .. +12] 这 13 个字符
                    int base = (entry - 1) * 13;
                    int j;

                    /* name1: 字符 0..4 */
                    for (j = 0; j < 5; j++) {
                        if (base + j < nameLen) lfn->name1[j] = (uint16_t)(unsigned char)name[base + j];
                        else lfn->name1[j] = 0xFFFF;
                    }
                    /* name2: 字符 5..10 */
                    for (j = 0; j < 6; j++) {
                        if (base + 5 + j < nameLen) lfn->name2[j] = (uint16_t)(unsigned char)name[base + 5 + j];
                        else lfn->name2[j] = 0xFFFF;
                    }
                    /* name3: 字符 11..12 */
                    for (j = 0; j < 2; j++) {
                        if (base + 11 + j < nameLen) lfn->name3[j] = (uint16_t)(unsigned char)name[base + 11 + j];
                        else lfn->name3[j] = 0xFFFF;
                    }

                    currentSlot++;
                }
            }

            // 写入短文件名条目（在最后一个槽位）
            fat32DirEntry* e = &entries[currentSlot];
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
        while (updateCluster < FAT32_CLUSTER_END) {
            uint32_t sector = clusterToSector(vol, updateCluster);
            for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
                ataReadSector(sector + s, sectorBuf);
                fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;
                for (int i = 0; i < 16; i++) {
                    fat32DirEntry* e = &entries[i];
                    if (e->name[0] == 0x00) return false;
                    if (e->name[0] == 0xE5 || e->attributes == 0x0F) continue;

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

/* ===================== 安装系统辅助 ===================== */

/* 在指定 FAT 表内的某块上写入一个簇项(FAT1/FAT2 各自独立写) */
static bool fmtWriteFatCluster(uint32_t fatStart, uint32_t cluster, uint32_t value) {
    static uint8_t sec[512];
    uint32_t offset = cluster * 4;
    uint32_t sector = fatStart + offset / 512;
    uint32_t off    = offset % 512;

    if (ataReadSector(sector, sec) != 0) return false;
    *(uint32_t*)(sec + off) = (*(uint32_t*)(sec + off) & 0xF0000000) | (value & 0x0FFFFFFF);
    return ataWriteSector(sector, sec) == 0;
}

/* 格式化几何参数：固定 64MB 磁盘。
 * 采用 MBR 分区布局以便脱离光驱引导：
 *   扇区 0     = MBR(分区表 + 引导标志 + 0x55AA)
 *   扇区 1~2047= gap(供 GRUB core.img 使用；boot.img 由安装阶段写入扇区 0)
 *   扇区 2048 起= FAT32 主分区(partitionOffset = 2048) */
#define FMT_TOTAL_SECTORS 131072   /* 磁盘总扇区数(64MB) */
#define FMT_PART_OFFSET   2048     /* 分区起始扇区(保留 gap 给 GRUB) */
#define FMT_SPC           8
#define FMT_RESERVED      32
#define FMT_FATS          2
#define FMT_FATSECTORS    128

bool fat32Format(fat32Volume* vol) {
    static const uint8_t zero512[512] = {0};
    uint8_t sec[512];
    uint32_t partSectors = FMT_TOTAL_SECTORS - FMT_PART_OFFSET;      /* 分区内扇区数 */
    uint32_t dataStart   = FMT_PART_OFFSET + FMT_RESERVED + FMT_FATS * FMT_FATSECTORS;

    /* 1. 写 MBR：一个 FAT32 主分区自 LBA2048 起，带引导标志 */
    memset(sec, 0, 512);
    sec[510] = 0x55;
    sec[511] = 0xAA;
    {
        uint8_t* pe = &sec[446];
        pe[0] = 0x80;                                    /* 引导标志 */
        pe[1] = 0xFE; pe[2] = 0xFF; pe[3] = 0xFF;        /* CHS(忽略) */
        pe[4] = 0x0C;                                    /* FAT32 LBA */
        pe[5] = 0xFE; pe[6] = 0xFF; pe[7] = 0xFF;
        *(uint32_t*)(pe + 8)  = FMT_PART_OFFSET;         /* 起始 LBA */
        *(uint32_t*)(pe + 12) = partSectors;             /* 分区扇区数 */
    }
    if (ataWriteSector(0, sec) != 0) { vol->valid = false; return false; }

    /* 2. 清零分区内保留区、FAT、根簇区域 */
    for (uint32_t s = FMT_PART_OFFSET; s <= dataStart + FMT_SPC; s++) {
        if (ataWriteSector(s, zero512) != 0) { vol->valid = false; return false; }
    }

    /* 3. 写引导扇区(BPB) 到分区起始扇区 */
    memset(sec, 0, 512);
    fat32BootSector* bs = (fat32BootSector*)sec;
    bs->jmpBoot[0] = 0xEB; bs->jmpBoot[1] = 0x58; bs->jmpBoot[2] = 0x90;
    memcpy(bs->oemName, "VORTEX  ", 8);
    bs->bytesPerSector    = 512;
    bs->sectorsPerCluster = FMT_SPC;
    bs->reservedSectorCount = FMT_RESERVED;
    bs->numFats           = FMT_FATS;
    bs->rootEntryCount    = 0;
    bs->totalSectors16    = 0;
    bs->mediaType         = 0xF8;
    bs->sectorsPerFat16   = 0;
    bs->sectorsPerTrack   = 63;
    bs->numHeads          = 255;
    bs->hiddenSectors     = FMT_PART_OFFSET;        /* 分区隐藏扇区数(分区偏移) */
    bs->totalSectors32    = partSectors;            /* 分区内总扇区数 */
    bs->sectorsPerFat     = FMT_FATSECTORS;
    bs->extFlags          = 0;
    bs->fsVersion         = 0;
    bs->rootCluster       = 2;
    bs->fsInfo            = 1;
    bs->backupBootSector  = 6;
    bs->bootSignature     = 0x29;
    bs->volumeId          = 0x12345678;
    memcpy(bs->volumeLabel, "VORTEX OS  ", 11);
    memcpy(bs->fsType, "FAT32   ", 8);
    sec[510] = 0x55;
    sec[511] = 0xAA;
    if (ataWriteSector(FMT_PART_OFFSET, sec) != 0) { vol->valid = false; return false; }

    /* 3. 初始化 FAT1/FAT2 的表头与根簇项 */
    for (uint32_t f = 0; f < FMT_FATS; f++) {
        uint32_t fatStart = FMT_PART_OFFSET + FMT_RESERVED + f * FMT_FATSECTORS;
        if (!fmtWriteFatCluster(fatStart, 0, 0x0FFFFFF8)) { vol->valid = false; return false; }
        if (!fmtWriteFatCluster(fatStart, 1, 0x0FFFFFFF)) { vol->valid = false; return false; }
        if (!fmtWriteFatCluster(fatStart, 2, 0x0FFFFFFF)) { vol->valid = false; return false; }
    }

    /* 4. 填充卷结构 */
    vol->valid              = true;
    vol->bytesPerSector     = 512;
    vol->sectorsPerCluster  = FMT_SPC;
    vol->sectorsPerFat      = FMT_FATSECTORS;
    vol->rootCluster        = 2;
    vol->reservedSectorCount = FMT_RESERVED;
    vol->numFats            = FMT_FATS;
    vol->partitionOffset    = FMT_PART_OFFSET;
    vol->dataStartSector    = dataStart;
    vol->rootDirStartSector = dataStart;
    return true;
}

bool fat32MkDirs(fat32Volume* vol, const char* path) {
    if (!vol->valid) return false;
    if (pathIsRoot(path)) return true;

    char full[PATH_MAX];
    full[0] = '/';
    int fl = 1;

    /* 手动路径解析，不使用 strtok(避免与 fat32PathToCluster 的 strtok 冲突) */
    const char* p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        while (*p && *p != '/') {
            full[fl++] = *p++;
        }
        full[fl] = '\0';

        uint32_t c;
        if (!fat32PathToCluster(vol, full, &c)) {
            if (!fat32CreateEntry(vol, full, true)) return false;
            if (!fat32PathToCluster(vol, full, &c)) return false;
        }
        full[fl++] = '/';
        full[fl] = '\0';
    }
    return true;
}

bool fat32FileExists(fat32Volume* vol, const char* path) {
    uint32_t cluster;
    return fat32PathToCluster(vol, path, &cluster);
}

bool fat32WriteRawFile(fat32Volume* vol, const char* path, const void* data, uint32_t size) {
    if (!vol->valid) return false;

    char dir[PATH_MAX];
    char name[NAME_MAX + 1];
    pathSplit(path, dir, name);

    uint32_t dirCluster;
    if (!fat32PathToCluster(vol, dir, &dirCluster)) return false;

    fat32DirEntry entry;
    if (!fat32FindDirEntry(vol, dirCluster, name, &entry)) return false;

    uint32_t oldCluster = ((uint32_t)entry.firstClusterHigh << 16) | entry.firstClusterLow;
    if (oldCluster >= 2) fat32FreeClusterChain(vol, oldCluster);
    if (size == 0) { fat32UpdateFileSize(vol, dirCluster, name, 0); return true; }

    uint32_t clusterSize = vol->sectorsPerCluster * vol->bytesPerSector;
    uint32_t needed = (size + clusterSize - 1) / clusterSize;

    uint32_t first = 0, last = 0;
    for (uint32_t i = 0; i < needed; i++) {
        uint32_t c = fat32FindFreeCluster(vol);
        if (c < 2) return false;
        if (first == 0) first = c;
        else fat32WriteFatEntry(vol, last, c);
        last = c;
    }
    fat32WriteFatEntry(vol, last, 0x0FFFFFFF);

    uint32_t off = 0;
    uint32_t cluster = first;
    uint8_t wb[512];

    while (off < size && cluster < FAT32_CLUSTER_END) {
        uint32_t sector = clusterToSector(vol, cluster);
        for (uint8_t s = 0; s < vol->sectorsPerCluster && off < size; s++) {
            memset(wb, 0, 512);
            uint32_t toCopy = size - off;
            if (toCopy > 512) toCopy = 512;
            memcpy(wb, (const uint8_t*)data + off, toCopy);
            if (ataWriteSector(sector + s, wb) != 0) return false;
            off += toCopy;
        }
        cluster = readFatEntry(vol, cluster);
    }

    /* 更新目录项：起始簇与文件大小(需按长文件名匹配，与 fat32FindDirEntry 一致) */
    char lfnBuffer[NAME_MAX + 1];
    char lfnSegments[3][14];
    int lfnLen;

    uint32_t uc = dirCluster;
    while (uc < FAT32_CLUSTER_END) {
        uint32_t sector = clusterToSector(vol, uc);
        for (uint8_t s = 0; s < vol->sectorsPerCluster; s++) {
            if (ataReadSector(sector + s, sectorBuf) != 0) return false;
            fat32DirEntry* entries = (fat32DirEntry*)sectorBuf;
            memset(lfnBuffer, 0, sizeof(lfnBuffer));
            memset(lfnSegments, 0, sizeof(lfnSegments));
            lfnLen = 0;

            for (int i = 0; i < 16; i++) {
                fat32DirEntry* e = &entries[i];
                if (e->name[0] == 0x00) return false;
                if (e->name[0] == 0xE5) {
                    memset(lfnBuffer, 0, sizeof(lfnBuffer));
                    memset(lfnSegments, 0, sizeof(lfnSegments));
                    lfnLen = 0;
                    continue;
                }
                if (e->attributes == 0x0F) {
                    fat32LfnEntry* lfn = (fat32LfnEntry*)e;
                    int order = lfn->order & 0x3F;
                    if (order >= 1 && order <= 3) parseLfn(lfn, lfnSegments[order - 1]);
                    if (order == 1) {
                        lfnLen = 0;
                        memset(lfnBuffer, 0, sizeof(lfnBuffer));
                        if (lfnSegments[0][0] != '\0') strcat(lfnBuffer, lfnSegments[0]);
                        if (lfnSegments[1][0] != '\0') strcat(lfnBuffer, lfnSegments[1]);
                        if (lfnSegments[2][0] != '\0') strcat(lfnBuffer, lfnSegments[2]);
                        lfnLen = strlen(lfnBuffer);
                    }
                    continue;
                }

                char entryName[NAME_MAX + 1];
                if (lfnLen > 0) {
                    strcpy(entryName, lfnBuffer);
                } else {
                    int n = 0;
                    for (int j = 0; j < 8 && e->name[j] != ' '; j++) entryName[n++] = (char)e->name[j];
                    if (e->name[8] != ' ') {
                        entryName[n++] = '.';
                        for (int j = 8; j < 11 && e->name[j] != ' '; j++) entryName[n++] = (char)e->name[j];
                    }
                    entryName[n] = '\0';
                }

                if (strcasecmp(entryName, name) == 0) {
                    e->firstClusterHigh = (uint16_t)(first >> 16);
                    e->firstClusterLow  = (uint16_t)(first & 0xFFFF);
                    e->fileSize         = size;
                    if (ataWriteSector(sector + s, sectorBuf) != 0) return false;
                    return true;
                }

                memset(lfnBuffer, 0, sizeof(lfnBuffer));
                memset(lfnSegments, 0, sizeof(lfnSegments));
                lfnLen = 0;
            }
        }
        uc = readFatEntry(vol, uc);
    }
    return true;
}