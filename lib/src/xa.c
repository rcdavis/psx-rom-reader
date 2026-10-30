#include "psxrom/psxrom.h"

#include <stdlib.h>
#include <string.h>

#define SUBMODE_AUDIO 0x04
#define GROUPS_PER_SECTOR 18
#define SAMPLES_PER_BLOCK 28

static int is_audio(const psxrom_raw_sector *s)
{
    return (s->submode & SUBMODE_AUDIO) && s->data_size >= GROUPS_PER_SECTOR * 128;
}

static void fill_info(psxrom_xa_stream *st, const psxrom_raw_sector *s, uint32_t lba)
{
    st->file = s->file;
    st->channel = s->channel;
    st->coding = s->coding;
    st->stereo = s->coding & 1;
    st->sample_rate = (s->coding & 4) ? 18900 : 37800;
    st->bits = (s->coding & 0x10) ? 8 : 4;
    st->first_lba = lba;
}

psxrom_result psxrom_xa_scan(psxrom_disc *d, uint32_t lba, uint32_t count,
                             psxrom_xa_stream **streams, size_t *num_streams)
{
    if (!psxrom_disc_has_raw(d)) return PSXROM_ERR_FORMAT;
    psxrom_xa_stream *list = NULL;
    size_t n = 0, cap = 0;
    psxrom_raw_sector s;

    for (uint32_t i = 0; i < count && lba + i < psxrom_disc_sector_count(d); i++) {
        psxrom_result r = psxrom_disc_read_raw(d, lba + i, &s);
        if (r != PSXROM_OK) { free(list); return r; }
        if (!is_audio(&s)) continue;

        size_t k;
        for (k = 0; k < n; k++)
            if (list[k].file == s.file && list[k].channel == s.channel) break;
        if (k == n) {
            if (n == cap) {
                size_t ncap = cap ? cap * 2 : 16;
                psxrom_xa_stream *p = realloc(list, ncap * sizeof *list);
                if (!p) { free(list); return PSXROM_ERR_NOMEM; }
                list = p;
                cap = ncap;
            }
            memset(&list[n], 0, sizeof list[n]);
            fill_info(&list[n], &s, lba + i);
            n++;
        }
        if (s.coding == list[k].coding) list[k].sectors++;
    }
    *streams = list;
    *num_streams = n;
    return PSXROM_OK;
}

typedef struct {
    int old, older;
} adpcm_state;

static const int filter0[4] = {0, 60, 115, 98};
static const int filter1[4] = {0, 0, -52, -55};

static int clamp16(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

/* Decodes one 28-sample block of a sound group. */
static void decode_block(const uint8_t *group, unsigned blk, int bits8,
                         adpcm_state *st, int16_t *out)
{
    unsigned param = group[4 + blk];
    unsigned range = param & 0xF;
    unsigned filter = (param >> 4) & 3;
    if (range > 12) range = 9;

    for (unsigned w = 0; w < SAMPLES_PER_BLOCK; w++) {
        const uint8_t *word = group + 16 + w * 4;
        int16_t raw;
        if (bits8)
            raw = (int16_t)(word[blk] << 8);
        else
            raw = (int16_t)(((word[blk >> 1] >> ((blk & 1) * 4)) & 0xF) << 12);
        int s = (raw >> range) + ((st->old * filter0[filter] + st->older * filter1[filter] + 32) >> 6);
        s = clamp16(s);
        st->older = st->old;
        st->old = s;
        out[w] = (int16_t)s;
    }
}

psxrom_result psxrom_xa_decode(psxrom_disc *d, uint32_t lba, uint32_t count,
                               uint8_t file, uint8_t channel, psxrom_xa_pcm *out)
{
    if (!psxrom_disc_has_raw(d)) return PSXROM_ERR_FORMAT;
    memset(out, 0, sizeof *out);

    int16_t *pcm = NULL;
    size_t cap = 0, frames = 0;
    int have_coding = 0;
    uint8_t coding = 0;
    adpcm_state state[2] = {{0, 0}, {0, 0}};
    psxrom_raw_sector s;

    for (uint32_t i = 0; i < count && lba + i < psxrom_disc_sector_count(d); i++) {
        psxrom_result r = psxrom_disc_read_raw(d, lba + i, &s);
        if (r != PSXROM_OK) { free(pcm); return r; }
        if (!is_audio(&s) || s.file != file || s.channel != channel) continue;
        if (!have_coding) { coding = s.coding; have_coding = 1; }
        else if (s.coding != coding) continue;

        int stereo = coding & 1;
        int bits8 = (coding & 0x10) != 0;
        unsigned blocks = bits8 ? 4 : 8;
        size_t add = (size_t)GROUPS_PER_SECTOR * SAMPLES_PER_BLOCK * (blocks / (stereo ? 2 : 1));
        if (frames + add > cap) {
            size_t ncap = cap ? cap * 2 : 1 << 16;
            while (ncap < frames + add) ncap *= 2;
            int16_t *p = realloc(pcm, ncap * (stereo ? 2 : 1) * sizeof *pcm);
            if (!p) { free(pcm); return PSXROM_ERR_NOMEM; }
            pcm = p;
            cap = ncap;
        }

        for (unsigned g = 0; g < GROUPS_PER_SECTOR; g++) {
            const uint8_t *group = s.data + g * 128;
            if (stereo) {
                for (unsigned b = 0; b < blocks; b += 2) {
                    int16_t l[SAMPLES_PER_BLOCK], rr[SAMPLES_PER_BLOCK];
                    decode_block(group, b, bits8, &state[0], l);
                    decode_block(group, b + 1, bits8, &state[1], rr);
                    for (unsigned k = 0; k < SAMPLES_PER_BLOCK; k++) {
                        pcm[(frames + k) * 2] = l[k];
                        pcm[(frames + k) * 2 + 1] = rr[k];
                    }
                    frames += SAMPLES_PER_BLOCK;
                }
            } else {
                for (unsigned b = 0; b < blocks; b++) {
                    decode_block(group, b, bits8, &state[0], pcm + frames);
                    frames += SAMPLES_PER_BLOCK;
                }
            }
        }
    }

    if (!have_coding) return PSXROM_ERR_NOT_FOUND;
    out->samples = pcm;
    out->frames = frames;
    out->channels = (coding & 1) ? 2 : 1;
    out->sample_rate = (coding & 4) ? 18900 : 37800;
    return PSXROM_OK;
}

void psxrom_xa_pcm_free(psxrom_xa_pcm *pcm)
{
    free(pcm->samples);
    memset(pcm, 0, sizeof *pcm);
}
