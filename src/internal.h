#ifndef PSXROM_INTERNAL_H
#define PSXROM_INTERNAL_H

#include <stdint.h>
#include <stdio.h>

struct psxrom_disc {
    FILE *f;
    uint32_t sector_size;
    uint32_t data_offset;
    uint32_t sectors;
};

#include <ctype.h>

static inline int strncasecmp_compat(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return x - y;
        if (!x) break;
    }
    return 0;
}

#endif
