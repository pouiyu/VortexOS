#include "fat32.h"
#include <ata.h>
#include <string.h>

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