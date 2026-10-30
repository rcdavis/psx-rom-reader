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

/* ---- Raw (Mode 2) sector access ---- */

typedef struct {
    uint8_t file;       /* XA subheader file number */
    uint8_t channel;    /* XA subheader channel number */
    uint8_t submode;    /* 0x04 audio, 0x08 data, 0x02 video, 0x20 form 2 */
    uint8_t coding;     /* XA audio coding info */
    uint32_t data_size; /* 2048 (form 1) or 2324 (form 2) */
    uint8_t data[2324];
} psxrom_raw_sector;

/* 1 when the image keeps the Mode 2 subheaders (raw .bin/.img), required for XA audio. */
int psxrom_disc_has_raw(const psxrom_disc *d);
/* On cooked 2048-byte images a data sector (submode 0x08) is synthesized. */
psxrom_result psxrom_disc_read_raw(psxrom_disc *d, uint32_t lba, psxrom_raw_sector *out);

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

/* ---- CD-XA ADPCM audio ---- */

typedef struct {
    uint8_t file;
    uint8_t channel;
    uint8_t coding;
    int stereo;
    uint32_t sample_rate;   /* 37800 or 18900 */
    uint32_t bits;          /* 4 or 8 */
    uint32_t sectors;
    uint32_t first_lba;
} psxrom_xa_stream;

typedef struct {
    int16_t *samples;       /* interleaved */
    size_t frames;
    uint32_t channels;
    uint32_t sample_rate;
} psxrom_xa_pcm;

/* Lists the XA audio file/channel streams within [lba, lba + count). Free *streams with free(). */
psxrom_result psxrom_xa_scan(psxrom_disc *d, uint32_t lba, uint32_t count,
                             psxrom_xa_stream **streams, size_t *num_streams);
/* Decodes one file/channel within [lba, lba + count). Release with psxrom_xa_pcm_free(). */
psxrom_result psxrom_xa_decode(psxrom_disc *d, uint32_t lba, uint32_t count,
                               uint8_t file, uint8_t channel, psxrom_xa_pcm *out);
void psxrom_xa_pcm_free(psxrom_xa_pcm *pcm);

/* ---- STR video ---- */

typedef struct psxrom_str psxrom_str;

typedef struct {
    uint32_t number;        /* frame number stored on disc (usually starts at 1) */
    uint16_t width;
    uint16_t height;
    uint32_t first_lba;
    const uint8_t *data;    /* MDEC bitstream; valid until the next call or close */
    size_t size;
} psxrom_str_frame;

/* Reads frames from the sectors [lba, lba + count), typically a .STR file. */
psxrom_result psxrom_str_open(psxrom_disc *d, uint32_t lba, uint32_t count, psxrom_str **out);
void psxrom_str_close(psxrom_str *s);
/* Returns PSXROM_ERR_NOT_FOUND once no more complete frames remain. */
psxrom_result psxrom_str_next_frame(psxrom_str *s, psxrom_str_frame *frame);

/* Decodes a version 2 or 3 MDEC frame to RGBA8 (width*height*4 bytes, malloc'd). */
psxrom_result psxrom_mdec_decode(const uint8_t *data, size_t size,
                                 uint16_t width, uint16_t height, uint8_t **rgba);

#ifdef __cplusplus
}
#endif

#endif
