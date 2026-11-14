#include "psxrom/psxrom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static void path(char *out, size_t size, const char *dir, const char *name)
{
	snprintf(out, size, "%s/%s", dir, name);
}

static int text_file(const char *name, const char *text)
{
	FILE *f = fopen(name, "wb");
	if (!f) return 0;
	int ok = fputs(text, f) >= 0;
	return fclose(f) == 0 && ok;
}

static int make_fixtures(const char *dir)
{
	char name[4096];
	path(name, sizeof name, dir, "disc.bin");
	FILE *f = fopen(name, "wb");
	CHECK(f);
	for (unsigned i = 0; i < 48; i++) {
		uint8_t sector[2352] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
		sector[15] = 2;
		sector[16] = sector[20] = (uint8_t)i;
		sector[17] = sector[21] = 2;
		sector[18] = sector[22] = i % 2 ? 0x20 : 0x08;
		sector[19] = sector[23] = 1;
		memset(sector + 24, (int)i, 2324);
		if (i == 16 || i == 20) memcpy(sector + 24, "\1CD001", 6);
		CHECK(fwrite(sector, 1, sizeof sector, f) == sizeof sector);
	}
	CHECK(fclose(f) == 0);
	path(name, sizeof name, dir, "whole.cue");
	CHECK(text_file(name, "FILE \"disc.bin\" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n"));
	path(name, sizeof name, dir, "offset.cue");
	CHECK(text_file(name, "REM test\nFILE \"disc.bin\" BINARY\n TRACK 01 MODE2/2352\n"
					" INDEX 00 00:00:00\n INDEX 01 00:00:04\n TRACK 02 AUDIO\n"
					" INDEX 00 00:00:44\n INDEX 01 00:00:46\n"));
	path(name, sizeof name, dir, "missing.cue");
	CHECK(text_file(name, "FILE \"missing.bin\" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n"));
	path(name, sizeof name, dir, "bad.cue");
	CHECK(text_file(name, "FILE \"disc.bin\" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:80:00\n"));
	path(name, sizeof name, dir, "unsupported.cue");
	CHECK(text_file(name, "FILE \"disc.bin\" WAVE\n TRACK 01 AUDIO\n INDEX 01 00:00:00\n"));
	return 0;
}

static int compare(psxrom_disc *a, psxrom_disc *b, uint32_t offset)
{
	uint8_t x[2048], y[2048];
	psxrom_raw_sector rx, ry;
	uint32_t n = psxrom_disc_sector_count(b);
	CHECK(psxrom_disc_has_raw(a) && psxrom_disc_has_raw(b));
	for (uint32_t i = 0; i < n; i++) {
		CHECK(psxrom_disc_read_sector(a, i + offset, x) == PSXROM_OK);
		CHECK(psxrom_disc_read_sector(b, i, y) == PSXROM_OK);
		CHECK(memcmp(x, y, sizeof x) == 0);
		CHECK(psxrom_disc_read_raw(a, i + offset, &rx) == PSXROM_OK);
		CHECK(psxrom_disc_read_raw(b, i, &ry) == PSXROM_OK);
		CHECK(rx.file == ry.file && rx.channel == ry.channel && rx.submode == ry.submode &&
			  rx.coding == ry.coding && rx.data_size == ry.data_size);
		CHECK(memcmp(rx.data, ry.data, rx.data_size) == 0);
	}
	CHECK(psxrom_disc_read_sector(b, n, y) == PSXROM_ERR_RANGE);
	return 0;
}

int main(int argc, char **argv)
{
	CHECK(argc >= 2);
	char name[4096];
	psxrom_disc *raw, *other;
	if (argc == 2) CHECK(make_fixtures(argv[1]) == 0);
	path(name, sizeof name, argv[1], "disc.bin");
	CHECK(psxrom_disc_open(name, &raw) == PSXROM_OK);
	int offset_chd = argc > 2 && !strcmp(argv[2], "compare-offset");
	path(name, sizeof name, argv[1], argc == 2 ? "whole.cue" : offset_chd ? "offset.chd" : "test.chd");
	CHECK(psxrom_disc_open(name, &other) == PSXROM_OK);
	CHECK(psxrom_disc_sector_count(other) == (offset_chd ? 40u : 48u));
	CHECK(compare(raw, other, offset_chd ? 4 : 0) == 0);
	psxrom_disc_close(other);
	if (argc == 2) {
		path(name, sizeof name, argv[1], "offset.cue");
		CHECK(psxrom_disc_open(name, &other) == PSXROM_OK);
		CHECK(psxrom_disc_sector_count(other) == 40);
		CHECK(compare(raw, other, 4) == 0);
		psxrom_disc_close(other);
		path(name, sizeof name, argv[1], "missing.cue");
		CHECK(psxrom_disc_open(name, &other) == PSXROM_ERR_IO);
		path(name, sizeof name, argv[1], "bad.cue");
		CHECK(psxrom_disc_open(name, &other) == PSXROM_ERR_FORMAT);
		path(name, sizeof name, argv[1], "unsupported.cue");
		CHECK(psxrom_disc_open(name, &other) == PSXROM_ERR_UNSUPPORTED);
	}
	psxrom_disc_close(raw);
	return 0;
}
