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

/* ---- TMD 3D models ---- */

typedef struct {
    float pos[3];           /* PSX units with the object scale applied; +Y is down */
    float normal[3];        /* unit length; face normal when the file has none */
    float uv[2];            /* texel coordinates, 0..255 inside the texture page */
    uint8_t color[3];
} psxrom_tmd_vertex;

typedef struct {
    psxrom_tmd_vertex v[3];
    uint8_t textured;
    uint8_t double_sided;
    uint16_t tsb;           /* texture page, mix rate and colour mode */
    uint16_t clut;          /* palette position in VRAM */
} psxrom_tmd_tri;

typedef struct {
    psxrom_tmd_tri *tris;   /* polygons only; quads are split in two */
    size_t num_tris;
} psxrom_tmd_object;

typedef struct {
    psxrom_tmd_object *objects;
    size_t num_objects;
} psxrom_tmd;

/* Parses a standard TMD (id 0x41, offsets relative to the object table).
   Lines and sprites are skipped. Free the result with psxrom_tmd_free. */
psxrom_result psxrom_tmd_parse(const uint8_t *data, size_t size, psxrom_tmd *out);
void psxrom_tmd_free(psxrom_tmd *tmd);

/* ---- Digimon World models (.MMD) ---- */

typedef struct {
    uint8_t object;         /* TMD object drawn by this node, 255 for none */
    uint8_t parent;         /* parent node index, 255 for a root */
} psxrom_dw1_node;

typedef struct {
    char name[9];           /* file name without extension, e.g. "AGUM" */
    uint32_t bone_count;
    psxrom_dw1_node *nodes;
} psxrom_dw1_digimon;

/* Reads the Digimon name and skeleton tables from the US executable (SLUS_010.32).
   Entry i uses /CHDAT/MMD<i/30>/<name>.MMD and the i-th 0x4800-byte TIM of /CHDAT/ALLTIM.TIM. */
psxrom_result psxrom_dw1_load_digimon(const uint8_t *exe, size_t size,
                                      psxrom_dw1_digimon **out, size_t *count);
void psxrom_dw1_free_digimon(psxrom_dw1_digimon *d, size_t count);

/* An MMD is [tmd offset][animation offset] followed by a TMD and the animation data.
   With nodes, the skeleton is posed using the first animation's initial frame and the
   result is a single object; without nodes the TMD objects are returned unchanged. */
psxrom_result psxrom_mmd_parse(const uint8_t *data, size_t size,
                               const psxrom_dw1_node *nodes, size_t bone_count, psxrom_tmd *out);

/* Skeleton and animation data of one MMD, with the TMD objects left unmerged. */
typedef struct {
    float scale[3];
    float rot[3];           /* degrees, matrix = Rx * Ry * Rz */
    float pos[3];
} psxrom_mmd_pose;

typedef struct {
    uint32_t frame_count;
    size_t num_samples;     /* 0 for unused slots */
    float *times;           /* seconds, strictly increasing (20 frames per second) */
    psxrom_mmd_pose *poses; /* num_samples * bone_count local poses, linearly interpolated */
    float loop_start, loop_end; /* seconds; negative unless the animation loops endlessly */
} psxrom_mmd_anim;

typedef struct {
    psxrom_tmd tmd;
    psxrom_dw1_node *nodes;
    size_t bone_count;
    psxrom_mmd_pose *rest;  /* bone_count poses: the first animation's initial frame */
    psxrom_mmd_anim *anims;
    size_t num_anims;
} psxrom_mmd_model;

/* Texture-change and sound events in the animation data are skipped. */
psxrom_result psxrom_mmd_load(const uint8_t *data, size_t size,
                              const psxrom_dw1_node *nodes, size_t bone_count, psxrom_mmd_model *out);
void psxrom_mmd_free(psxrom_mmd_model *m);

/* ---- Digimon World maps (.MAP + .TFS) ----
 * A map is a pre-rendered background: a grid of 128x128 8bpp tiles (in the .TFS, with several
 * palettes, e.g. times of day) described by the .MAP, which also holds camera, lights, spawn points,
 * warps and sprite objects. Only the parts needed to render and describe the map are decoded. */

/* A sprite: a rectangle of one of the map's embedded images, drawn over the background. */
typedef struct {
    uint16_t uv_x, uv_y;        /* texel position within the texture page */
    uint16_t width, height;
    int16_t pos[3];
    uint16_t clut;              /* 0xFFFF = use the TFS palette; < 16 = 4bpp sub-palette; else a VRAM CLUT row - 468 */
    uint16_t transparency;      /* 4 = opaque (no semi-transparency) */
} psxrom_map_object;

/* A placed sprite with up to 8 animation steps. */
typedef struct {
    int16_t sprite[8];          /* object index per step, negative = unused */
    int16_t duration[8];
    uint16_t x, y;
    uint16_t flag;
} psxrom_map_instance;

typedef struct {
    int32_t camera_origin[3];
    int32_t camera_target[3];
    struct { int32_t pos[3]; uint32_t rgb[3]; } lights[3];
    uint32_t view_distance;
    uint32_t tiles_w, tiles_h;
    uint32_t *tiles;            /* tiles_w * tiles_h entries; 0xFFFFFFFF = empty cell */
    int16_t spawn[10][4];       /* x, y, z, rotation */
    uint16_t warp_map[10];      /* destination map index */
    uint16_t warp_spawn[10];    /* destination spawn point */
    uint16_t num_digimon;       /* roaming Digimon records (not decoded) */
    uint16_t num_objects;
    psxrom_map_object *objects;
    uint16_t num_instances;
    psxrom_map_instance *instances;
    size_t num_images;          /* embedded TIMs: 8bpp ones hold sprite pixels, 4bpp ones too */
    psxrom_tim *images;
    uint8_t collision[100][100];
    uint8_t *data;              /* copy of the file; images point into it */
} psxrom_map;

psxrom_result psxrom_map_parse(const uint8_t *data, size_t size, psxrom_map *out);
void psxrom_map_free(psxrom_map *m);

typedef struct {
    uint16_t width, height;     /* tile grid size recorded in the file */
    uint32_t num_palettes;
    uint16_t *palettes;         /* num_palettes * 256 PSX 15-bit colours */
    size_t num_tiles;
    uint8_t *pixels;            /* num_tiles * 128 * 128 palette indices */
} psxrom_tfs;

psxrom_result psxrom_tfs_parse(const uint8_t *data, size_t size, psxrom_tfs *out);
void psxrom_tfs_free(psxrom_tfs *t);

/* Renders one sprite to RGBA. With a 0xFFFF clut the TFS palette selects the colours.
 * PSXROM_ERR_NOT_FOUND if no embedded image holds the sprite's texels. The caller frees *rgba. */
psxrom_result psxrom_map_render_object(const psxrom_map *m, const psxrom_tfs *t, size_t object,
                                       uint32_t palette, uint8_t **rgba, uint32_t *width, uint32_t *height);

/* Renders the whole background with one palette to tiles_w*128 x tiles_h*128 RGBA (empty cells are
 * transparent). The caller frees *rgba. */
psxrom_result psxrom_map_render(const psxrom_map *m, const psxrom_tfs *t, uint32_t palette,
                                uint8_t **rgba, uint32_t *width, uint32_t *height);

/* Like psxrom_map_render, then draws every instance's sprite for the given game frame (30 per second
 * in the data's units): each instance steps through its sprites for their durations (negative sprite
 * ids hide it, -1 ends the list) and loops. */
psxrom_result psxrom_map_render_scene(const psxrom_map *m, const psxrom_tfs *t, uint32_t palette, uint32_t frame,
                                      uint8_t **rgba, uint32_t *width, uint32_t *height);

#ifdef __cplusplus
}
#endif

#endif
