#include <stdlib.h>
#include <string.h>

#include "psxrom/psxrom.h"

#define SETUP_FIXED 0x98
#define OBJECT_SIZE 0x12
#define TILE_SIZE (128 * 128)
#define TFS_TILE_SIZE (4 + TILE_SIZE)
#define COLLISION_SIZE (100 * 100)

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t rds32(const uint8_t *p) { return (int32_t)rd32(p); }

psxrom_result psxrom_map_parse(const uint8_t *data, size_t size, psxrom_map *out)
{
    memset(out, 0, sizeof *out);
    if (size < 12) return PSXROM_ERR_FORMAT;

    /* The header is a list of u32 offsets ending where the first section (the setup) starts:
     * setup, [images..., objects,] elements, collision. */
    uint32_t setup = rd32(data);
    if (setup < 12 || setup % 4 || setup > size) return PSXROM_ERR_FORMAT;
    size_t words = setup / 4;
    size_t nimg = words == 3 ? 0 : words - 4;
    if (words < 3 || (words > 3 && words < 5)) return PSXROM_ERR_FORMAT;
    uint32_t objects = nimg ? rd32(data + 4 * (1 + nimg)) : 0;
    uint32_t elements = rd32(data + 4 * (words - 2));
    uint32_t collision = rd32(data + 4 * (words - 1));
    out->num_images = nimg;

    if (setup + SETUP_FIXED > size) return PSXROM_ERR_FORMAT;
    const uint8_t *s = data + setup;
    for (int i = 0; i < 3; i++) {
        out->camera_origin[i] = rds32(s + 4 * i);
        out->camera_target[i] = rds32(s + 12 + 4 * i);
    }
    for (int l = 0; l < 3; l++)
        for (int i = 0; i < 3; i++) {
            out->lights[l].pos[i] = rds32(s + 24 + l * 24 + 4 * i);
            out->lights[l].rgb[i] = rd32(s + 24 + l * 24 + 12 + 4 * i);
        }
    out->view_distance = rd32(s + 108);
    out->tiles_w = rd32(s + 0x90);
    out->tiles_h = rd32(s + 0x94);
    if (out->tiles_w > 64 || out->tiles_h > 64) return PSXROM_ERR_FORMAT;
    size_t ntiles = (size_t)out->tiles_w * out->tiles_h;
    if (setup + SETUP_FIXED + ntiles * 4 > size) return PSXROM_ERR_FORMAT;
    out->tiles = malloc((ntiles ? ntiles : 1) * sizeof *out->tiles);
    if (!out->tiles) return PSXROM_ERR_NOMEM;
    for (size_t i = 0; i < ntiles; i++) out->tiles[i] = rd32(s + SETUP_FIXED + i * 4);

    if ((size_t)elements + 122 > size) goto bad;
    const uint8_t *e = data + elements;
    for (int i = 0; i < 10; i++) {
        for (int k = 0; k < 4; k++) out->spawn[i][k] = rds16(e + k * 20 + i * 2);
        out->warp_map[i] = rd16(e + 80 + i * 2);
        out->warp_spawn[i] = rd16(e + 100 + i * 2);
    }
    out->num_digimon = rd16(e + 120);

    if (nimg) {
        if ((size_t)objects + 2 > size) goto bad;
        out->num_objects = rd16(data + objects);
        size_t after = (size_t)objects + 2 + (size_t)out->num_objects * OBJECT_SIZE;
        if (after + 2 > size) goto bad;
        out->num_instances = rd16(data + after);
    }

    if ((size_t)collision + COLLISION_SIZE > size) goto bad;
    for (int y = 0; y < 100; y++) memcpy(out->collision[y], data + collision + y * 100, 100);
    return PSXROM_OK;

bad:
    psxrom_map_free(out);
    return PSXROM_ERR_FORMAT;
}

void psxrom_map_free(psxrom_map *m)
{
    free(m->tiles);
    memset(m, 0, sizeof *m);
}

psxrom_result psxrom_tfs_parse(const uint8_t *data, size_t size, psxrom_tfs *out)
{
    memset(out, 0, sizeof *out);
    if (size < 8) return PSXROM_ERR_FORMAT;
    out->width = rd16(data);
    out->height = rd16(data + 2);
    out->num_palettes = rd32(data + 4);
    if (out->num_palettes > 64 || 8 + (size_t)out->num_palettes * 512 > size) return PSXROM_ERR_FORMAT;
    size_t pal_bytes = (size_t)out->num_palettes * 512;
    size_t ntiles = (size - 8 - pal_bytes) / TFS_TILE_SIZE;

    out->palettes = malloc(pal_bytes ? pal_bytes : 2);
    out->pixels = malloc(ntiles ? ntiles * TILE_SIZE : 1);
    if (!out->palettes || !out->pixels) { psxrom_tfs_free(out); return PSXROM_ERR_NOMEM; }
    for (size_t i = 0; i < pal_bytes / 2; i++) out->palettes[i] = rd16(data + 8 + i * 2);
    const uint8_t *t = data + 8 + pal_bytes;
    for (size_t i = 0; i < ntiles; i++) memcpy(out->pixels + i * TILE_SIZE, t + i * TFS_TILE_SIZE + 4, TILE_SIZE);
    out->num_tiles = ntiles;
    return PSXROM_OK;
}

void psxrom_tfs_free(psxrom_tfs *t)
{
    free(t->palettes);
    free(t->pixels);
    memset(t, 0, sizeof *t);
}

psxrom_result psxrom_map_render(const psxrom_map *m, const psxrom_tfs *t, uint32_t palette,
                                uint8_t **rgba, uint32_t *width, uint32_t *height)
{
    if (palette >= t->num_palettes || !m->tiles_w || !m->tiles_h) return PSXROM_ERR_RANGE;
    size_t used = 0;
    for (size_t i = 0; i < (size_t)m->tiles_w * m->tiles_h; i++)
        if (m->tiles[i] != 0xFFFFFFFFu) used++;
    if (used > t->num_tiles) return PSXROM_ERR_FORMAT;

    uint32_t w = m->tiles_w * 128, h = m->tiles_h * 128;
    uint8_t *img = calloc((size_t)w * h, 4);
    if (!img) return PSXROM_ERR_NOMEM;

    const uint16_t *pal = t->palettes + (size_t)palette * 256;
    uint8_t lut[256][4];
    for (int i = 0; i < 256; i++) {
        uint16_t c = pal[i];
        lut[i][0] = (uint8_t)((c & 31) * 255 / 31);
        lut[i][1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
        lut[i][2] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
        lut[i][3] = 255;
    }

    /* Non-empty cells consume the tile images in order. */
    size_t next = 0;
    for (uint32_t ty = 0; ty < m->tiles_h; ty++)
        for (uint32_t tx = 0; tx < m->tiles_w; tx++) {
            if (m->tiles[ty * m->tiles_w + tx] == 0xFFFFFFFFu) continue;
            const uint8_t *src = t->pixels + next++ * TILE_SIZE;
            for (uint32_t y = 0; y < 128; y++) {
                uint8_t *dst = img + (((size_t)ty * 128 + y) * w + (size_t)tx * 128) * 4;
                for (uint32_t x = 0; x < 128; x++) memcpy(dst + x * 4, lut[src[y * 128 + x]], 4);
            }
        }
    *rgba = img;
    *width = w;
    *height = h;
    return PSXROM_OK;
}
