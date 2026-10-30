#ifndef PSXROM_H
#define PSXROM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PSXROM_OK = 0,
    PSXROM_ERR_IO,
    PSXROM_ERR_FORMAT,
    PSXROM_ERR_NOT_FOUND,
    PSXROM_ERR_NOMEM,
    PSXROM_ERR_RANGE
} psxrom_result;

const char *psxrom_strerror(psxrom_result r);

/* ---- Disc image (.iso 2048, .bin/.img 2352, 2336 sector layouts) ---- */

typedef struct psxrom_disc psxrom_disc;

psxrom_result psxrom_disc_open(const char *path, psxrom_disc **out);
void psxrom_disc_close(psxrom_disc *d);
/* Reads the 2048-byte user data of one logical sector. */
psxrom_result psxrom_disc_read_sector(psxrom_disc *d, uint32_t lba, uint8_t out[2048]);
uint32_t psxrom_disc_sector_count(const psxrom_disc *d);

/* ---- ISO9660 filesystem ---- */

typedef struct {
    char name[256];     /* no ";1" version suffix */
    char path[1024];    /* full path, '/' separated, leading '/' */
    uint32_t lba;
    uint32_t size;
    int is_dir;
} psxrom_entry;

/* Return non-zero to stop iteration. */
typedef int (*psxrom_walk_cb)(const psxrom_entry *e, void *user);

psxrom_result psxrom_walk(psxrom_disc *d, psxrom_walk_cb cb, void *user);
psxrom_result psxrom_find(psxrom_disc *d, const char *path, psxrom_entry *out);
/* Allocates *data (free with free()). */
psxrom_result psxrom_read_file(psxrom_disc *d, const psxrom_entry *e,
                               uint8_t **data, size_t *size);
/* Copies the volume label (trimmed) into out. */
psxrom_result psxrom_volume_label(psxrom_disc *d, char *out, size_t out_size);

/* ---- TIM textures ---- */

typedef struct {
    uint32_t bpp;            /* 4, 8, 16 or 24 */
    int has_clut;
    uint16_t clut_x, clut_y, clut_w, clut_h;
    uint16_t img_x, img_y;
    uint16_t width;          /* in pixels (not VRAM words) */
    uint16_t height;
    uint32_t num_cluts;      /* clut_h palettes of clut_w entries */
    const uint8_t *clut;     /* points into the input buffer */
    const uint8_t *pixels;
    size_t pixels_size;
} psxrom_tim;

psxrom_result psxrom_tim_parse(const uint8_t *data, size_t size, psxrom_tim *out);
/* Converts to RGBA8 (width*height*4 bytes, malloc'd). Transparent when color == 0
 * for paletted/16-bit images, or STP-aware alpha is ignored. */
psxrom_result psxrom_tim_to_rgba(const psxrom_tim *t, uint32_t clut_index, uint8_t **rgba);
/* Heuristic: does the buffer look like a TIM? */
int psxrom_tim_probe(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
