#include "psxrom/psxrom.h"
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

const char *psxrom_strerror(psxrom_result r)
{
	switch (r) {
	case PSXROM_OK: return "ok";
	case PSXROM_ERR_IO: return "I/O error";
	case PSXROM_ERR_FORMAT: return "invalid format";
	case PSXROM_ERR_NOT_FOUND: return "not found";
	case PSXROM_ERR_NOMEM: return "out of memory";
	case PSXROM_ERR_RANGE: return "out of range";
	case PSXROM_ERR_UNSUPPORTED: return "unsupported disc image or feature";
	}
	return "unknown error";
}

static const uint8_t SYNC[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};

static int fread_at(FILE *f, uint64_t off, void *buf, size_t n)
{
	if (off > LONG_MAX) return 0;
	if (fseek(f, (long)off, SEEK_SET) != 0) return 0;
	return fread(buf, 1, n, f) == n;
}

static int image_read(psxrom_disc *d, uint64_t off, void *buf, size_t n)
{
	off += d->base_offset;
#ifdef PSXROM_ENABLE_CHD
	if (d->chd) {
		uint8_t *dest = buf;
		while (n) {
			uint64_t hunk = off / d->hunk_size;
			size_t pos = (size_t)(off % d->hunk_size);
			size_t count = d->hunk_size - pos;
			if (count > n) count = n;
			if (hunk > UINT32_MAX) return 0;
			if (d->cached_hunk != (uint32_t)hunk) {
				d->cached_hunk = UINT32_MAX;
				if (chd_read(d->chd, (uint32_t)hunk, d->hunk) != CHDERR_NONE) return 0;
				d->cached_hunk = (uint32_t)hunk;
			}
			memcpy(dest, d->hunk + pos, count);
			dest += count;
			off += count;
			n -= count;
		}
		return 1;
	}
#endif
	return fread_at(d->f, off, buf, n);
}

/* Probe for "\x01CD001" in the primary volume descriptor at LBA 16. */
static int detect_layout(psxrom_disc *d)
{
	static const struct { uint32_t sector; uint32_t data; } layouts[] = {
		{2048, 0}, {2352, 24}, {2352, 16}, {2336, 8}
	};
	if (d->sectors && d->sectors <= 16) return 0;
	for (size_t i = 0; i < sizeof layouts / sizeof *layouts; i++) {
		uint8_t hdr[6];
		uint32_t stride = layouts[i].sector;
#ifdef PSXROM_ENABLE_CHD
		if (d->chd) {
			if (stride != 2352) continue;
			stride = 2448;
		}
#endif
		if (d->sector_size && d->sector_size != stride) continue;
		uint64_t base = (uint64_t)16 * stride;
		if (!image_read(d, base + layouts[i].data, hdr, 6)) continue;
		if (memcmp(hdr, "\x01" "CD001", 6) != 0) continue;
		if (layouts[i].sector == 2352) {
			uint8_t sync[12];
			if (!image_read(d, base, sync, 12) || memcmp(sync, SYNC, 12) != 0) continue;
		}
		d->sector_size = stride;
		d->data_offset = layouts[i].data;
		d->has_subheader = layouts[i].data >= 8 && layouts[i].data != 16;
		return 1;
	}
	return 0;
}

static int has_extension(const char *path, const char *ext)
{
	const size_t n = strlen(path), e = strlen(ext);
	return n >= e && strncasecmp_compat(path + n - e, ext, e) == 0;
}

static int cue_time(const char *s, uint32_t *frames)
{
	unsigned m, sec, f;
	char tail;
	if (sscanf(s, "%u:%u:%u %c", &m, &sec, &f, &tail) != 3 ||
		sec >= 60 || f >= 75 || m > UINT32_MAX / 4500 - 1) return 0;
	*frames = (m * 60 + sec) * 75 + f;
	return 1;
}

/* The public disc interface describes the first data track, not CD-DA tracks. */
static psxrom_result open_cue(psxrom_disc *d, const char *path)
{
	FILE *cue = fopen(path, "rb");
	if (!cue) return PSXROM_ERR_IO;
	char line[4096], filename[4096] = "", type[32] = "";
	uint32_t start = 0, end = UINT32_MAX, sector = 0;
	int track = 0, have_index = 0, have_file = 0;
	psxrom_result result = PSXROM_ERR_FORMAT;
	while (fgets(line, sizeof line, cue)) {
		if (!strchr(line, '\n') && !feof(cue)) goto done;
		char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (!strncasecmp_compat(p, "FILE ", 5) || !strncasecmp_compat(p, "FILE\t", 5)) {
			if (have_index) break;
			p += 5;
			while (*p == ' ' || *p == '\t') p++;
			char *name = p, *endname;
			if (*p == '"') {
				name = ++p;
				endname = strchr(p, '"');
				if (!endname) goto done;
				p = endname + 1;
			} else {
				while (*p && !isspace((unsigned char)*p)) p++;
				endname = p;
			}
			size_t len = (size_t)(endname - name);
			if (!len || len >= sizeof filename || sscanf(p, " %31s", type) != 1) goto done;
			if (strncasecmp_compat(type, "BINARY", 7)) { result = PSXROM_ERR_UNSUPPORTED; goto done; }
			memcpy(filename, name, len);
			filename[len] = 0;
			for (size_t i = 0; i < len; i++) if (filename[i] == '\\') filename[i] = '/';
			have_file = 1;
		} else if (!strncasecmp_compat(p, "TRACK ", 6) || !strncasecmp_compat(p, "TRACK\t", 6)) {
			unsigned number;
			if (!have_file || sscanf(p + 5, "%u %31s", &number, type) != 2) goto done;
			track++;
			if (track == 1) {
				if (number != 1) goto done;
				if (!strncasecmp_compat(type, "MODE1/2048", 11)) sector = 2048;
				else if (!strncasecmp_compat(type, "MODE1/2352", 11) ||
						 !strncasecmp_compat(type, "MODE2/2352", 11)) sector = 2352;
				else if (!strncasecmp_compat(type, "MODE2/2336", 11)) sector = 2336;
				else { result = PSXROM_ERR_UNSUPPORTED; goto done; }
			} else if (!have_index) goto done;
		} else if (!strncasecmp_compat(p, "INDEX ", 6) || !strncasecmp_compat(p, "INDEX\t", 6)) {
			unsigned number;
			char time[64];
			uint32_t frame;
			if (sscanf(p + 5, "%u %63s", &number, time) != 2 || !cue_time(time, &frame)) goto done;
			if (track == 1 && number == 1 && !have_index) { start = frame; have_index = 1; }
			else if (track > 1 && (number == 0 || number == 1)) { end = frame; break; }
		}
	}
	if (ferror(cue)) { result = PSXROM_ERR_IO; goto done; }
	if (!have_index || !sector || (track > 1 && end == UINT32_MAX)) goto done;
	{
		const char *slash = strrchr(path, '/');
		const char *backslash = strrchr(path, '\\');
		if (backslash && (!slash || backslash > slash)) slash = backslash;
		size_t dir = slash ? (size_t)(slash - path + 1) : 0;
		int absolute = filename[0] == '/' || (isalpha((unsigned char)filename[0]) && filename[1] == ':');
		if (absolute) dir = 0;
		size_t n = dir + strlen(filename) + 1;
		char *bin = malloc(n);
		if (!bin) { result = PSXROM_ERR_NOMEM; goto done; }
		memcpy(bin, path, dir);
		strcpy(bin + dir, filename);
		d->f = fopen(bin, "rb");
		free(bin);
		if (!d->f) { result = PSXROM_ERR_IO; goto done; }
		if (fseek(d->f, 0, SEEK_END) != 0) { result = PSXROM_ERR_IO; goto done; }
		long len = ftell(d->f);
		if (len < 0) { result = PSXROM_ERR_IO; goto done; }
		uint64_t total = (uint64_t)len / sector;
		if ((uint64_t)len % sector || start >= total || end <= start ||
			(end != UINT32_MAX && end > total)) goto done;
		if (end == UINT32_MAX) {
			if (total > UINT32_MAX) goto done;
			end = (uint32_t)total;
		}
		d->base_offset = (uint64_t)start * sector;
		d->sector_size = sector;
		d->sectors = end - start;
		result = detect_layout(d) ? PSXROM_OK : PSXROM_ERR_FORMAT;
	}
done:
	fclose(cue);
	return result;
}

#ifdef PSXROM_ENABLE_CHD
static psxrom_result open_chd(psxrom_disc *d, const char *path)
{
	chd_error err = chd_open(path, CHD_OPEN_READ, NULL, &d->chd);
	if (err != CHDERR_NONE) {
		if (err == CHDERR_OUT_OF_MEMORY) return PSXROM_ERR_NOMEM;
		if (err == CHDERR_FILE_NOT_FOUND || err == CHDERR_READ_ERROR) return PSXROM_ERR_IO;
		if (err == CHDERR_REQUIRES_PARENT || err == CHDERR_UNSUPPORTED_VERSION ||
			err == CHDERR_NOT_SUPPORTED) return PSXROM_ERR_UNSUPPORTED;
		return PSXROM_ERR_FORMAT;
	}
	const chd_header *h = chd_get_header(d->chd);
	if (h->unitbytes != 2448 || !h->hunkbytes || h->hunkbytes % 2448) return PSXROM_ERR_UNSUPPORTED;
	char meta[512] = {0};
	uint32_t length = 0;
	err = chd_get_metadata(d->chd, CDROM_TRACK_METADATA2_TAG, 0, meta, sizeof meta - 1, &length, NULL, NULL);
	int version2 = err == CHDERR_NONE;
	if (err == CHDERR_METADATA_NOT_FOUND)
		err = chd_get_metadata(d->chd, CDROM_TRACK_METADATA_TAG, 0, meta, sizeof meta - 1, &length, NULL, NULL);
	if (err != CHDERR_NONE || length >= sizeof meta) return PSXROM_ERR_FORMAT;
	int track, frames, pregap = 0, postgap = 0;
	char type[32], subtype[32], pgtype[32] = "", pgsub[32];
	int parsed = sscanf(meta, "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d",
						&track, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap);
	if (parsed < (version2 ? 8 : 4) || track != 1 || frames <= 0 || pregap < 0 || postgap < 0)
		return PSXROM_ERR_FORMAT;
	if (strcmp(type, "MODE2_RAW") && strcmp(type, "MODE1_RAW")) return PSXROM_ERR_UNSUPPORTED;
	uint32_t start = version2 && pgtype[0] == 'V' ? (uint32_t)pregap : 0;
	if (start >= (uint32_t)frames || (uint64_t)frames * 2448 > h->logicalbytes) return PSXROM_ERR_FORMAT;
	d->hunk = malloc(h->hunkbytes);
	if (!d->hunk) return PSXROM_ERR_NOMEM;
	d->hunk_size = h->hunkbytes;
	d->cached_hunk = UINT32_MAX;
	d->base_offset = (uint64_t)start * 2448;
	d->sector_size = 2448;
	d->sectors = (uint32_t)frames - start;
	return detect_layout(d) ? PSXROM_OK : PSXROM_ERR_FORMAT;
}
#endif

psxrom_result psxrom_disc_open(const char *path, psxrom_disc **out)
{
	*out = NULL;
	psxrom_disc *d = calloc(1, sizeof *d);
	if (!d) return PSXROM_ERR_NOMEM;
	psxrom_result result;
	if (has_extension(path, ".cue")) {
		result = open_cue(d, path);
		if (result != PSXROM_OK) { psxrom_disc_close(d); return result; }
		*out = d;
		return PSXROM_OK;
	}
	d->f = fopen(path, "rb");
	if (!d->f) { free(d); return PSXROM_ERR_IO; }
	uint8_t magic[8];
	int is_chd = fread_at(d->f, 0, magic, sizeof magic) && !memcmp(magic, "MComprHD", 8);
	if (is_chd || has_extension(path, ".chd")) {
		fclose(d->f);
		d->f = NULL;
#ifdef PSXROM_ENABLE_CHD
		result = open_chd(d, path);
#else
		result = PSXROM_ERR_UNSUPPORTED;
#endif
		if (result != PSXROM_OK) { psxrom_disc_close(d); return result; }
		*out = d;
		return PSXROM_OK;
	}
	if (fseek(d->f, 0, SEEK_END) != 0) { psxrom_disc_close(d); return PSXROM_ERR_IO; }
	long len = ftell(d->f);
	if (len < 0 || !detect_layout(d) || (uint64_t)len / d->sector_size > UINT32_MAX) {
		psxrom_disc_close(d);
		return PSXROM_ERR_FORMAT;
	}
	d->sectors = (uint32_t)((uint64_t)len / d->sector_size);
	*out = d;
	return PSXROM_OK;
}

void psxrom_disc_close(psxrom_disc *d)
{
	if (!d) return;
	if (d->f) fclose(d->f);
#ifdef PSXROM_ENABLE_CHD
	if (d->chd) chd_close(d->chd);
	free(d->hunk);
#endif
	free(d);
}

uint32_t psxrom_disc_sector_count(const psxrom_disc *d) { return d->sectors; }

psxrom_result psxrom_disc_read_sector(psxrom_disc *d, uint32_t lba, uint8_t out[2048])
{
	if (lba >= d->sectors) return PSXROM_ERR_RANGE;
	uint64_t off = (uint64_t)lba * d->sector_size + d->data_offset;
	return image_read(d, off, out, 2048) ? PSXROM_OK : PSXROM_ERR_IO;
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
    if (!image_read(d, off, buf, sizeof buf)) return PSXROM_ERR_IO;
    out->file = buf[0];
    out->channel = buf[1];
    out->submode = buf[2];
    out->coding = buf[3];
    out->data_size = (out->submode & 0x20) ? 2324 : 2048;
    memcpy(out->data, buf + 8, out->data_size);
    return PSXROM_OK;
}
