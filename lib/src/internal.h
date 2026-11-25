#ifndef PSXROM_INTERNAL_H
#define PSXROM_INTERNAL_H

#include <stdint.h>
#include <stdio.h>

#ifdef PSXROM_ENABLE_CHD
#include <libchdr/chd.h>
#endif

struct psxrom_disc {
	FILE *f;
	uint64_t base_offset;
	uint32_t sector_size;
	uint32_t data_offset;
	uint32_t sectors;
	int has_subheader;
#ifdef PSXROM_ENABLE_CHD
	chd_file *chd;
	uint8_t *hunk;
	uint32_t hunk_size;
	uint32_t cached_hunk;
#endif
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
