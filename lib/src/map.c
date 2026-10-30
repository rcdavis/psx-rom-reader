#include <stdlib.h>
#include <string.h>

#include "psxrom/psxrom.h"

#define SETUP_FIXED 0x98
#define OBJECT_SIZE 0x12
#define INSTANCE_SIZE 0x26
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
    out->data = malloc(size);
    if (!out->data) return PSXROM_ERR_NOMEM;
    memcpy(out->data, data, size);
    data = out->data;
    out->num_images = nimg;

    if (setup + SETUP_FIXED > size) goto bad;
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
    if (out->tiles_w > 64 || out->tiles_h > 64) goto bad;
    size_t ntiles = (size_t)out->tiles_w * out->tiles_h;
    if (setup + SETUP_FIXED + ntiles * 4 > size) goto bad;
    out->tiles = malloc((ntiles ? ntiles : 1) * sizeof *out->tiles);
    if (!out->tiles) { psxrom_map_free(out); return PSXROM_ERR_NOMEM; }
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
        out->images = calloc(nimg, sizeof *out->images);
        if (!out->images) { psxrom_map_free(out); return PSXROM_ERR_NOMEM; }
        for (size_t i = 0; i < nimg; i++) {
            uint32_t off = rd32(data + 4 + 4 * i);
            if (off >= size || psxrom_tim_parse(data + off, size - off, &out->images[i]) != PSXROM_OK) goto bad;
        }

        if ((size_t)objects + 2 > size) goto bad;
        size_t n = rd16(data + objects);
        size_t after = (size_t)objects + 2 + n * OBJECT_SIZE;
        if (after + 2 > size) goto bad;
        size_t ni = rd16(data + after);
        if (after + 2 + ni * INSTANCE_SIZE > size) goto bad;
        out->objects = malloc((n ? n : 1) * sizeof *out->objects);
        out->instances = malloc((ni ? ni : 1) * sizeof *out->instances);
        if (!out->objects || !out->instances) { psxrom_map_free(out); return PSXROM_ERR_NOMEM; }
        out->num_objects = (uint16_t)n;
        out->num_instances = (uint16_t)ni;
        for (size_t i = 0; i < n; i++) {
            const uint8_t *p = data + objects + 2 + i * OBJECT_SIZE;
            psxrom_map_object *o = &out->objects[i];
            o->uv_x = rd16(p); o->uv_y = rd16(p + 2); o->width = rd16(p + 4); o->height = rd16(p + 6);
            for (int k = 0; k < 3; k++) o->pos[k] = rds16(p + 8 + 2 * k);
            o->clut = rd16(p + 14); o->transparency = rd16(p + 16);
        }
        for (size_t i = 0; i < ni; i++) {
            const uint8_t *p = data + after + 2 + i * INSTANCE_SIZE;
            psxrom_map_instance *in = &out->instances[i];
            for (int k = 0; k < 8; k++) {
                in->sprite[k] = rds16(p + 2 * k);
                in->duration[k] = rds16(p + 16 + 2 * k);
            }
            in->x = rd16(p + 32); in->y = rd16(p + 34); in->flag = rd16(p + 36);
        }
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
    free(m->tiles); free(m->objects); free(m->instances); free(m->images); free(m->data);
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

/* VRAM CLUT rows used by the game: 480 is the "active" row, 481.. hold the TFS palettes and 486 holds
 * the 4bpp sprite palettes. Embedded images may add rows of their own. Returns colour count (0 = none). */
static size_t clut_row(const psxrom_map *m, const psxrom_tfs *t, uint32_t y, uint16_t *out)
{
    size_t n = 0;
    if (y == 486) n = 256;
    if (y == 480 && t->num_palettes) y = 481;
    if (y >= 481 && y < 481 + t->num_palettes) {
        memcpy(out, t->palettes + (size_t)(y - 481) * 256, 512);
        n = 256;
    }
    for (size_t i = 0; i < m->num_images; i++) {
        const psxrom_tim *im = &m->images[i];
        if (im->bpp != 8 || !im->has_clut || im->clut_y == 480 || y < im->clut_y || y >= im->clut_y + im->clut_h)
            continue;
        n = im->clut_w < 256 ? im->clut_w : 256;
        for (size_t k = 0; k < n; k++) out[k] = rd16(im->clut + ((size_t)(y - im->clut_y) * im->clut_w + k) * 2);
    }
    if (y == 486) {
        for (size_t i = 0; i < m->num_images; i++) {
            const psxrom_tim *im = &m->images[i];
            if (im->bpp != 4 || !im->has_clut) continue;
            n = 0;
            for (uint32_t p = 0; p < im->clut_h && n + 16 <= 256; p++)
                for (int k = 0; k < 16 && k < im->clut_w; k++)
                    out[n++] = rd16(im->clut + ((size_t)p * im->clut_w + k) * 2);
            break;
        }
    }
    return n;
}

psxrom_result psxrom_map_render_object(const psxrom_map *m, const psxrom_tfs *t, size_t object,
                                       uint32_t palette, uint8_t **rgba, uint32_t *width, uint32_t *height)
{
    if (object >= m->num_objects) return PSXROM_ERR_RANGE;
    const psxrom_map_object *o = &m->objects[object];
    if (!o->width || !o->height) return PSXROM_ERR_NOT_FOUND;

    uint16_t pal[256] = {0};
    size_t npal;
    int is4 = o->clut < 16;
    if (o->clut == 0xFFFF) npal = clut_row(m, t, 481 + palette, pal);
    else if (is4) {
        npal = clut_row(m, t, 486, pal);
        size_t first = (size_t)o->clut * 16;
        npal = npal > first ? npal - first : 0;
        memmove(pal, pal + first, 32);
    }
    else npal = clut_row(m, t, 468 + o->clut, pal);

    /* Texture pages start at VRAM x = 384; the last image covering the texel wins, 4bpp after 8bpp. */
    uint32_t px = 384 + o->uv_x / (is4 ? 4 : 2);
    const psxrom_tim *im = NULL;
    for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0; i < m->num_images; i++) {
            const psxrom_tim *c = &m->images[i];
            if (c->bpp != (pass ? 4u : 8u)) continue;
            uint32_t words = c->width / (c->bpp == 4 ? 4 : 2);
            if (px >= c->img_x && px < c->img_x + words && o->uv_y >= c->img_y && o->uv_y < c->img_y + c->height) im = c;
        }
    if (!im || !im->width || !im->height) return PSXROM_ERR_NOT_FOUND;

    uint8_t *img = calloc((size_t)o->width * o->height, 4);
    if (!img) return PSXROM_ERR_NOMEM;
    size_t stride = im->pixels_size / im->height;
    int semi = o->transparency != 4;
    for (uint32_t y = 0; y < o->height; y++)
        for (uint32_t x = 0; x < o->width; x++) {
            uint32_t sx = (o->uv_x % im->width) + x, sy = (o->uv_y % im->height) + y;
            if (sx >= im->width || sy >= im->height) continue;
            const uint8_t *row = im->pixels + sy * stride;
            unsigned idx = im->bpp == 4 ? (row[sx / 2] >> ((sx & 1) * 4)) & 15 : row[sx];
            if (idx >= npal) continue;
            uint16_t c = pal[idx];
            uint8_t *d = img + ((size_t)y * o->width + x) * 4;
            if (!(c & 0x7FFF) && !(c & 0x8000)) continue;
            d[0] = (uint8_t)((c & 31) << 3);
            d[1] = (uint8_t)(((c >> 5) & 31) << 3);
            d[2] = (uint8_t)(((c >> 10) & 31) << 3);
            d[3] = (uint8_t)(semi && (c & 0x8000) ? 0x7F : 0xFF);
        }
    *rgba = img;
    *width = o->width;
    *height = o->height;
    return PSXROM_OK;
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

static int active_sprite(const psxrom_map_instance *in, uint32_t frame)
{
    uint32_t total = 0;
    for (int k = 0; k < 8 && in->sprite[k] != -1; k++) total += in->duration[k] > 0 ? (uint32_t)in->duration[k] : 0;
    if (!total) return in->sprite[0] >= 0 ? in->sprite[0] : -1;
    frame %= total;
    for (int k = 0; k < 8 && in->sprite[k] != -1; k++) {
        uint32_t d = in->duration[k] > 0 ? (uint32_t)in->duration[k] : 0;
        if (frame < d) return in->sprite[k];
        frame -= d;
    }
    return -1;
}

psxrom_result psxrom_map_render_scene(const psxrom_map *m, const psxrom_tfs *t, uint32_t palette, uint32_t frame,
                                      uint8_t **rgba, uint32_t *width, uint32_t *height)
{
    psxrom_result r = psxrom_map_render(m, t, palette, rgba, width, height);
    if (r != PSXROM_OK) return r;
    for (size_t i = 0; i < m->num_instances; i++) {
        const psxrom_map_instance *in = &m->instances[i];
        int sp = active_sprite(in, frame);
        if (sp < 0 || (size_t)sp >= m->num_objects) continue;
        uint8_t *spr;
        uint32_t sw, sh;
        if (psxrom_map_render_object(m, t, (size_t)sp, palette, &spr, &sw, &sh) != PSXROM_OK) continue;
        for (uint32_t y = 0; y < sh; y++)
            for (uint32_t x = 0; x < sw; x++) {
                uint32_t dx = in->x + x, dy = in->y + y;
                const uint8_t *s = spr + ((size_t)y * sw + x) * 4;
                if (dx >= *width || dy >= *height || !s[3]) continue;
                uint8_t *d = *rgba + ((size_t)dy * *width + dx) * 4;
                for (int c = 0; c < 3; c++) d[c] = (uint8_t)((s[c] * s[3] + d[c] * (255 - s[3])) / 255);
                d[3] = (uint8_t)(s[3] + d[3] * (255 - s[3]) / 255);
            }
        free(spr);
    }
    return PSXROM_OK;
}
