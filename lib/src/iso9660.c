#include "psxrom/psxrom.h"
#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 32

static uint32_t rd32(const uint8_t *p) /* little-endian half of a both-endian field */
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static psxrom_result read_root(psxrom_disc *d, psxrom_entry *root)
{
	uint8_t pvd[2048];
	psxrom_result r = psxrom_disc_read_sector(d, 16, pvd);
	if (r != PSXROM_OK) return r;
	if (memcmp(pvd, "\x01" "CD001", 6) != 0) return PSXROM_ERR_FORMAT;
	memset(root, 0, sizeof *root);
	root->lba = rd32(pvd + 156 + 2);
	root->size = rd32(pvd + 156 + 10);
	root->is_dir = 1;
	strcpy(root->path, "/");
	return PSXROM_OK;
}

psxrom_result psxrom_volume_label(psxrom_disc *d, char *out, size_t out_size)
{
	uint8_t pvd[2048];
	psxrom_result r = psxrom_disc_read_sector(d, 16, pvd);
	if (r != PSXROM_OK) return r;
	size_t n = 32;
	while (n > 0 && (pvd[40 + n - 1] == ' ' || pvd[40 + n - 1] == 0)) n--;
	if (out_size == 0) return PSXROM_ERR_RANGE;
	if (n >= out_size) n = out_size - 1;
	memcpy(out, pvd + 40, n);
	out[n] = 0;
	return PSXROM_OK;
}

psxrom_result psxrom_read_file(psxrom_disc *d, const psxrom_entry *e,
							   uint8_t **data, size_t *size)
{
	uint32_t sectors = (e->size + 2047) / 2048;
	uint8_t *buf = malloc((size_t)sectors * 2048 + 1);
	if (!buf) return PSXROM_ERR_NOMEM;
	for (uint32_t i = 0; i < sectors; i++) {
		psxrom_result r = psxrom_disc_read_sector(d, e->lba + i, buf + (size_t)i * 2048);
		if (r != PSXROM_OK) { free(buf); return r; }
	}
	*data = buf;
	*size = e->size;
	return PSXROM_OK;
}

/* Iterates a directory; fn returns non-zero to stop. */
typedef int (*dir_fn)(const psxrom_entry *e, void *user);

static psxrom_result iterate_dir(psxrom_disc *d, const psxrom_entry *dir, dir_fn fn, void *user)
{
	uint8_t *buf;
	size_t size;
	psxrom_result r = psxrom_read_file(d, dir, &buf, &size);
	if (r != PSXROM_OK) return r;

	size_t pos = 0;
	int stop = 0;
	while (pos < size && !stop) {
		uint8_t len = buf[pos];
		if (len == 0) { /* records never straddle sectors; skip padding */
			pos = (pos / 2048 + 1) * 2048;
			continue;
		}
		if (pos + len > size || len < 34) { r = PSXROM_ERR_FORMAT; break; }
		const uint8_t *rec = buf + pos;
		uint8_t nlen = rec[32];
		if (33 + (size_t)nlen > len) { r = PSXROM_ERR_FORMAT; break; }
		if (!(nlen == 1 && (rec[33] == 0 || rec[33] == 1))) { /* skip . and .. */
			psxrom_entry e;
			memset(&e, 0, sizeof e);
			memcpy(e.name, rec + 33, nlen);
			e.name[nlen] = 0;
			char *semi = strchr(e.name, ';');
			if (semi) *semi = 0;
			e.lba = rd32(rec + 2);
			e.size = rd32(rec + 10);
			e.is_dir = (rec[25] & 2) != 0;
			const char *base = dir->path;
			size_t bl = strlen(base);
			int n = snprintf(e.path, sizeof e.path, "%s%s%s", base,
							 (bl > 0 && base[bl - 1] == '/') ? "" : "/", e.name);
			if (n > 0 && (size_t)n < sizeof e.path) stop = fn(&e, user);
		}
		pos += len;
	}
	free(buf);
	return r;
}

struct walk_ctx {
	psxrom_disc *d;
	psxrom_walk_cb cb;
	void *user;
	int depth;
	int stopped;
	psxrom_result err;
};

static int walk_fn(const psxrom_entry *e, void *user)
{
	struct walk_ctx *c = user;
	if (c->cb(e, c->user)) { c->stopped = 1; return 1; }
	if (e->is_dir && c->depth < MAX_DEPTH) {
		c->depth++;
		psxrom_result r = iterate_dir(c->d, e, walk_fn, c);
		c->depth--;
		if (r != PSXROM_OK && c->err == PSXROM_OK) c->err = r;
		if (c->stopped) return 1;
	}
	return 0;
}

psxrom_result psxrom_walk(psxrom_disc *d, psxrom_walk_cb cb, void *user)
{
	psxrom_entry root;
	psxrom_result r = read_root(d, &root);
	if (r != PSXROM_OK) return r;
	struct walk_ctx c = {d, cb, user, 0, 0, PSXROM_OK};
	r = iterate_dir(d, &root, walk_fn, &c);
	return r != PSXROM_OK ? r : c.err;
}

struct find_ctx {
	const char *name;
	size_t len;
	psxrom_entry found;
	int ok;
};

static int find_fn(const psxrom_entry *e, void *user)
{
	struct find_ctx *c = user;
	if (strlen(e->name) == c->len && strncasecmp_compat(e->name, c->name, c->len) == 0) {
		c->found = *e;
		c->ok = 1;
		return 1;
	}
	return 0;
}

psxrom_result psxrom_find(psxrom_disc *d, const char *path, psxrom_entry *out)
{
	psxrom_entry cur;
	psxrom_result r = read_root(d, &cur);
	if (r != PSXROM_OK) return r;
	while (*path == '/') path++;
	while (*path) {
		const char *end = strchr(path, '/');
		size_t len = end ? (size_t)(end - path) : strlen(path);
		if (!cur.is_dir) return PSXROM_ERR_NOT_FOUND;
		struct find_ctx c = {path, len, {{0}, {0}, 0, 0, 0}, 0};
		r = iterate_dir(d, &cur, find_fn, &c);
		if (r != PSXROM_OK) return r;
		if (!c.ok) return PSXROM_ERR_NOT_FOUND;
		cur = c.found;
		path += len;
		while (*path == '/') path++;
	}
	*out = cur;
	return PSXROM_OK;
}
