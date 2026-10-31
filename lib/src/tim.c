#include "psxrom/psxrom.h"

#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

int psxrom_tim_probe(const uint8_t *data, size_t size)
{
	psxrom_tim t;
	return psxrom_tim_parse(data, size, &t) == PSXROM_OK;
}

psxrom_result psxrom_tim_parse(const uint8_t *data, size_t size, psxrom_tim *out)
{
	if (size < 12 || rd32(data) != 0x10) return PSXROM_ERR_FORMAT;
	uint32_t flags = rd32(data + 4);
	if (flags & ~0xFu || (flags & 7) > 3) return PSXROM_ERR_FORMAT;

	memset(out, 0, sizeof *out);
	static const uint32_t bpps[] = {4, 8, 16, 24};
	out->bpp = bpps[flags & 3];
	size_t pos = 8;

	if (flags & 8) {
		if (size < pos + 12) return PSXROM_ERR_FORMAT;
		uint32_t bnum = rd32(data + pos);
		out->clut_x = rd16(data + pos + 4);
		out->clut_y = rd16(data + pos + 6);
		out->clut_w = rd16(data + pos + 8);
		out->clut_h = rd16(data + pos + 10);
		if (bnum < 12 || bnum > size - pos) return PSXROM_ERR_FORMAT;
		if ((size_t)out->clut_w * out->clut_h * 2 + 12 > bnum) return PSXROM_ERR_FORMAT;
		if (!out->clut_w || !out->clut_h) return PSXROM_ERR_FORMAT;
		out->has_clut = 1;
		out->clut = data + pos + 12;
		out->num_cluts = out->clut_h;
		pos += bnum;
	} else if (out->bpp <= 8) {
		return PSXROM_ERR_FORMAT;
	}

	if (size < pos + 12) return PSXROM_ERR_FORMAT;
	uint32_t bnum = rd32(data + pos);
	uint16_t words = rd16(data + pos + 8);
	out->img_x = rd16(data + pos + 4);
	out->img_y = rd16(data + pos + 6);
	out->height = rd16(data + pos + 10);
	if (bnum < 12 || bnum > size - pos || !words || !out->height) return PSXROM_ERR_FORMAT;
	if ((size_t)words * out->height * 2 + 12 > bnum) return PSXROM_ERR_FORMAT;

	switch (out->bpp) {
	case 4: out->width = (uint16_t)(words * 4); break;
	case 8: out->width = (uint16_t)(words * 2); break;
	case 16: out->width = words; break;
	default: out->width = (uint16_t)(words * 2 / 3); break;
	}
	out->pixels = data + pos + 12;
	out->pixels_size = (size_t)words * out->height * 2;
	return PSXROM_OK;
}

static void put15(uint8_t *dst, uint16_t c)
{
	dst[0] = (uint8_t)(((c & 31) << 3) | ((c & 31) >> 2));
	dst[1] = (uint8_t)((((c >> 5) & 31) << 3) | (((c >> 5) & 31) >> 2));
	dst[2] = (uint8_t)((((c >> 10) & 31) << 3) | (((c >> 10) & 31) >> 2));
	dst[3] = c == 0 ? 0 : 255;
}

psxrom_result psxrom_tim_to_rgba(const psxrom_tim *t, uint32_t clut_index, uint8_t **rgba)
{
	if (t->bpp <= 8 && (!t->has_clut || clut_index >= t->num_cluts)) return PSXROM_ERR_RANGE;
	uint32_t entries = t->has_clut ? t->clut_w : 0;
	uint8_t *img = malloc((size_t)t->width * t->height * 4);
	if (!img) return PSXROM_ERR_NOMEM;

	size_t stride = t->pixels_size / t->height; /* bytes per row, padded to words */
	const uint8_t *pal = t->clut ? t->clut + (size_t)clut_index * entries * 2 : NULL;

	for (uint32_t y = 0; y < t->height; y++) {
		const uint8_t *row = t->pixels + y * stride;
		uint8_t *dst = img + (size_t)y * t->width * 4;
		for (uint32_t x = 0; x < t->width; x++, dst += 4) {
			switch (t->bpp) {
			case 4: {
				uint32_t i = (row[x / 2] >> ((x & 1) * 4)) & 15;
				if (i < entries) put15(dst, rd16(pal + i * 2)); else put15(dst, 0);
				break;
			}
			case 8: {
				uint32_t i = row[x];
				if (i < entries) put15(dst, rd16(pal + i * 2)); else put15(dst, 0);
				break;
			}
			case 16:
				put15(dst, rd16(row + x * 2));
				break;
			default:
				dst[0] = row[x * 3];
				dst[1] = row[x * 3 + 1];
				dst[2] = row[x * 3 + 2];
				dst[3] = 255;
				break;
			}
		}
	}
	*rgba = img;
	return PSXROM_OK;
}
