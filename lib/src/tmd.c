#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "psxrom/psxrom.h"

#define TMD_ID 0x41
#define HDR_SIZE 12
#define OBJ_SIZE 28

#define FLAG_LGT 1
#define FLAG_GRD 4
#define MODE_TGE 1
#define MODE_TME 4
#define MODE_QUAD 8
#define MODE_IIP 16

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void face_normal(psxrom_tmd_tri *t)
{
    float a[3], b[3], n[3];
    for (int i = 0; i < 3; i++) {
        a[i] = t->v[1].pos[i] - t->v[0].pos[i];
        b[i] = t->v[2].pos[i] - t->v[0].pos[i];
    }
    n[0] = a[1] * b[2] - a[2] * b[1];
    n[1] = a[2] * b[0] - a[0] * b[2];
    n[2] = a[0] * b[1] - a[1] * b[0];
    float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 0) for (int i = 0; i < 3; i++) n[i] /= len;
    for (int v = 0; v < 3; v++) memcpy(t->v[v].normal, n, sizeof n);
}

static psxrom_result parse_object(const uint8_t *base, size_t avail, const uint8_t *ent,
                                  psxrom_tmd_object *obj)
{
    uint32_t vert_top = rd32(ent), n_vert = rd32(ent + 4);
    uint32_t norm_top = rd32(ent + 8), n_norm = rd32(ent + 12);
    uint32_t prim_top = rd32(ent + 16), n_prim = rd32(ent + 20);
    int32_t scale = (int32_t)rd32(ent + 24);

    if (vert_top > avail || (uint64_t)n_vert * 8 > avail - vert_top) return PSXROM_ERR_FORMAT;
    if (norm_top > avail || (uint64_t)n_norm * 8 > avail - norm_top) return PSXROM_ERR_FORMAT;
    if (prim_top > avail) return PSXROM_ERR_FORMAT;

    float mul = 1.0f;
    if (scale > 0 && scale < 16) mul = (float)(1 << scale);
    else if (scale < 0 && scale > -16) mul = 1.0f / (float)(1 << -scale);

    size_t cap = 16;
    obj->tris = malloc(cap * sizeof *obj->tris);
    if (!obj->tris) return PSXROM_ERR_NOMEM;
    obj->num_tris = 0;

    size_t pos = prim_top;
    for (uint32_t p = 0; p < n_prim; p++) {
        if (avail - pos < 4) goto bad;
        unsigned ilen = base[pos + 1], flag = base[pos + 2], mode = base[pos + 3];
        size_t body = pos + 4, total = 4 + (size_t)ilen * 4;
        if (total > avail - pos) goto bad;
        size_t next = pos + total;

        if ((mode >> 5) != 1) { pos = next; continue; }

        int quad = (mode & MODE_QUAD) != 0, tex = (mode & MODE_TME) != 0;
        int gouraud = (mode & MODE_IIP) != 0;
        unsigned vc = quad ? 4 : 3;
        unsigned nc = (flag & FLAG_LGT) || (mode & MODE_TGE) ? 0 : gouraud ? vc : 1;
        unsigned cc = (flag & FLAG_GRD) ? vc : (flag & FLAG_LGT) ? (gouraud ? vc : 1) : !tex ? 1 : 0;
        size_t need = (tex ? vc * 4 : 0) + cc * 4 + vc * 2 + nc * 2;
        if (need > total - 4) goto bad;

        const uint8_t *q = base + body;
        uint8_t u[4] = {0}, v[4] = {0};
        uint16_t clut = 0, tsb = 0;
        if (tex) {
            u[0] = q[0]; v[0] = q[1]; clut = rd16(q + 2);
            u[1] = q[4]; v[1] = q[5]; tsb = rd16(q + 6);
            u[2] = q[8]; v[2] = q[9];
            if (quad) { u[3] = q[12]; v[3] = q[13]; }
            q += vc * 4;
        }
        uint8_t col[4][3] = {{128, 128, 128}, {128, 128, 128}, {128, 128, 128}, {128, 128, 128}};
        for (unsigned i = 0; i < cc; i++, q += 4) memcpy(col[i], q, 3);
        if (cc == 1) for (unsigned i = 1; i < 4; i++) memcpy(col[i], col[0], 3);

        psxrom_tmd_vertex pv[4];
        int has_normal = nc > 0;
        for (unsigned i = 0; i < vc; i++) {
            unsigned ni = 0;
            if (i < nc) { ni = rd16(q); q += 2; }
            unsigned vi = rd16(q); q += 2;
            if (vi >= n_vert || (i < nc && ni >= n_norm)) goto bad;

            const uint8_t *vp = base + vert_top + (size_t)vi * 8;
            for (int k = 0; k < 3; k++) pv[i].pos[k] = (float)rds16(vp + k * 2) * mul;
            memcpy(pv[i].color, col[i], 3);
            pv[i].uv[0] = u[i]; pv[i].uv[1] = v[i];
            memset(pv[i].normal, 0, sizeof pv[i].normal);
            if (i < nc) {
                const uint8_t *np = base + norm_top + (size_t)ni * 8;
                for (int k = 0; k < 3; k++) pv[i].normal[k] = (float)rds16(np + k * 2) / 4096.0f;
            }
        }
        /* flat polygons carry a single normal that applies to every vertex */
        if (nc == 1) for (unsigned i = 1; i < vc; i++) memcpy(pv[i].normal, pv[0].normal, sizeof pv[i].normal);

        static const unsigned idx[2][3] = {{0, 1, 2}, {2, 1, 3}};
        for (unsigned t = 0; t < (quad ? 2u : 1u); t++) {
            if (obj->num_tris == cap) {
                cap *= 2;
                psxrom_tmd_tri *g = realloc(obj->tris, cap * sizeof *g);
                if (!g) { free(obj->tris); obj->tris = NULL; return PSXROM_ERR_NOMEM; }
                obj->tris = g;
            }
            psxrom_tmd_tri *tri = &obj->tris[obj->num_tris++];
            for (int k = 0; k < 3; k++) tri->v[k] = pv[idx[t][k]];
            tri->textured = (uint8_t)tex;
            tri->double_sided = (flag & 2) != 0;
            tri->tsb = tsb;
            tri->clut = clut;
            if (!has_normal) face_normal(tri);
        }
        pos = next;
    }
    return PSXROM_OK;

bad:
    free(obj->tris);
    obj->tris = NULL;
    obj->num_tris = 0;
    return PSXROM_ERR_FORMAT;
}

psxrom_result psxrom_tmd_parse(const uint8_t *data, size_t size, psxrom_tmd *out)
{
    memset(out, 0, sizeof *out);
    if (size < HDR_SIZE || rd32(data) != TMD_ID || rd32(data + 4) != 0) return PSXROM_ERR_FORMAT;
    uint32_t n = rd32(data + 8);
    if (n == 0 || (uint64_t)n * OBJ_SIZE > size - HDR_SIZE) return PSXROM_ERR_FORMAT;

    out->objects = calloc(n, sizeof *out->objects);
    if (!out->objects) return PSXROM_ERR_NOMEM;
    out->num_objects = n;
    for (uint32_t i = 0; i < n; i++) {
        psxrom_result r = parse_object(data + HDR_SIZE, size - HDR_SIZE,
                                       data + HDR_SIZE + (size_t)i * OBJ_SIZE, &out->objects[i]);
        if (r != PSXROM_OK) { psxrom_tmd_free(out); return r; }
    }
    return PSXROM_OK;
}

void psxrom_tmd_free(psxrom_tmd *tmd)
{
    if (!tmd) return;
    for (size_t i = 0; i < tmd->num_objects; i++) free(tmd->objects[i].tris);
    free(tmd->objects);
    memset(tmd, 0, sizeof *tmd);
}
