#ifndef _KERNEL_FS_PATH_H
#define _KERNEL_FS_PATH_H

#include <stdbool.h>

// 把路径拆成目录部分和文件名部分
// "/dir1/dir2/file.txt" → dir="/dir1/dir2/", name="FILE.TXT"
void pathSplit(const char* fullPath, char* dir, char* name);

// 路径规范化：把所有字符转大写，'/' 转 '\'
void pathNormalize(const char* input, char* output);

// 检查是否是根目录
bool pathIsRoot(const char* path);

#endif