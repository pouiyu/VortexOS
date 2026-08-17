#include "path.h"
#include <string.h>

void pathSplit(const char* fullPath, char* dir, char* name) {
    int len = strlen(fullPath);
    int slashPos = -1;

    for (int i = len - 1; i >= 0; i--) {
        if (fullPath[i] == '/') {
            slashPos = i;
            break;
        }
    }

    if (slashPos < 0) {
        dir[0] = '\0';
        strcpy(name, fullPath);
    } else if (slashPos == 0) {
        dir[0] = '/';
        dir[1] = '\0';
        strcpy(name, fullPath + 1);
    } else {
        int i;
        for (i = 0; i < slashPos; i++)
            dir[i] = fullPath[i];
        dir[i] = '\0';
        strcpy(name, fullPath + slashPos + 1);
    }
}

void pathNormalize(const char* input, char* output) {
    int i = 0;
    while (input[i]) {
        output[i] = input[i];
        i++;
    }
    output[i] = '\0';
}

bool pathIsRoot(const char* path) {
    return path[0] == '\0' || (path[0] == '/' && path[1] == '\0');
}