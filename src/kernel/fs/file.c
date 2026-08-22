// file.c
#include "file.h"
#include "fat32.h"
#include "path.h"
#include <ata.h>
#include <string/string.h>

extern fat32Volume fsVolume;

static uint8_t sectorBuf[512];

// 跟踪路径进入目录
static bool followPath(fat32Volume* vol, const char* path, uint32_t* outCluster) {
    if (pathIsRoot(path)) {
        *outCluster = vol->rootCluster;
        return true;
    }

    char normalized[PATH_MAX];
    pathNormalize(path, normalized);

    char dir[PATH_MAX];
    char name[NAME_MAX + 1];
    pathSplit(normalized, dir, name);

    // 如果有目录部分，先递归进入
    uint32_t currentCluster = vol->rootCluster;
    if (dir[0] != '\0') {
        if (!followPath(vol, dir, &currentCluster)) {
            return false;
        }
    }

    // 在当前目录找目标
    char entryName[NAME_MAX + 1];
    pathNormalize(name, entryName);

    fat32DirEntry entry;
    if (!fat32FindDirEntry(vol, currentCluster, entryName, &entry)) {
        return false;
    }

    uint32_t cluster = entry.firstClusterHigh;
    cluster = (cluster << 16) | entry.firstClusterLow;

    if (cluster < 2) {
        return false;
    }

    *outCluster = cluster;
    return true;
}

bool fsOpen(FileHandle* file, const char* path) {
    if (!fsVolume.valid || !file) return false;

    uint32_t cluster;
    if (!followPath(&fsVolume, path, &cluster)) {
        return false;
    }

    char normalized[PATH_MAX];
    pathNormalize(path, normalized);

    char dir[PATH_MAX];
    char name[NAME_MAX + 1];
    pathSplit(normalized, dir, name);
    pathNormalize(name, name);

    uint32_t dirCluster = fsVolume.rootCluster;
    if (dir[0] != '\0') {
        if (!followPath(&fsVolume, dir, &dirCluster)) {
            return false;
        }
    }

    fat32DirEntry entry;
    if (!fat32FindDirEntry(&fsVolume, dirCluster, name, &entry)) {  // 改这行
        return false;
    }

    file->cluster = ((uint32_t)entry.firstClusterHigh << 16) | entry.firstClusterLow;
    file->size = entry.fileSize;
    file->position = 0;
    file->isDirectory = (entry.attributes & FAT32_ATTR_DIRECTORY) != 0;

    return true;
}

int fsRead(FileHandle* file, void* buf, uint32_t count) {
    if (!file || !buf) return 0;

    // 不能超过文件大小
    if (file->position >= file->size) return 0;
    if (count > file->size - file->position) {
        count = file->size - file->position;
    }

    uint8_t* out = (uint8_t*)buf;
    uint32_t bytesRead = 0;

    // 跳到当前簇
    uint32_t cluster = file->cluster;
    uint32_t clusterSize = fsVolume.sectorsPerCluster * fsVolume.bytesPerSector;

    uint32_t skipClusters = file->position / clusterSize;
    for (uint32_t i = 0; i < skipClusters; i++) {
        cluster = readFatEntry(&fsVolume, cluster);
        if (cluster >= FAT32_CLUSTER_END) return 0;
    }

    uint32_t offsetInCluster = file->position % clusterSize;

    while (bytesRead < count && cluster < FAT32_CLUSTER_END) {
        uint32_t sector = clusterToSector(&fsVolume, cluster);
        uint32_t startBlock = offsetInCluster / fsVolume.bytesPerSector;
        uint32_t startByte = offsetInCluster % fsVolume.bytesPerSector;

        for (uint32_t b = startBlock; 
             b < fsVolume.sectorsPerCluster && bytesRead < count; b++) {
            ataReadSector(sector + b, sectorBuf);

            uint32_t toCopy = fsVolume.bytesPerSector - startByte;
            if (toCopy > count - bytesRead) {
                toCopy = count - bytesRead;
            }

            memcpy(out + bytesRead, sectorBuf + startByte, toCopy);
            bytesRead += toCopy;
            startByte = 0;
        }

        offsetInCluster = 0;
        cluster = readFatEntry(&fsVolume, cluster);
    }

    file->position += bytesRead;
    return bytesRead;
}

void fsClose(FileHandle* file) {
    if (file) {
        file->position = 0;
    }
}