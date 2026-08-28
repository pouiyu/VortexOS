#ifndef _KERNEL_FS_ISO9660_H
#define _KERNEL_FS_ISO9660_H

#include <stdint.h>
#include <stdbool.h>

/* 初始化：读取 PVD，定位根目录 */
bool iso9660Init(void);

/* 在光盘上按路径查找文件(大小写不敏感，忽略 ";n" 版本号)。
 * 路径形如 "FONT.BIN" 或 "/FONT.BIN" 或 "DIR/FONT.BIN"。
 * 找到返回 true，ext=数据起始逻辑块，len=字节数。 */
bool iso9660FindFile(const char* path, uint32_t* ext, uint32_t* len);

/* 目录遍历回调：name 为条目名(已去 ";n" 版本号，保留原始大小写)，
 * flags 第 1 位(0x02)置位表示子目录。返回 false 可中止遍历。 */
typedef bool (*iso9660Visitor)(const char* name, uint32_t ext, uint32_t len,
                               uint8_t flags, void* arg);

/* 遍历目录 path 下的所有条目(自动跳过 "." 与 "..")，
 * 每项回调 visit，path 必须是一个目录。读取成功返回 true。 */
bool iso9660ListDir(const char* path, iso9660Visitor visit, void* arg);

#endif