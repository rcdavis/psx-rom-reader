#include <psxrom/psxrom.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir((p), 0777)
#endif

static void usage(void)
{
	fputs("usage: psxview <image> <command> [args]\n"
		  "  info                       volume label, boot executable, sector count\n"
		  "  ls                         list all files\n"
		  "  hex <path> [count]         hex dump (default 256 bytes)\n"
		  "  extract <path> <out>       extract a file\n"
		  "  extract-all <dir>          extract every file into dir\n"
		  "  tims                       list files that contain a valid TIM texture\n"
		  "  tim <path> <out.bmp> [n]   describe a TIM and convert it to BMP (palette n)\n"
		  "  tim-all <dir>              convert every TIM file to BMP in dir\n",
		  stderr);
}

static int write_file(const char *path, const void *data, size_t n)
{
	FILE *f = fopen(path, "wb");
	if (!f) return 0;
	int ok = fwrite(data, 1, n, f) == n;
	return fclose(f) == 0 && ok;
}

static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static int write_bmp(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
	size_t px = (size_t)w * h * 4;
	uint8_t *buf = calloc(1, 54 + px);
	if (!buf) return 0;
	buf[0] = 'B'; buf[1] = 'M';
	put32(buf + 2, (uint32_t)(54 + px));
	put32(buf + 10, 54);
	put32(buf + 14, 40);
	put32(buf + 18, w);
	put32(buf + 22, (uint32_t)-(int32_t)h); /* top-down */
	buf[26] = 1; buf[28] = 32;
	put32(buf + 34, (uint32_t)px);
	for (size_t i = 0; i < (size_t)w * h; i++) { /* RGBA -> BGRA */
		buf[54 + i * 4] = rgba[i * 4 + 2];
		buf[54 + i * 4 + 1] = rgba[i * 4 + 1];
		buf[54 + i * 4 + 2] = rgba[i * 4];
		buf[54 + i * 4 + 3] = rgba[i * 4 + 3];
	}
	int ok = write_file(path, buf, 54 + px);
	free(buf);
	return ok;
}

static void mkdirs_for(const char *path)
{
	char tmp[2048];
	snprintf(tmp, sizeof tmp, "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') { *p = 0; MKDIR(tmp); *p = '/'; }
	}
}

static int cb_ls(const psxrom_entry *e, void *u)
{
	(void)u;
	printf("%10u  %8u  %s%s\n", e->size, e->lba, e->path, e->is_dir ? "/" : "");
	return 0;
}

struct out_ctx { psxrom_disc *d; const char *dir; int errors; };

static int cb_extract(const psxrom_entry *e, void *u)
{
	struct out_ctx *c = u;
	char out[2048];
	snprintf(out, sizeof out, "%s%s", c->dir, e->path);
	if (e->is_dir) { mkdirs_for(out); MKDIR(out); return 0; }
	uint8_t *data; size_t n;
	psxrom_result r = psxrom_read_file(c->d, e, &data, &n);
	if (r == PSXROM_OK) {
		mkdirs_for(out);
		if (!write_file(out, data, n)) r = PSXROM_ERR_IO;
		free(data);
	}
	if (r != PSXROM_OK) { fprintf(stderr, "%s: %s\n", e->path, psxrom_strerror(r)); c->errors++; }
	else printf("%s\n", out);
	return 0;
}

static void describe_tim(const psxrom_tim *t)
{
	printf("TIM %ux%u %u-bpp, vram (%u,%u)", t->width, t->height, t->bpp, t->img_x, t->img_y);
	if (t->has_clut)
		printf(", clut %ux%u at (%u,%u): %u palette(s)", t->clut_w, t->clut_h,
			   t->clut_x, t->clut_y, t->num_cluts);
	putchar('\n');
}

static int cb_tims(const psxrom_entry *e, void *u)
{
	struct out_ctx *c = u;
	if (e->is_dir || e->size < 20) return 0;
	uint8_t *data; size_t n;
	if (psxrom_read_file(c->d, e, &data, &n) != PSXROM_OK) return 0;
	psxrom_tim t;
	if (psxrom_tim_parse(data, n, &t) == PSXROM_OK) {
		printf("%s: ", e->path);
		describe_tim(&t);
		if (c->dir) {
			uint8_t *rgba;
			char out[2048];
			snprintf(out, sizeof out, "%s%s.bmp", c->dir, e->path);
			if (psxrom_tim_to_rgba(&t, 0, &rgba) == PSXROM_OK) {
				mkdirs_for(out);
				if (!write_bmp(out, rgba, t.width, t.height)) c->errors++;
				free(rgba);
			}
		}
	}
	free(data);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 3) { usage(); return 2; }
	const char *cmd = argv[2];
	psxrom_disc *d;
	psxrom_result r = psxrom_disc_open(argv[1], &d);
	if (r != PSXROM_OK) { fprintf(stderr, "%s: %s\n", argv[1], psxrom_strerror(r)); return 1; }

	int rc = 0;
	if (!strcmp(cmd, "info")) {
		char label[64] = "";
		psxrom_volume_label(d, label, sizeof label);
		printf("label:   %s\nsectors: %u\n", label, psxrom_disc_sector_count(d));
		psxrom_entry e;
		if (psxrom_find(d, "SYSTEM.CNF", &e) == PSXROM_OK) {
			uint8_t *data; size_t n;
			if (psxrom_read_file(d, &e, &data, &n) == PSXROM_OK) {
				data[n] = 0;
				printf("SYSTEM.CNF:\n%s\n", (char *)data);
				free(data);
			}
		}
	} else if (!strcmp(cmd, "ls")) {
		r = psxrom_walk(d, cb_ls, NULL);
	} else if (!strcmp(cmd, "hex") && argc >= 4) {
		psxrom_entry e;
		uint8_t *data; size_t n;
		if ((r = psxrom_find(d, argv[3], &e)) == PSXROM_OK &&
			(r = psxrom_read_file(d, &e, &data, &n)) == PSXROM_OK) {
			size_t count = argc >= 5 ? strtoul(argv[4], NULL, 0) : 256;
			if (count > n) count = n;
			for (size_t i = 0; i < count; i += 16) {
				printf("%08zx  ", i);
				for (size_t j = 0; j < 16; j++) {
					if (i + j < count) printf("%02x ", data[i + j]); else printf("   ");
				}
				putchar(' ');
				for (size_t j = 0; j < 16 && i + j < count; j++)
					putchar(isprint(data[i + j]) ? data[i + j] : '.');
				putchar('\n');
			}
			free(data);
		}
	} else if (!strcmp(cmd, "extract") && argc >= 5) {
		psxrom_entry e;
		uint8_t *data; size_t n;
		if ((r = psxrom_find(d, argv[3], &e)) == PSXROM_OK &&
			(r = psxrom_read_file(d, &e, &data, &n)) == PSXROM_OK) {
			if (!write_file(argv[4], data, n)) r = PSXROM_ERR_IO;
			free(data);
		}
	} else if (!strcmp(cmd, "extract-all") && argc >= 4) {
		struct out_ctx c = {d, argv[3], 0};
		MKDIR(argv[3]);
		r = psxrom_walk(d, cb_extract, &c);
		if (c.errors) rc = 1;
	} else if (!strcmp(cmd, "tims") || (!strcmp(cmd, "tim-all") && argc >= 4)) {
		struct out_ctx c = {d, argc >= 4 ? argv[3] : NULL, 0};
		if (c.dir) MKDIR(c.dir);
		r = psxrom_walk(d, cb_tims, &c);
		if (c.errors) rc = 1;
	} else if (!strcmp(cmd, "tim") && argc >= 5) {
		psxrom_entry e;
		uint8_t *data; size_t n;
		if ((r = psxrom_find(d, argv[3], &e)) == PSXROM_OK &&
			(r = psxrom_read_file(d, &e, &data, &n)) == PSXROM_OK) {
			psxrom_tim t;
			uint8_t *rgba;
			if ((r = psxrom_tim_parse(data, n, &t)) == PSXROM_OK) {
				describe_tim(&t);
				uint32_t clut = argc >= 6 ? (uint32_t)strtoul(argv[5], NULL, 0) : 0;
				if ((r = psxrom_tim_to_rgba(&t, clut, &rgba)) == PSXROM_OK) {
					if (!write_bmp(argv[4], rgba, t.width, t.height)) r = PSXROM_ERR_IO;
					free(rgba);
				}
			}
			free(data);
		}
	} else {
		usage();
		psxrom_disc_close(d);
		return 2;
	}

	if (r != PSXROM_OK) { fprintf(stderr, "error: %s\n", psxrom_strerror(r)); rc = 1; }
	psxrom_disc_close(d);
	return rc;
}
