#include <string/string.h>
#include <stddef.h>
#include <stdlib/stdlib.h>

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

size_t strnlen(const char* s, size_t maxlen) {
    size_t len = 0;
    while (len < maxlen && s[len]) {
        len++;
    }
    return len;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while (*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;
    size_t i;
    for (i = 0; i < n && src[i]; i++) {
        d[i] = src[i];
    }
    for (; i < n; i++) {
        d[i] = '\0';
    }
    return dest;
}

char* strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* new = (char*)malloc(len);
    if (new) {
        strcpy(new, s);
    }
    return new;
}

char* strcat(char* dest, const char* src) {
    char* d = dest + strlen(dest);
    while (*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

char* strncat(char* dest, const char* src, size_t n) {
    char* d = dest + strlen(dest);
    size_t i;
    for (i = 0; i < n && src[i]; i++) {
        d[i] = src[i];
    }
    d[i] = '\0';
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char*)s;
        }
        s++;
    }
    return (c == '\0') ? (char*)s : NULL;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    return (c == '\0') ? (char*)s : (char*)last;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) {
        return (char*)haystack;
    }
    
    for (const char* h = haystack; *h; h++) {
        const char* hTemp = h;
        const char* nTemp = needle;
        while (*hTemp && *nTemp && *hTemp == *nTemp) {
            hTemp++;
            nTemp++;
        }
        if (!*nTemp) {
            return (char*)h;
        }
    }
    return NULL;
}

char* strpbrk(const char* s, const char* accept) {
    while (*s) {
        const char* a = accept;
        while (*a) {
            if (*s == *a) {
                return (char*)s;
            }
            a++;
        }
        s++;
    }
    return NULL;
}

size_t strspn(const char* s, const char* accept) {
    size_t count = 0;
    while (s[count]) {
        const char* a = accept;
        int found = 0;
        while (*a) {
            if (s[count] == *a) {
                found = 1;
                break;
            }
            a++;
        }
        if (!found) {
            break;
        }
        count++;
    }
    return count;
}

size_t strcspn(const char* s, const char* reject) {
    size_t count = 0;
    while (s[count]) {
        const char* r = reject;
        int found = 0;
        while (*r) {
            if (s[count] == *r) {
                found = 1;
                break;
            }
            r++;
        }
        if (found) {
            break;
        }
        count++;
    }
    return count;
}

char* strtok(char* s, const char* delim) {
    static char* saveptr = NULL;
    return strtok_r(s, delim, &saveptr);
}

char* strtok_r(char* s, const char* delim, char** saveptr) {
    char* token;
    
    if (s == NULL) {
        s = *saveptr;
    }
    
    s += strspn(s, delim);
    
    if (*s == '\0') {
        *saveptr = s;
        return NULL;
    }
    
    token = s;
    s = strpbrk(token, delim);
    
    if (s == NULL) {
        *saveptr = token + strlen(token);
    } else {
        *s = '\0';
        *saveptr = s + 1;
    }
    
    return token;
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) {
            d[i-1] = s[i-1];
        }
    }
    return dest;
}

void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = (const unsigned char*)s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == (unsigned char)c) {
            return (void*)(p + i);
        }
    }
    return NULL;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

int strcasecmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}