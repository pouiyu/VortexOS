// file.c
#include "file.h"
#include "fat32.h"
#include "path.h"
#include <disk.h>
#include <serial.h>
#include <string/string.h>

extern fat32Volume fsVolume;

static uint8_t sectorBuf[512];

/* ===== GRUB multiboot2 模块后备(只读内存文件) =====
 * 真机从 U盘 启动时内核没有 USB 存储驱动、SATA 光驱在 AHCI 模式下 ATAPI(PIO)
 * 也读不到，此时"光驱"整体不可用。GRUB 在引导期能从 U盘 ISO9660 读文件，
 * 由 grub.cfg 的 module2 行把 system 运行目录随内核一起送入内存，内核把模块
 * 当作只读文件挂在相同路径下，使字体/壁纸/图形程序/安装源在任何设备上一致可用。 */
#define FS_MAX_MODULES 16
typedef struct {
    uint32_t start;
    uint32_t end;
    char     path[96];
} FsModule;

static FsModule sMods[FS_MAX_MODULES];
static int      sModCount = 0;

void fsRegisterModules(uint32_t mb2InfoAddr) {
    sModCount = 0;
    if (!mb2InfoAddr) return;
    uint32_t total = *(const uint32_t*)(uintptr_t)mb2InfoAddr;
    uint32_t off   = 8;
    while (off + 8 <= total && sModCount < FS_MAX_MODULES) {
        const uint8_t* p = (const uint8_t*)(uintptr_t)(mb2InfoAddr + off);
        uint32_t type = *(const uint32_t*)(p);
        uint32_t size = *(const uint32_t*)(p + 4);
        if (size < 8 || off + size > total) break;
        if (type == 3 && size >= 16) {           /* module tag */
            FsModule* m = &sMods[sModCount];
            m->start = *(const uint32_t*)(p + 8);
            m->end   = *(const uint32_t*)(p + 12);
            const char* cmd = (const char*)(p + 16);
            int i = 0;
            for (; i < 95 && cmd[i]; i++) m->path[i] = cmd[i];
            m->path[i] = '\0';
            if (m->end > m->start && m->path[0]) sModCount++;
        }
        off += size;
        if (size & 7) off += 8 - (size & 7);     /* tag 按 8 字节对齐 */
    }
    serialPutStr("[MOD] registered ");
    /* 简单十进制输出 */
    { char t[12]; int i = 0, v = sModCount;
      if (!v) serialPutStr("0");
      while (v) { t[i++] = (char)('0' + v % 10); v /= 10; }
      while (i--) { char c[2] = { t[i], 0 }; serialPutStr(c); } }
    serialPutStr(" modules\n");
    for (int i = 0; i < sModCount; i++) {
        serialPutStr("[MOD] "); serialPutStr(sMods[i].path); serialPutStr("\n");
    }
}

const uint8_t* fsFindModule(const char* path, uint32_t* outSize) {
    if (!path) return 0;
    for (int i = 0; i < sModCount; i++) {
        if (strcasecmp(sMods[i].path, path) == 0) {
            if (outSize) *outSize = sMods[i].end - sMods[i].start;
            return (const uint8_t*)(uintptr_t)sMods[i].start;
        }
    }
    return 0;
}

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

/* 模块后备打开：FAT32 卷无效或卷上无此文件时调用。 */
static bool fsOpenModule(FileHandle* file, const char* path) {
    char normalized[PATH_MAX];
    pathNormalize(path, normalized);
    uint32_t sz = 0;
    const uint8_t* data = fsFindModule(normalized, &sz);
    if (!data) return false;
    file->cluster = 0;
    file->size = sz;
    file->position = 0;
    file->isDirectory = false;
    file->modData = data;
    return true;
}

bool fsOpen(FileHandle* file, const char* path) {
    if (!file) return false;
    file->modData = 0;

    if (!fsVolume.valid) return fsOpenModule(file, path);

    uint32_t cluster;
    if (!followPath(&fsVolume, path, &cluster)) {
        return fsOpenModule(file, path);
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
            return fsOpenModule(file, path);
        }
    }

    fat32DirEntry entry;
    if (!fat32FindDirEntry(&fsVolume, dirCluster, name, &entry)) {
        return fsOpenModule(file, path);
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

    /* 模块后备：数据整段在内存，直接拷贝 */
    if (file->modData) {
        memcpy(buf, file->modData + file->position, count);
        file->position += count;
        return (int)count;
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
            /* 经磁盘抽象层按卷所属驱动器读取：AHCI(SATA) 真机硬盘走 AHCI 通道，
             * 直写 legacy ATA 端口会在无 IDE 控制器的机器上读错/挂死。 */
            driveReadSectors(fsVolume.drive, sector + b, 1, sectorBuf);

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