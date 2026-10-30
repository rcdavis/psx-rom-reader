#include "psxrom/psxrom.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SUBMODE_AUDIO 0x04
#define STR_MAGIC 0x80010160u
#define STR_HEADER_SIZE 32
#define MAX_FRAME_SIZE (16u * 1024 * 1024)

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- STR demuxing ---- */

struct psxrom_str {
    psxrom_disc *disc;
    uint32_t lba;
    uint32_t end;
    uint8_t *buf;
    size_t cap;
};

psxrom_result psxrom_str_open(psxrom_disc *d, uint32_t lba, uint32_t count, psxrom_str **out)
{
    psxrom_str *s = calloc(1, sizeof *s);
    if (!s) return PSXROM_ERR_NOMEM;
    s->disc = d;
    s->lba = lba;
    s->end = lba + count;
    *out = s;
    return PSXROM_OK;
}

void psxrom_str_close(psxrom_str *s)
{
    if (!s) return;
    free(s->buf);
    free(s);
}

psxrom_result psxrom_str_next_frame(psxrom_str *s, psxrom_str_frame *frame)
{
    psxrom_raw_sector sec;
    size_t len = 0;
    uint32_t cur_frame = 0, expected = 0, demux = 0, first_lba = 0;
    uint16_t width = 0, height = 0;
    int have = 0;

    while (s->lba < s->end && s->lba < psxrom_disc_sector_count(s->disc)) {
        uint32_t lba = s->lba++;
        psxrom_result r = psxrom_disc_read_raw(s->disc, lba, &sec);
        if (r != PSXROM_OK) return r;
        if ((sec.submode & SUBMODE_AUDIO) || sec.data_size < STR_HEADER_SIZE) continue;
        if (rd32(sec.data) != STR_MAGIC) continue;

        uint32_t chunk = rd16(sec.data + 4);
        uint32_t chunks = rd16(sec.data + 6);
        uint32_t number = rd32(sec.data + 8);
        if (chunks == 0 || chunk >= chunks) continue;

        if (chunk == 0) {
            have = 1;
            cur_frame = number;
            expected = 0;
            len = 0;
            demux = rd32(sec.data + 12);
            width = rd16(sec.data + 16);
            height = rd16(sec.data + 18);
            first_lba = lba;
        }
        if (!have || number != cur_frame || chunk != expected) {
            have = 0;
            continue;
        }

        size_t payload = (sec.data_size == 2048 ? 2048 : 2304) - STR_HEADER_SIZE;
        if (len + payload > MAX_FRAME_SIZE) { have = 0; continue; }
        if (len + payload > s->cap) {
            size_t ncap = s->cap ? s->cap * 2 : 1 << 16;
            while (ncap < len + payload) ncap *= 2;
            uint8_t *p = realloc(s->buf, ncap);
            if (!p) return PSXROM_ERR_NOMEM;
            s->buf = p;
            s->cap = ncap;
        }
        size_t avail = sec.data_size - STR_HEADER_SIZE;
        memcpy(s->buf + len, sec.data + STR_HEADER_SIZE, avail < payload ? avail : payload);
        if (avail < payload) memset(s->buf + len + avail, 0, payload - avail);
        len += payload;
        expected++;

        if (expected == chunks) {
            frame->number = cur_frame;
            frame->width = width;
            frame->height = height;
            frame->first_lba = first_lba;
            frame->data = s->buf;
            frame->size = (demux && demux < len) ? demux : len;
            return PSXROM_OK;
        }
    }
    return PSXROM_ERR_NOT_FOUND;
}

/* ---- MDEC decoding (MPEG-1 style VLC, bitstream versions 2 and 3) ---- */

static const uint16_t vlc_table[113][2] = {
    {0x3, 2}, {0x4, 4}, {0x5, 5}, {0x6, 7}, {0x26, 8}, {0x21, 8}, {0xa, 10}, {0x1d, 12},
    {0x18, 12}, {0x13, 12}, {0x10, 12}, {0x1a, 13}, {0x19, 13}, {0x18, 13}, {0x17, 13}, {0x1f, 14},
    {0x1e, 14}, {0x1d, 14}, {0x1c, 14}, {0x1b, 14}, {0x1a, 14}, {0x19, 14}, {0x18, 14}, {0x17, 14},
    {0x16, 14}, {0x15, 14}, {0x14, 14}, {0x13, 14}, {0x12, 14}, {0x11, 14}, {0x10, 14}, {0x18, 15},
    {0x17, 15}, {0x16, 15}, {0x15, 15}, {0x14, 15}, {0x13, 15}, {0x12, 15}, {0x11, 15}, {0x10, 15},
    {0x3, 3}, {0x6, 6}, {0x25, 8}, {0xc, 10}, {0x1b, 12}, {0x16, 13}, {0x15, 13}, {0x1f, 15},
    {0x1e, 15}, {0x1d, 15}, {0x1c, 15}, {0x1b, 15}, {0x1a, 15}, {0x19, 15}, {0x13, 16}, {0x12, 16},
    {0x11, 16}, {0x10, 16}, {0x5, 4}, {0x4, 7}, {0xb, 10}, {0x14, 12}, {0x14, 13}, {0x7, 5},
    {0x24, 8}, {0x1c, 12}, {0x13, 13}, {0x6, 5}, {0xf, 10}, {0x12, 12}, {0x7, 6}, {0x9, 10},
    {0x12, 13}, {0x5, 6}, {0x1e, 12}, {0x14, 16}, {0x4, 6}, {0x15, 12}, {0x7, 7}, {0x11, 12},
    {0x5, 7}, {0x11, 13}, {0x27, 8}, {0x10, 13}, {0x23, 8}, {0x1a, 16}, {0x22, 8}, {0x19, 16},
    {0x20, 8}, {0x18, 16}, {0xe, 10}, {0x17, 16}, {0xd, 10}, {0x16, 16}, {0x8, 10}, {0x15, 16},
    {0x1f, 12}, {0x1a, 12}, {0x19, 12}, {0x17, 12}, {0x16, 12}, {0x1f, 13}, {0x1e, 13}, {0x1d, 13},
    {0x1c, 13}, {0x1b, 13}, {0x1f, 16}, {0x1e, 16}, {0x1d, 16}, {0x1c, 16}, {0x1b, 16},
    {0x1, 6}, /* escape */
    {0x2, 2}, /* end of block */
};
#define VLC_ESCAPE 111
#define VLC_EOB 112

static const int8_t vlc_level[111] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 1, 2, 3, 4, 5, 6, 7, 8,
    9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 1, 2, 3, 4, 5, 1,
    2, 3, 4, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 1, 2,
    1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static const int8_t vlc_run[111] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5,
    5, 6, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12,
    13, 13, 14, 14, 15, 15, 16, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31,
};

static const uint16_t dc_lum_code[12] = {0x4, 0x0, 0x1, 0x5, 0x6, 0xe, 0x1e, 0x3e, 0x7e, 0xfe, 0x1fe, 0x1ff};
static const uint8_t dc_lum_bits[12] = {3, 2, 2, 3, 3, 4, 5, 6, 7, 8, 9, 9};
static const uint16_t dc_chroma_code[12] = {0x0, 0x1, 0x2, 0x6, 0xe, 0x1e, 0x3e, 0x7e, 0xfe, 0x1fe, 0x3fe, 0x3ff};
static const uint8_t dc_chroma_bits[12] = {2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10};

static const uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static const uint8_t quant_matrix[64] = {
    8, 16, 19, 22, 26, 27, 29, 34,
    16, 16, 22, 24, 27, 29, 34, 37,
    19, 22, 26, 27, 29, 34, 34, 38,
    22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48,
    26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69,
    27, 29, 35, 38, 46, 56, 69, 83,
};

/* Bits are read MSB first from little-endian 16-bit words. */
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos; /* in bits */
} bits;

static uint32_t word_at(const bits *b, size_t w)
{
    size_t o = w * 2;
    if (o + 1 < b->size) return rd16(b->data + o);
    if (o < b->size) return b->data[o];
    return 0;
}

static uint32_t peek(const bits *b, unsigned n)
{
    size_t w = b->pos >> 4;
    uint64_t v = ((uint64_t)word_at(b, w) << 32) | ((uint64_t)word_at(b, w + 1) << 16) | word_at(b, w + 2);
    unsigned shift = 48 - (unsigned)(b->pos & 15) - n;
    return (uint32_t)((v >> shift) & ((1u << n) - 1));
}

static uint32_t get(bits *b, unsigned n)
{
    uint32_t v = peek(b, n);
    b->pos += n;
    return v;
}

static int get_signed(bits *b, unsigned n)
{
    int v = (int)get(b, n);
    return (v & (1 << (n - 1))) ? v - (1 << n) : v;
}

static int decode_dc_diff(bits *b, int chroma)
{
    const uint16_t *codes = chroma ? dc_chroma_code : dc_lum_code;
    const uint8_t *lens = chroma ? dc_chroma_bits : dc_lum_bits;
    for (unsigned size = 0; size < 12; size++) {
        if (peek(b, lens[size]) != codes[size]) continue;
        b->pos += lens[size];
        if (size == 0) return 0;
        int v = (int)get(b, size);
        return (v & (1 << (size - 1))) ? v : v - ((1 << size) - 1);
    }
    return INT32_MIN;
}

static int decode_block(bits *b, int version, int n, int *last_dc, int qscale_default,
                        int *block)
{
    int qscale = qscale_default;
    memset(block, 0, 64 * sizeof *block);

    if (version <= 2) {
        uint32_t head = get(b, 16);
        qscale = (int)(head >> 10);
        int dc = (int)(head & 0x3FF);
        if (dc & 0x200) dc -= 0x400;
        block[0] = dc * 2 + 1024;
    } else {
        int comp = n <= 3 ? 0 : n - 3;
        int diff = decode_dc_diff(b, comp != 0);
        if (diff == INT32_MIN) return 0;
        last_dc[comp] += diff;
        block[0] = last_dc[comp] * 8;
    }

    int i = 0;
    for (;;) {
        int idx = -1;
        for (int k = 0; k < 113; k++) {
            if (peek(b, 16) >> (16 - vlc_table[k][1]) == vlc_table[k][0]) { idx = k; break; }
        }
        if (idx < 0) return 0;
        b->pos += vlc_table[idx][1];
        if (idx == VLC_EOB) break;

        int level, j;
        if (idx != VLC_ESCAPE) {
            i += vlc_run[idx] + 1;
            if (i > 63) return 0;
            j = zigzag[i];
            level = (vlc_level[idx] * qscale * quant_matrix[j]) >> 3;
            if (get(b, 1)) level = -level;
        } else {
            i += (int)get(b, 6) + 1;
            level = get_signed(b, 10);
            if (i > 63) return 0;
            j = zigzag[i];
            int mag = level < 0 ? -level : level;
            mag = (mag * qscale * quant_matrix[j]) >> 3;
            mag = (mag - 1) | 1;
            level = level < 0 ? -mag : mag;
        }
        block[j] = level;
        if (b->pos > b->size * 8 + 32) return 0;
    }
    return b->pos <= b->size * 8;
}

typedef struct { float c[8][8]; } idct_table; /* [u][x], including the C(u)/2 scale */

static void idct_init(idct_table *t)
{
    for (int u = 0; u < 8; u++)
        for (int x = 0; x < 8; x++)
            t->c[u][x] = (float)((u == 0 ? 0.35355339059327373 : 0.5) *
                                     cos((2 * x + 1) * u * 3.14159265358979323846 / 16.0));
}

static uint8_t clamp8(float v)
{
    int i = (int)floorf(v + 0.5f);
    return (uint8_t)(i < 0 ? 0 : i > 255 ? 255 : i);
}

static void idct_put(const idct_table *t, const int *coef, uint8_t *dst, size_t stride)
{
    float tmp[64];
    for (int y = 0; y < 8; y++) { /* rows */
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += t->c[u][x] * (float)coef[y * 8 + u];
            tmp[y * 8 + x] = s;
        }
    }
    for (int x = 0; x < 8; x++) { /* columns */
        for (int y = 0; y < 8; y++) {
            float s = 0;
            for (int v = 0; v < 8; v++) s += t->c[v][y] * tmp[v * 8 + x];
            dst[(size_t)y * stride + x] = clamp8(s);
        }
    }
}

psxrom_result psxrom_mdec_decode(const uint8_t *data, size_t size,
                                 uint16_t width, uint16_t height, uint8_t **rgba)
{
    if (size < 8 || !width || !height || rd16(data + 2) != 0x3800) return PSXROM_ERR_FORMAT;
    int qscale = rd16(data + 4);
    int version = rd16(data + 6);
    if (version != 2 && version != 3) return PSXROM_ERR_FORMAT;

    unsigned mbw = (width + 15u) / 16, mbh = (height + 15u) / 16;
    size_t yw = (size_t)mbw * 16, yh = (size_t)mbh * 16, cw = mbw * 8, ch = mbh * 8;
    uint8_t *yp = malloc(yw * yh + cw * ch * 2);
    uint8_t *out = malloc((size_t)width * height * 4);
    if (!yp || !out) { free(yp); free(out); return PSXROM_ERR_NOMEM; }
    uint8_t *cbp = yp + yw * yh, *crp = cbp + cw * ch;

    idct_table cos_table;
    idct_init(&cos_table);
    bits b = {data, size, 64};
    int last_dc[3] = {128, 128, 128};
    int blocks[6][64];
    static const int order[6] = {5, 4, 0, 1, 2, 3};
    psxrom_result res = PSXROM_OK;

    for (unsigned mx = 0; mx < mbw && res == PSXROM_OK; mx++) {
        for (unsigned my = 0; my < mbh; my++) {
            for (int k = 0; k < 6; k++) {
                if (!decode_block(&b, version, order[k], last_dc, qscale, blocks[order[k]])) {
                    res = PSXROM_ERR_FORMAT;
                    break;
                }
            }
            if (res != PSXROM_OK) break;
            uint8_t *dy = yp + (size_t)my * 16 * yw + mx * 16;
            idct_put(&cos_table, blocks[0], dy, yw);
            idct_put(&cos_table, blocks[1], dy + 8, yw);
            idct_put(&cos_table, blocks[2], dy + 8 * yw, yw);
            idct_put(&cos_table, blocks[3], dy + 8 * yw + 8, yw);
            idct_put(&cos_table, blocks[4], cbp + (size_t)my * 8 * cw + mx * 8, cw);
            idct_put(&cos_table, blocks[5], crp + (size_t)my * 8 * cw + mx * 8, cw);
        }
    }
    if (res != PSXROM_OK) { free(yp); free(out); return res; }

    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            float Y = yp[y * yw + x];
            float cb = (float)cbp[(y / 2) * cw + x / 2] - 128.0f;
            float cr = (float)crp[(y / 2) * cw + x / 2] - 128.0f;
            uint8_t *p = out + ((size_t)y * width + x) * 4;
            p[0] = clamp8(Y + 1.402f * cr);
            p[1] = clamp8(Y - 0.344136f * cb - 0.714136f * cr);
            p[2] = clamp8(Y + 1.772f * cb);
            p[3] = 255;
        }
    }
    free(yp);
    *rgba = out;
    return PSXROM_OK;
}
