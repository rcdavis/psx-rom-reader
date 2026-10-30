#include "psxrom/psxrom.h"
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *psxrom_strerror(psxrom_result r)
{
	switch (r) {
	case PSXROM_OK: return "ok";
	case PSXROM_ERR_IO: return "I/O error";
	case PSXROM_ERR_FORMAT: return "invalid format";
	case PSXROM_ERR_NOT_FOUND: return "not found";
	case PSXROM_ERR_NOMEM: return "out of memory";
	case PSXROM_ERR_RANGE: return "out of range";
	}
	return "unknown error";
}

static const uint8_t SYNC[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};

static int fread_at(FILE *f, uint64_t off, void *buf, size_t n)
{
	if (fseek(f, (long)off, SEEK_SET) != 0) return 0;
	return fread(buf, 1, n, f) == n;
}

/* Probe for "\x01CD001" in the primary volume descriptor at LBA 16. */
static int detect_layout(psxrom_disc *d)
{
	static const struct { uint32_t sector; uint32_t data; } layouts[] = {
		{2048, 0}, {2352, 24}, {2352, 16}, {2336, 8}
	};
	for (size_t i = 0; i < sizeof layouts / sizeof *layouts; i++) {
		uint8_t hdr[6];
		uint64_t base = (uint64_t)16 * layouts[i].sector;
		if (!fread_at(d->f, base + layouts[i].data, hdr, 6)) continue;
		if (memcmp(hdr, "\x01" "CD001", 6) != 0) continue;
		if (layouts[i].sector == 2352) {
			uint8_t sync[12];
			if (!fread_at(d->f, base, sync, 12) || memcmp(sync, SYNC, 12) != 0) continue;
		}
		d->sector_size = layouts[i].sector;
		d->data_offset = layouts[i].data;
		d->has_subheader = layouts[i].data >= 8 && layouts[i].data != 16;
		return 1;
	}
	return 0;
}

psxrom_result psxrom_disc_open(const char *path, psxrom_disc **out)
{
	psxrom_disc *d = calloc(1, sizeof *d);
	if (!d) return PSXROM_ERR_NOMEM;
	d->f = fopen(path, "rb");
	if (!d->f) { free(d); return PSXROM_ERR_IO; }
	fseek(d->f, 0, SEEK_END);
	long len = ftell(d->f);
	if (len < 0 || !detect_layout(d)) {
		fclose(d->f);
		free(d);
		return PSXROM_ERR_FORMAT;
	}
	d->sectors = (uint32_t)((uint64_t)len / d->sector_size);
	*out = d;
	return PSXROM_OK;
}

void psxrom_disc_close(psxrom_disc *d)
{
	if (!d) return;
	fclose(d->f);
	free(d);
}

uint32_t psxrom_disc_sector_count(const psxrom_disc *d) { return d->sectors; }

psxrom_result psxrom_disc_read_sector(psxrom_disc *d, uint32_t lba, uint8_t out[2048])
{
	if (lba >= d->sectors) return PSXROM_ERR_RANGE;
	uint64_t off = (uint64_t)lba * d->sector_size + d->data_offset;
	return fread_at(d->f, off, out, 2048) ? PSXROM_OK : PSXROM_ERR_IO;
}

int psxrom_disc_has_raw(const psxrom_disc *d) { return d->has_subheader; }

psxrom_result psxrom_disc_read_raw(psxrom_disc *d, uint32_t lba, psxrom_raw_sector *out)
{
    if (lba >= d->sectors) return PSXROM_ERR_RANGE;
    memset(out, 0, sizeof *out);
    if (!d->has_subheader) {
        out->submode = 0x08;
        out->data_size = 2048;
        return psxrom_disc_read_sector(d, lba, out->data);
    }
    uint8_t buf[8 + 2324];
    uint64_t off = (uint64_t)lba * d->sector_size + d->data_offset - 8;
    if (!fread_at(d->f, off, buf, sizeof buf)) return PSXROM_ERR_IO;
    out->file = buf[0];
    out->channel = buf[1];
    out->submode = buf[2];
    out->coding = buf[3];
    out->data_size = (out->submode & 0x20) ? 2324 : 2048;
    memcpy(out->data, buf + 8, out->data_size);
    return PSXROM_OK;
}
