#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "psxrom/psxrom.h"

#define NO_NODE 255

/* US executable (SLUS_010.32), loaded at 0x80090800: file offset = RAM address - 0x80090000. */
#define DW1_RAM_BASE 0x80090000u
#define DW1_NAMES 0xA3B44u
#define DW1_PARA 0x9CEB4u
#define DW1_SKEL 0x8CE60u
#define DW1_COUNT 180u
#define DW1_NAME_SIZE 8u
#define DW1_PARA_SIZE 52u
#define DW1_PARA_BONES 20u

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void psxrom_dw1_free_digimon(psxrom_dw1_digimon *d, size_t count)
{
    if (!d) return;
    for (size_t i = 0; i < count; i++) free(d[i].nodes);
    free(d);
}

psxrom_result psxrom_dw1_load_digimon(const uint8_t *exe, size_t size,
                                      psxrom_dw1_digimon **out, size_t *count)
{
    *out = NULL;
    *count = 0;
    if (size < 8 || memcmp(exe, "PS-X EXE", 8) != 0) return PSXROM_ERR_FORMAT;
    if (DW1_NAMES + DW1_COUNT * DW1_NAME_SIZE > size || DW1_PARA + DW1_COUNT * DW1_PARA_SIZE > size ||
        DW1_SKEL + DW1_COUNT * 4 > size)
        return PSXROM_ERR_FORMAT;
    if (memcmp(exe + DW1_NAMES, "BOYS", 5) != 0) return PSXROM_ERR_FORMAT;

    psxrom_dw1_digimon *d = calloc(DW1_COUNT, sizeof *d);
    if (!d) return PSXROM_ERR_NOMEM;

    for (size_t i = 0; i < DW1_COUNT; i++) {
        memcpy(d[i].name, exe + DW1_NAMES + i * DW1_NAME_SIZE, DW1_NAME_SIZE);
        d[i].name[DW1_NAME_SIZE] = 0;

        uint32_t bones = rd32(exe + DW1_PARA + i * DW1_PARA_SIZE + DW1_PARA_BONES);
        uint32_t ptr = rd32(exe + DW1_SKEL + i * 4);
        if (bones > 64) goto bad;
        if (bones) {
            /* unused slots have no skeleton and a null pointer */
            if (ptr < DW1_RAM_BASE) goto bad;
            size_t off = ptr - DW1_RAM_BASE;
            if (off > size || (size_t)bones * 2 > size - off) goto bad;
            d[i].bone_count = bones;
            d[i].nodes = malloc(bones * sizeof *d[i].nodes);
            if (!d[i].nodes) { psxrom_dw1_free_digimon(d, DW1_COUNT); return PSXROM_ERR_NOMEM; }
            for (uint32_t j = 0; j < bones; j++) {
                d[i].nodes[j].object = exe[off + j * 2];
                d[i].nodes[j].parent = exe[off + j * 2 + 1];
            }
        }
    }
    *out = d;
    *count = DW1_COUNT;
    return PSXROM_OK;

bad:
    psxrom_dw1_free_digimon(d, DW1_COUNT);
    return PSXROM_ERR_FORMAT;
}

/* Affine transform: 3x3 matrix (row-major) followed by a translation. */
typedef struct { float r[9]; float t[3]; } xform;

static xform xf_identity(void)
{
    xform x = {{1, 0, 0, 0, 1, 0, 0, 0, 1}, {0, 0, 0}};
    return x;
}

static xform xf_mul(const xform *a, const xform *b)
{
    xform o;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            o.r[i * 3 + j] = a->r[i * 3] * b->r[j] + a->r[i * 3 + 1] * b->r[3 + j] + a->r[i * 3 + 2] * b->r[6 + j];
        o.t[i] = a->r[i * 3] * b->t[0] + a->r[i * 3 + 1] * b->t[1] + a->r[i * 3 + 2] * b->t[2] + a->t[i];
    }
    return o;
}

/* T * Rx * Ry * Rz * S, angles in degrees. */
static xform xf_local(const float rot[3], const float pos[3], const float scale[3])
{
    const float k = 3.14159265358979f / 180.0f;
    float ca = cosf(rot[0] * k), sa = sinf(rot[0] * k);
    float cb = cosf(rot[1] * k), sb = sinf(rot[1] * k);
    float cc = cosf(rot[2] * k), sc = sinf(rot[2] * k);
    xform x = {{cb * cc, -cb * sc, sb,
                ca * sc + sa * sb * cc, ca * cc - sa * sb * sc, -sa * cb,
                sa * sc - ca * sb * cc, sa * cc + ca * sb * sc, ca * cb},
               {pos[0], pos[1], pos[2]}};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) x.r[i * 3 + j] *= scale[j];
    return x;
}

static void xf_point(const xform *x, const float in[3], float out[3])
{
    for (int i = 0; i < 3; i++)
        out[i] = x->r[i * 3] * in[0] + x->r[i * 3 + 1] * in[1] + x->r[i * 3 + 2] * in[2] + x->t[i];
}

static void xf_normal(const xform *x, const float in[3], float out[3])
{
    float len = 0;
    for (int i = 0; i < 3; i++) {
        out[i] = x->r[i * 3] * in[0] + x->r[i * 3 + 1] * in[1] + x->r[i * 3 + 2] * in[2];
        len += out[i] * out[i];
    }
    len = sqrtf(len);
    if (len > 0) for (int i = 0; i < 3; i++) out[i] /= len;
}

/* Reads the initial transform of every node from the first non-empty animation. */
static psxrom_result read_rest_pose(const uint8_t *anim, size_t size, size_t bones, xform *local)
{
    if (size < 4) return PSXROM_ERR_FORMAT;
    uint32_t table = rd32(anim);
    if (table < 4 || table > size || table % 4) return PSXROM_ERR_FORMAT;

    for (size_t i = 0; i < table / 4; i++) {
        uint32_t off = rd32(anim + i * 4);
        if (!off) continue;
        if (off > size || size - off < 2) return PSXROM_ERR_FORMAT;
        const uint8_t *p = anim + off;
        int has_scale = (rd16(p) & 0x8000) != 0;
        size_t stride = has_scale ? 18 : 12;
        if (bones > 1 && (size - off - 2) / stride < bones - 1) return PSXROM_ERR_FORMAT;
        p += 2;

        local[0] = xf_identity();
        for (size_t b = 1; b < bones; b++, p += stride) {
            float scale[3] = {1, 1, 1}, rot[3], pos[3];
            const uint8_t *q = p;
            if (has_scale) {
                for (int k = 0; k < 3; k++) scale[k] = (float)rds16(q + k * 2) / 4096.0f;
                q += 6;
            }
            for (int k = 0; k < 3; k++) rot[k] = (float)rds16(q + k * 2) * 360.0f / 4096.0f;
            for (int k = 0; k < 3; k++) pos[k] = (float)rds16(q + 6 + k * 2);
            local[b] = xf_local(rot, pos, scale);
        }
        return PSXROM_OK;
    }
    return PSXROM_ERR_NOT_FOUND;
}

static psxrom_result assemble(const psxrom_tmd *src, const psxrom_dw1_node *nodes, size_t bones,
                              const xform *local, psxrom_tmd *out)
{
    xform *world = malloc(bones * sizeof *world);
    uint8_t *done = calloc(bones, 1);
    if (!world || !done) { free(world); free(done); return PSXROM_ERR_NOMEM; }

    /* Parents are not guaranteed to precede their children, so resolve repeatedly. */
    size_t resolved = 0;
    for (size_t pass = 0; pass <= bones && resolved < bones; pass++) {
        for (size_t i = 0; i < bones; i++) {
            if (done[i]) continue;
            if (nodes[i].parent == NO_NODE) world[i] = local[i];
            else if (nodes[i].parent < bones && done[nodes[i].parent]) world[i] = xf_mul(&world[nodes[i].parent], &local[i]);
            else continue;
            done[i] = 1;
            resolved++;
        }
    }
    free(done);
    if (resolved != bones) { free(world); return PSXROM_ERR_FORMAT; }

    size_t total = 0;
    for (size_t i = 0; i < bones; i++)
        if (nodes[i].object < src->num_objects) total += src->objects[nodes[i].object].num_tris;

    psxrom_tmd_tri *tris = malloc((total ? total : 1) * sizeof *tris);
    if (!tris) { free(world); return PSXROM_ERR_NOMEM; }
    size_t n = 0;
    for (size_t i = 0; i < bones; i++) {
        if (nodes[i].object >= src->num_objects) continue;
        const psxrom_tmd_object *ob = &src->objects[nodes[i].object];
        for (size_t t = 0; t < ob->num_tris; t++, n++) {
            tris[n] = ob->tris[t];
            for (int v = 0; v < 3; v++) {
                float p[3], nm[3];
                xf_point(&world[i], ob->tris[t].v[v].pos, p);
                xf_normal(&world[i], ob->tris[t].v[v].normal, nm);
                memcpy(tris[n].v[v].pos, p, sizeof p);
                memcpy(tris[n].v[v].normal, nm, sizeof nm);
            }
        }
    }
    free(world);

    out->objects = calloc(1, sizeof *out->objects);
    if (!out->objects) { free(tris); return PSXROM_ERR_NOMEM; }
    out->num_objects = 1;
    out->objects[0].tris = tris;
    out->objects[0].num_tris = n;
    return PSXROM_OK;
}

psxrom_result psxrom_mmd_parse(const uint8_t *data, size_t size,
                               const psxrom_dw1_node *nodes, size_t bone_count, psxrom_tmd *out)
{
    memset(out, 0, sizeof *out);
    if (size < 8) return PSXROM_ERR_FORMAT;
    uint32_t tmd_off = rd32(data), anim_off = rd32(data + 4);
    if (tmd_off < 8 || tmd_off >= size || anim_off > size) return PSXROM_ERR_FORMAT;

    psxrom_tmd tmd;
    psxrom_result r = psxrom_tmd_parse(data + tmd_off, size - tmd_off, &tmd);
    if (r != PSXROM_OK || !nodes || !bone_count) {
        if (r == PSXROM_OK) *out = tmd;
        return r;
    }

    xform *local = malloc(bone_count * sizeof *local);
    if (!local) { psxrom_tmd_free(&tmd); return PSXROM_ERR_NOMEM; }
    r = read_rest_pose(data + anim_off, size - anim_off, bone_count, local);
    if (r == PSXROM_OK) r = assemble(&tmd, nodes, bone_count, local, out);
    free(local);
    psxrom_tmd_free(&tmd);
    return r;
}
