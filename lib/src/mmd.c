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

static void pose_identity(psxrom_mmd_pose *p)
{
    memset(p, 0, sizeof *p);
    p->scale[0] = p->scale[1] = p->scale[2] = 1.0f;
}

/* Reads the initial pose of bones 1..n-1; the caller has checked that the data is large enough. */
static void read_initial_poses(const uint8_t *p, int has_scale, size_t bones, psxrom_mmd_pose *poses)
{
    pose_identity(&poses[0]);
    for (size_t b = 1; b < bones; b++) {
        pose_identity(&poses[b]);
        if (has_scale) {
            for (int k = 0; k < 3; k++) poses[b].scale[k] = (float)rds16(p + k * 2) / 4096.0f;
            p += 6;
        }
        for (int k = 0; k < 3; k++) poses[b].rot[k] = (float)rds16(p + k * 2) * 360.0f / 4096.0f;
        for (int k = 0; k < 3; k++) poses[b].pos[k] = (float)rds16(p + 6 + k * 2);
        p += 12;
    }
}

static psxrom_result check_initial_poses(size_t size, uint32_t off, int has_scale, size_t bones)
{
    size_t stride = has_scale ? 18 : 12;
    if (bones > 1 && (size - off - 2) / stride < bones - 1) return PSXROM_ERR_FORMAT;
    return PSXROM_OK;
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
        int has_scale = (rd16(anim + off) & 0x8000) != 0;
        if (check_initial_poses(size, off, has_scale, bones) != PSXROM_OK) return PSXROM_ERR_FORMAT;

        psxrom_mmd_pose *poses = malloc(bones * sizeof *poses);
        if (!poses) return PSXROM_ERR_NOMEM;
        read_initial_poses(anim + off + 2, has_scale, bones, poses);
        for (size_t b = 0; b < bones; b++) local[b] = xf_local(poses[b].rot, poses[b].pos, poses[b].scale);
        free(poses);
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

/* ---- animations ---- */

#define ANIM_FPS 20.0f
#define MAX_KEYFRAMES 65536u

typedef struct {
    uint8_t op;
    uint16_t timecode;
    uint16_t arg;               /* loop count, or the frame a loop end jumps back to */
    const uint8_t *entries;     /* keyframe entries */
    size_t nentries;
} instr;

static int popcount9(unsigned v)
{
    int n = 0;
    for (; v; v >>= 1) n += (int)(v & 1);
    return n;
}

/* Instruction stream: 4-bit opcode + 12-bit time code per word, terminated by a zero word. */
static psxrom_result parse_instrs(const uint8_t *sec, size_t size, size_t pos, instr **out, size_t *count)
{
    size_t cap = 32, n = 0;
    instr *v = malloc(cap * sizeof *v);
    if (!v) return PSXROM_ERR_NOMEM;

    for (;;) {
        if (size - pos < 2) goto bad;
        uint16_t w = rd16(sec + pos);
        pos += 2;
        if (!w) break;
        if (n == cap) {
            instr *g = realloc(v, cap * 2 * sizeof *v);
            if (!g) { free(v); return PSXROM_ERR_NOMEM; }
            v = g;
            cap *= 2;
        }
        instr *in = &v[n++];
        memset(in, 0, sizeof *in);
        in->op = (uint8_t)(w >> 12);
        in->timecode = w & 0xFFF;
        switch (in->op) {
        case 0:
            in->entries = sec + pos;
            while (size - pos >= 2 && (rd16(sec + pos) & 0x8000)) {
                if (size - pos < 4) goto bad;
                size_t len = 4 + (size_t)popcount9((rd16(sec + pos) & 0x7FC0) >> 6) * 2;
                if (size - pos < len) goto bad;
                pos += len;
                in->nentries++;
            }
            break;
        case 1: in->arg = w & 0xFF; break;
        case 2:
            if (size - pos < 2) goto bad;
            in->arg = rd16(sec + pos);
            pos += 2;
            break;
        case 3:
            if (size - pos < 6) goto bad;
            pos += 6;
            break;
        case 4:
            if (size - pos < 2) goto bad;
            pos += 2;
            break;
        default: goto bad;
        }
    }
    *out = v;
    *count = n;
    return PSXROM_OK;
bad:
    free(v);
    return PSXROM_ERR_FORMAT;
}

typedef struct {
    float *val, *mom;       /* per bone: 9 axes, momentum is the change per frame */
    size_t bones, nv, cap;
    uint32_t last;
    psxrom_mmd_anim *a;
} sim;

static psxrom_result sim_record(sim *s, uint32_t key)
{
    float t = (float)(key - 1) / ANIM_FPS;
    psxrom_mmd_anim *a = s->a;
    if (a->num_samples && t <= a->times[a->num_samples - 1]) return PSXROM_OK;
    if (a->num_samples == s->cap) {
        size_t c = s->cap ? s->cap * 2 : 32;
        float *tm = realloc(a->times, c * sizeof *tm);
        if (!tm) return PSXROM_ERR_NOMEM;
        a->times = tm;
        psxrom_mmd_pose *ps = realloc(a->poses, c * s->bones * sizeof *ps);
        if (!ps) return PSXROM_ERR_NOMEM;
        a->poses = ps;
        s->cap = c;
    }
    a->times[a->num_samples] = t;
    memcpy(a->poses + a->num_samples * s->bones, s->val, s->nv * sizeof *s->val);
    a->num_samples++;
    return PSXROM_OK;
}

static psxrom_result sim_update(sim *s, uint32_t key)
{
    float dt = (float)(key - s->last);
    for (size_t i = 0; i < s->nv; i++) s->val[i] += s->mom[i] * dt;
    s->last = key;
    return sim_record(s, key);
}

static void sim_keyframe(sim *s, const instr *in)
{
    const uint8_t *p = in->entries;
    for (size_t e = 0; e < in->nentries; e++) {
        uint16_t w = rd16(p);
        unsigned axes = (w & 0x7FC0) >> 6, node = w & 0x3F;
        float scale = (float)rd16(p + 2);
        if (scale == 0) scale = 1;
        p += 4;
        for (int bit = 8; bit >= 0; bit--) {
            if (!(axes & (1u << bit))) continue;
            int axis = 8 - bit;
            float f = axis < 3 ? 1.0f / 4096.0f : axis < 6 ? 360.0f / 4096.0f : 1.0f;
            if (node < s->bones) s->mom[node * 9 + (size_t)axis] = (float)rds16(p) / scale * f;
            p += 2;
        }
    }
}

/* Replays the animation script frame by frame, sampling the pose at every keyframe. */
static psxrom_result simulate(const instr *ins, size_t nins, size_t bones, uint32_t frames,
                              const psxrom_mmd_pose *init, psxrom_mmd_anim *a)
{
    sim s = {0};
    s.bones = bones;
    s.nv = bones * 9;
    s.a = a;
    s.last = 1;
    s.val = malloc(s.nv * sizeof *s.val);
    s.mom = calloc(s.nv, sizeof *s.mom);
    if (!s.val || !s.mom) { free(s.val); free(s.mom); return PSXROM_ERR_NOMEM; }
    memcpy(s.val, init, s.nv * sizeof *s.val);

    psxrom_result r = sim_record(&s, 1);
    uint32_t mtn = 1, key = 1, loops = 0;
    size_t cur = 0, back = 0;

    do {
        while (r == PSXROM_OK && cur < nins) {
            const instr *in = &ins[cur];
            if (in->op == 1) {
                back = cur;
                loops = in->arg;
            } else if (mtn != in->timecode) {
                break;
            } else if (in->op == 0) {
                r = sim_update(&s, key);
                sim_keyframe(&s, in);
            } else if (in->op == 2) {
                if (loops == 0xFF || loops == 0) {
                    a->loop_end = (float)(in->timecode - 1) / ANIM_FPS;
                    a->loop_start = (float)((int)in->arg - 1) / ANIM_FPS;
                } else if (--loops != 0) {
                    mtn = in->arg;
                    cur = back;
                }
            }
            cur++;
        }
        mtn++;
        key++;
    } while (r == PSXROM_OK && mtn <= frames && key < MAX_KEYFRAMES);

    if (r == PSXROM_OK) r = sim_update(&s, key);
    free(s.val);
    free(s.mom);
    return r;
}

void psxrom_mmd_free(psxrom_mmd_model *m)
{
    if (!m) return;
    for (size_t i = 0; i < m->num_anims; i++) {
        free(m->anims[i].times);
        free(m->anims[i].poses);
    }
    free(m->anims);
    free(m->rest);
    free(m->nodes);
    psxrom_tmd_free(&m->tmd);
    memset(m, 0, sizeof *m);
}

static int hierarchy_ok(const psxrom_dw1_node *nodes, size_t bones)
{
    for (size_t i = 0; i < bones; i++) {
        size_t cur = i, steps = 0;
        while (nodes[cur].parent != NO_NODE) {
            if (nodes[cur].parent >= bones || ++steps > bones) return 0;
            cur = nodes[cur].parent;
        }
    }
    return 1;
}

psxrom_result psxrom_mmd_load(const uint8_t *data, size_t size,
                              const psxrom_dw1_node *nodes, size_t bones, psxrom_mmd_model *out)
{
    memset(out, 0, sizeof *out);
    if (!nodes || !bones || bones > 64 || !hierarchy_ok(nodes, bones)) return PSXROM_ERR_RANGE;
    if (size < 8) return PSXROM_ERR_FORMAT;
    uint32_t tmd_off = rd32(data), anim_off = rd32(data + 4);
    if (tmd_off < 8 || tmd_off >= size || anim_off >= size) return PSXROM_ERR_FORMAT;
    const uint8_t *sec = data + anim_off;
    size_t sec_size = size - anim_off;
    if (sec_size < 4) return PSXROM_ERR_FORMAT;
    uint32_t table = rd32(sec);
    if (table < 4 || table > sec_size || table % 4) return PSXROM_ERR_FORMAT;

    psxrom_result r = psxrom_tmd_parse(data + tmd_off, size - tmd_off, &out->tmd);
    if (r != PSXROM_OK) return r;

    out->bone_count = bones;
    out->nodes = malloc(bones * sizeof *out->nodes);
    out->rest = malloc(bones * sizeof *out->rest);
    out->num_anims = table / 4;
    out->anims = calloc(out->num_anims, sizeof *out->anims);
    psxrom_mmd_pose *init = malloc(bones * sizeof *init);
    if (!out->nodes || !out->rest || !out->anims || !init) { r = PSXROM_ERR_NOMEM; goto fail; }
    memcpy(out->nodes, nodes, bones * sizeof *nodes);
    for (size_t b = 0; b < bones; b++) pose_identity(&out->rest[b]);

    int have_rest = 0;
    for (size_t i = 0; i < out->num_anims; i++) {
        psxrom_mmd_anim *a = &out->anims[i];
        a->loop_start = a->loop_end = -1.0f;
        uint32_t off = rd32(sec + i * 4);
        if (!off) continue;
        if (off >= sec_size || sec_size - off < 2) { r = PSXROM_ERR_FORMAT; goto fail; }

        uint16_t hdr = rd16(sec + off);
        int has_scale = (hdr & 0x8000) != 0;
        a->frame_count = hdr & 0x7FFF;
        if ((r = check_initial_poses(sec_size, off, has_scale, bones)) != PSXROM_OK) goto fail;
        read_initial_poses(sec + off + 2, has_scale, bones, init);
        if (!have_rest) {
            memcpy(out->rest, init, bones * sizeof *init);
            have_rest = 1;
        }

        instr *ins;
        size_t nins;
        size_t pos = off + 2 + (bones > 1 ? (bones - 1) * (has_scale ? 18u : 12u) : 0);
        if ((r = parse_instrs(sec, sec_size, pos, &ins, &nins)) != PSXROM_OK) goto fail;
        r = simulate(ins, nins, bones, a->frame_count, init, a);
        free(ins);
        if (r != PSXROM_OK) goto fail;
    }
    free(init);
    return PSXROM_OK;

fail:
    free(init);
    psxrom_mmd_free(out);
    return r;
}
