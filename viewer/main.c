#include <psxrom/psxrom.h>

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define POPEN_WRITE(c) _popen((c), "wb")
#define PCLOSE _pclose
#define SHELL_QUOTE '"'
#else
#include <signal.h>
#define MKDIR(p) mkdir((p), 0777)
#define POPEN_WRITE(c) popen((c), "w")
#define PCLOSE pclose
#define SHELL_QUOTE '\''
#endif

static void usage(void)
{
	fputs("usage: psxview <image> <command> [args]\n"
		  "  info                       volume label, boot executable, sector count\n"
		  "  ls                         list all files\n"
		  "  hex <path> [count]         hex dump (default 256 bytes)\n"
		  "  extract <path> <out>       extract a file\n"
		  "  extract-all <dir>          extract every file into dir\n"
		  "  tims                       list files that contain a valid TIM texture\n"
		  "  tim <path> <out.bmp> [n]   describe a TIM and convert it to BMP (palette n)\n"
		  "  tim-all <dir>              convert every TIM file to BMP in dir\n"
		  "  xa-list [path]             list CD-XA audio streams in a file (default: whole disc)\n"
		  "  xa <path> <file> <ch> <out.wav>  decode one XA stream to WAV\n"
		  "  xa-all <path> <dir>        decode every XA stream in a file to WAV\n"
		  "  str-info <path>            frame count and size of a .STR movie\n"
		  "  str-frame <path> <n> <out.bmp>   decode frame n (0-based) to BMP\n"
		  "  str-frames <path> <dir> [first] [count]  decode frames to BMP files\n"
		  "  str-y4m <path> <out.y4m>   write the movie as YUV4MPEG2 video (no audio)\n"
		  "  tmd <path> [out.obj]       summarize a TMD model; export by extension: .glb .gltf .obj\n"
		  "  tmd-all <dir> [glb|gltf|obj]  export every .TMD file into dir (default glb)\n"
		  "  str-convert <path> <out>   video + XA audio to webm/mkv/mp4/... (needs ffmpeg in PATH)\n",
		  stderr);
}

static int write_file(const char *path, const void *data, size_t n)
{
	FILE *f = fopen(path, "wb");
	if (!f) return 0;
	int ok = fwrite(data, 1, n, f) == n;
	return fclose(f) == 0 && ok;
}

static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static int write_bmp(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
	size_t px = (size_t)w * h * 4;
	uint8_t *buf = calloc(1, 54 + px);
	if (!buf) return 0;
	buf[0] = 'B'; buf[1] = 'M';
	put32(buf + 2, (uint32_t)(54 + px));
	put32(buf + 10, 54);
	put32(buf + 14, 40);
	put32(buf + 18, w);
	put32(buf + 22, (uint32_t)-(int32_t)h); /* top-down */
	buf[26] = 1; buf[28] = 32;
	put32(buf + 34, (uint32_t)px);
	for (size_t i = 0; i < (size_t)w * h; i++) { /* RGBA -> BGRA */
		buf[54 + i * 4] = rgba[i * 4 + 2];
		buf[54 + i * 4 + 1] = rgba[i * 4 + 1];
		buf[54 + i * 4 + 2] = rgba[i * 4];
		buf[54 + i * 4 + 3] = rgba[i * 4 + 3];
	}
	int ok = write_file(path, buf, 54 + px);
	free(buf);
	return ok;
}

static void mkdirs_for(const char *path)
{
	char tmp[2048];
	snprintf(tmp, sizeof tmp, "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') { *p = 0; MKDIR(tmp); *p = '/'; }
	}
}

static int cb_ls(const psxrom_entry *e, void *u)
{
	(void)u;
	printf("%10u  %8u  %s%s\n", e->size, e->lba, e->path, e->is_dir ? "/" : "");
	return 0;
}

struct out_ctx { psxrom_disc *d; const char *dir; int errors; const char *ext; };

static int cb_extract(const psxrom_entry *e, void *u)
{
	struct out_ctx *c = u;
	char out[2048];
	snprintf(out, sizeof out, "%s%s", c->dir, e->path);
	if (e->is_dir) { mkdirs_for(out); MKDIR(out); return 0; }
	uint8_t *data; size_t n;
	psxrom_result r = psxrom_read_file(c->d, e, &data, &n);
	if (r == PSXROM_OK) {
		mkdirs_for(out);
		if (!write_file(out, data, n)) r = PSXROM_ERR_IO;
		free(data);
	}
	if (r != PSXROM_OK) { fprintf(stderr, "%s: %s\n", e->path, psxrom_strerror(r)); c->errors++; }
	else printf("%s\n", out);
	return 0;
}

static void describe_tim(const psxrom_tim *t)
{
	printf("TIM %ux%u %u-bpp, vram (%u,%u)", t->width, t->height, t->bpp, t->img_x, t->img_y);
	if (t->has_clut)
		printf(", clut %ux%u at (%u,%u): %u palette(s)", t->clut_w, t->clut_h,
			   t->clut_x, t->clut_y, t->num_cluts);
	putchar('\n');
}

static int cb_tims(const psxrom_entry *e, void *u)
{
	struct out_ctx *c = u;
	if (e->is_dir || e->size < 20) return 0;
	uint8_t *data; size_t n;
	if (psxrom_read_file(c->d, e, &data, &n) != PSXROM_OK) return 0;
	psxrom_tim t;
	if (psxrom_tim_parse(data, n, &t) == PSXROM_OK) {
		printf("%s: ", e->path);
		describe_tim(&t);
		if (c->dir) {
			uint8_t *rgba;
			char out[2048];
			snprintf(out, sizeof out, "%s%s.bmp", c->dir, e->path);
			if (psxrom_tim_to_rgba(&t, 0, &rgba) == PSXROM_OK) {
				mkdirs_for(out);
				if (!write_bmp(out, rgba, t.width, t.height)) c->errors++;
				free(rgba);
			}
		}
	}
	free(data);
	return 0;
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static int write_wav(const char *path, const psxrom_xa_pcm *pcm)
{
	size_t bytes = pcm->frames * pcm->channels * 2;
	uint8_t *buf = malloc(44 + bytes);
	if (!buf) return 0;
	memcpy(buf, "RIFF", 4);
	put32(buf + 4, (uint32_t)(36 + bytes));
	memcpy(buf + 8, "WAVEfmt ", 8);
	put32(buf + 16, 16);
	put16(buf + 20, 1);
	put16(buf + 22, pcm->channels);
	put32(buf + 24, pcm->sample_rate);
	put32(buf + 28, pcm->sample_rate * pcm->channels * 2);
	put16(buf + 32, pcm->channels * 2);
	put16(buf + 34, 16);
	memcpy(buf + 36, "data", 4);
	put32(buf + 40, (uint32_t)bytes);
	for (size_t i = 0; i < pcm->frames * pcm->channels; i++) /* little-endian samples */
		put16(buf + 44 + i * 2, (uint16_t)pcm->samples[i]);
	int ok = write_file(path, buf, 44 + bytes);
	free(buf);
	return ok;
}

/* Sector range of a file, or the whole disc when path is NULL. */
static psxrom_result sector_range(psxrom_disc *d, const char *path, uint32_t *lba, uint32_t *count)
{
	if (!path) {
		*lba = 0;
		*count = psxrom_disc_sector_count(d);
		return PSXROM_OK;
	}
	psxrom_entry e;
	psxrom_result r = psxrom_find(d, path, &e);
	if (r != PSXROM_OK) return r;
	*lba = e.lba;
	*count = (e.size + 2047) / 2048;
	return PSXROM_OK;
}

static void print_xa_stream(const psxrom_xa_stream *s)
{
	printf("file %3u channel %2u  %s %u Hz %u-bit  %u sectors  first lba %u\n", s->file, s->channel,
		   s->stereo ? "stereo" : "mono", s->sample_rate, s->bits, s->sectors, s->first_lba);
}

static psxrom_result cmd_xa_all(psxrom_disc *d, const char *path, const char *dir)
{
	uint32_t lba, count;
	psxrom_xa_stream *streams;
	size_t n;
	psxrom_result r = sector_range(d, path, &lba, &count);
	if (r == PSXROM_OK) r = psxrom_xa_scan(d, lba, count, &streams, &n);
	if (r != PSXROM_OK) return r;
	MKDIR(dir);
	for (size_t i = 0; i < n && r == PSXROM_OK; i++) {
		psxrom_xa_pcm pcm;
		char out[2048];
		if ((r = psxrom_xa_decode(d, lba, count, streams[i].file, streams[i].channel, &pcm)) != PSXROM_OK) break;
		snprintf(out, sizeof out, "%s/xa_f%u_c%u.wav", dir, streams[i].file, streams[i].channel);
		if (!write_wav(out, &pcm)) r = PSXROM_ERR_IO;
		else printf("%s\n", out);
		psxrom_xa_pcm_free(&pcm);
	}
	free(streams);
	return r;
}

static psxrom_result cmd_str_info(psxrom_disc *d, const char *path)
{
	uint32_t lba, count;
	psxrom_result r = sector_range(d, path, &lba, &count);
	psxrom_str *s;
	if (r != PSXROM_OK || (r = psxrom_str_open(d, lba, count, &s)) != PSXROM_OK) return r;
	psxrom_str_frame f;
	uint32_t frames = 0, first = 0, last = 0;
	uint16_t w = 0, h = 0;
	while (psxrom_str_next_frame(s, &f) == PSXROM_OK) {
		if (!frames) { first = f.number; w = f.width; h = f.height; }
		last = f.number;
		frames++;
	}
	psxrom_str_close(s);
	if (!frames) return PSXROM_ERR_NOT_FOUND;
	printf("%ux%u, %u frames (numbers %u-%u), %u sectors", w, h, frames, first, last, count);
	printf(", %.2f sectors/frame (%.2f fps at 2x speed)\n", (double)count / frames,
		   150.0 * frames / count);
	return PSXROM_OK;
}

static psxrom_result cmd_str_frames(psxrom_disc *d, const char *path, const char *out,
									int single, uint32_t first, uint32_t num)
{
	uint32_t lba, count;
	psxrom_result r = sector_range(d, path, &lba, &count);
	psxrom_str *s;
	if (r != PSXROM_OK || (r = psxrom_str_open(d, lba, count, &s)) != PSXROM_OK) return r;
	if (!single) MKDIR(out);

	psxrom_str_frame f;
	uint32_t index = 0, written = 0;
	while (written < num && psxrom_str_next_frame(s, &f) == PSXROM_OK) {
		if (index++ < first) continue;
		uint8_t *rgba;
		char name[2048];
		const char *target = out;
		if (!single) {
			snprintf(name, sizeof name, "%s/frame_%05u.bmp", out, f.number);
			target = name;
		}
		r = psxrom_mdec_decode(f.data, f.size, f.width, f.height, &rgba);
		if (r != PSXROM_OK) {
			fprintf(stderr, "frame %u: %s\n", f.number, psxrom_strerror(r));
			r = PSXROM_OK;
			continue;
		}
		if (!write_bmp(target, rgba, f.width, f.height)) r = PSXROM_ERR_IO;
		else printf("%s\n", target);
		free(rgba);
		written++;
		if (r != PSXROM_OK) break;
	}
	psxrom_str_close(s);
	return (r == PSXROM_OK && !written) ? PSXROM_ERR_NOT_FOUND : r;
}

struct str_timing {
	uint16_t width, height;
	uint32_t frames;
	uint32_t first_lba, last_lba;
	uint32_t fps_num, fps_den;
};

static uint32_t gcd32(uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }

/* STR files store no frame rate; derive it from the sector distance between frames (150 sectors/s). */
static psxrom_result str_timing(psxrom_disc *d, uint32_t lba, uint32_t count, struct str_timing *t)
{
	psxrom_str *s;
	psxrom_result r = psxrom_str_open(d, lba, count, &s);
	if (r != PSXROM_OK) return r;
	psxrom_str_frame f;
	memset(t, 0, sizeof *t);
	while (psxrom_str_next_frame(s, &f) == PSXROM_OK) {
		if (!t->frames) { t->width = f.width; t->height = f.height; t->first_lba = f.first_lba; }
		t->last_lba = f.first_lba;
		t->frames++;
	}
	psxrom_str_close(s);
	if (!t->frames) return PSXROM_ERR_NOT_FOUND;

	uint32_t num = 15, den = 1;
	if (t->frames > 1 && t->last_lba > t->first_lba) {
		num = 150 * (t->frames - 1);
		den = t->last_lba - t->first_lba;
		uint32_t g = gcd32(num, den);
		num /= g;
		den /= g;
		uint32_t nearest = (num + den / 2) / den;
		if (nearest && (double)(num > nearest * den ? num - nearest * den : nearest * den - num) / ((double)nearest * den) < 0.01) {
			num = nearest;
			den = 1;
		}
	}
	t->fps_num = num;
	t->fps_den = den;
	return PSXROM_OK;
}

/* RGBA -> planar YCbCr 4:2:0, BT.601 limited range. */
static void rgba_to_i420(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *out)
{
	uint32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
	uint8_t *yp = out, *up = out + (size_t)w * h, *vp = up + (size_t)cw * ch;
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			const uint8_t *p = rgba + ((size_t)y * w + x) * 4;
			yp[(size_t)y * w + x] = (uint8_t)((66 * p[0] + 129 * p[1] + 25 * p[2] + 128) / 256 + 16);
		}
	}
	for (uint32_t y = 0; y < ch; y++) {
		for (uint32_t x = 0; x < cw; x++) {
			int r = 0, g = 0, b = 0, n = 0;
			for (uint32_t dy = 0; dy < 2; dy++)
				for (uint32_t dx = 0; dx < 2; dx++) {
					uint32_t px = x * 2 + dx, py = y * 2 + dy;
					if (px >= w || py >= h) continue;
					const uint8_t *p = rgba + ((size_t)py * w + px) * 4;
					r += p[0]; g += p[1]; b += p[2]; n++;
				}
			r /= n; g /= n; b /= n;
			int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
			int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
			up[(size_t)y * cw + x] = (uint8_t)(u < 0 ? 0 : u > 255 ? 255 : u);
			vp[(size_t)y * cw + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
		}
	}
}

/* Streams the movie as YUV4MPEG2. Undecodable frames repeat the previous one to keep timing. */
static psxrom_result write_y4m(psxrom_disc *d, uint32_t lba, uint32_t count,
							   const struct str_timing *t, FILE *out)
{
	uint32_t cw = (t->width + 1) / 2, ch = (t->height + 1) / 2;
	size_t frame_size = (size_t)t->width * t->height + 2 * (size_t)cw * ch;
	uint8_t *i420 = calloc(1, frame_size);
	if (!i420) return PSXROM_ERR_NOMEM;
	memset(i420, 16, (size_t)t->width * t->height);
	memset(i420 + (size_t)t->width * t->height, 128, 2 * (size_t)cw * ch);

	psxrom_str *s;
	psxrom_result r = psxrom_str_open(d, lba, count, &s);
	if (r != PSXROM_OK) { free(i420); return r; }

	if (fprintf(out, "YUV4MPEG2 W%u H%u F%u:%u Ip A1:1 C420jpeg\n", t->width, t->height,
				t->fps_num, t->fps_den) < 0)
		r = PSXROM_ERR_IO;

	psxrom_str_frame f;
	uint32_t bad = 0;
	while (r == PSXROM_OK && psxrom_str_next_frame(s, &f) == PSXROM_OK) {
		uint8_t *rgba;
		if (f.width == t->width && f.height == t->height &&
			psxrom_mdec_decode(f.data, f.size, f.width, f.height, &rgba) == PSXROM_OK) {
			rgba_to_i420(rgba, t->width, t->height, i420);
			free(rgba);
		} else {
			bad++;
		}
		if (fputs("FRAME\n", out) < 0 || fwrite(i420, 1, frame_size, out) != frame_size)
			r = PSXROM_ERR_IO;
	}
	if (bad) fprintf(stderr, "warning: %u frame(s) failed to decode and were repeated\n", bad);
	psxrom_str_close(s);
	free(i420);
	if (r == PSXROM_OK && fflush(out) != 0) r = PSXROM_ERR_IO;
	return r;
}

static psxrom_result cmd_str_y4m(psxrom_disc *d, const char *path, const char *out_path)
{
	uint32_t lba, count;
	struct str_timing t;
	psxrom_result r = sector_range(d, path, &lba, &count);
	if (r == PSXROM_OK) r = str_timing(d, lba, count, &t);
	if (r != PSXROM_OK) return r;
	FILE *f = fopen(out_path, "wb");
	if (!f) return PSXROM_ERR_IO;
	r = write_y4m(d, lba, count, &t, f);
	if (fclose(f) != 0 && r == PSXROM_OK) r = PSXROM_ERR_IO;
	if (r == PSXROM_OK)
		printf("%ux%u, %u frames at %u/%u fps\n", t.width, t.height, t.frames, t.fps_num, t.fps_den);
	return r;
}

/* Appends s to cmd as one shell word; fails on characters that cannot be quoted safely. */
static int append_arg(char *cmd, size_t cap, const char *s)
{
	size_t n = strlen(cmd);
	size_t len = strlen(s);
	if (n + len + 4 > cap) return 0;
	if (strchr(s, SHELL_QUOTE) || strchr(s, '\n') || strchr(s, '%')) return 0;
	cmd[n++] = ' ';
	cmd[n++] = SHELL_QUOTE;
	memcpy(cmd + n, s, len);
	n += len;
	cmd[n++] = SHELL_QUOTE;
	cmd[n] = 0;
	return 1;
}

static psxrom_result cmd_str_convert(psxrom_disc *d, const char *path, const char *out_path)
{
	uint32_t lba, count;
	struct str_timing t;
	psxrom_result r = sector_range(d, path, &lba, &count);
	if (r == PSXROM_OK) r = str_timing(d, lba, count, &t);
	if (r != PSXROM_OK) return r;

	/* Audio: first XA stream within the movie, written next to the output. */
	char wav_path[2048] = "";
	double audio_offset = 0;
	psxrom_xa_stream *streams = NULL;
	size_t nstreams = 0;
	if (psxrom_disc_has_raw(d) && psxrom_xa_scan(d, lba, count, &streams, &nstreams) == PSXROM_OK && nstreams) {
		psxrom_xa_pcm pcm;
		if (psxrom_xa_decode(d, lba, count, streams[0].file, streams[0].channel, &pcm) == PSXROM_OK) {
			snprintf(wav_path, sizeof wav_path, "%s.audio.wav", out_path);
			if (!write_wav(wav_path, &pcm)) { psxrom_xa_pcm_free(&pcm); free(streams); return PSXROM_ERR_IO; }
			audio_offset = ((double)streams[0].first_lba - (double)t.first_lba) / 150.0;
			printf("audio: file %u channel %u, %.3f s offset\n", streams[0].file, streams[0].channel, audio_offset);
			psxrom_xa_pcm_free(&pcm);
		}
	}
	free(streams);
	if (!wav_path[0])
		fprintf(stderr, "note: no XA audio decoded (needs a raw .bin image); video only\n");

	char cmd[8192] = "ffmpeg -hide_banner -loglevel error -y -f yuv4mpegpipe -i -";
	char offset[64];
	int ok = 1;
	if (wav_path[0]) {
		snprintf(offset, sizeof offset, "%.6f", audio_offset);
		strcat(cmd, " -itsoffset");
		ok = append_arg(cmd, sizeof cmd, offset) && strcat(cmd, " -i") && append_arg(cmd, sizeof cmd, wav_path);
	}
	ok = ok && append_arg(cmd, sizeof cmd, out_path);
	if (!ok) {
		fprintf(stderr, "paths with quotes, '%%' or newlines are not supported\n");
		if (wav_path[0]) remove(wav_path);
		return PSXROM_ERR_IO;
	}

#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
	FILE *pipe = POPEN_WRITE(cmd);
	if (!pipe) {
		fprintf(stderr, "could not start ffmpeg\n");
		if (wav_path[0]) remove(wav_path);
		return PSXROM_ERR_IO;
	}
	r = write_y4m(d, lba, count, &t, pipe);
	int status = PCLOSE(pipe);
	if (wav_path[0]) remove(wav_path);
	if (status != 0) {
		fprintf(stderr, "ffmpeg failed (is it installed and does '%s' have a known extension?)\n", out_path);
		return PSXROM_ERR_IO;
	}
	if (r == PSXROM_OK)
		printf("%s: %ux%u, %u frames at %u/%u fps\n", out_path, t.width, t.height, t.frames, t.fps_num, t.fps_den);
	return r;
}

/* Returns 1 and sets *r when cmd is a media (XA/STR) command. */
static int write_obj(const char *path, const psxrom_tmd *t)
{
	FILE *f = fopen(path, "w");
	if (!f) return 0;
	fputs("# exported by psxview; Y and Z are negated so +Y is up\n", f);
	size_t base = 1;
	for (size_t o = 0; o < t->num_objects; o++) {
		const psxrom_tmd_object *ob = &t->objects[o];
		fprintf(f, "o object%zu\n", o);
		for (size_t i = 0; i < ob->num_tris; i++)
			for (int k = 0; k < 3; k++) {
				const psxrom_tmd_vertex *v = &ob->tris[i].v[k];
				fprintf(f, "v %g %g %g %g %g %g\n", v->pos[0], -v->pos[1], -v->pos[2],
						v->color[0] / 255.0, v->color[1] / 255.0, v->color[2] / 255.0);
			}
		for (size_t i = 0; i < ob->num_tris; i++)
			for (int k = 0; k < 3; k++) {
				const psxrom_tmd_vertex *v = &ob->tris[i].v[k];
				fprintf(f, "vn %g %g %g\n", v->normal[0], -v->normal[1], -v->normal[2]);
			}
		for (size_t i = 0; i < ob->num_tris; i++)
			for (int k = 0; k < 3; k++) {
				const psxrom_tmd_vertex *v = &ob->tris[i].v[k];
				fprintf(f, "vt %g %g\n", v->uv[0] / 256.0, 1.0 - v->uv[1] / 256.0);
			}
		for (size_t i = 0; i < ob->num_tris; i++) {
			size_t n = base + i * 3;
			fprintf(f, "f %zu/%zu/%zu %zu/%zu/%zu %zu/%zu/%zu\n",
					n, n, n, n + 1, n + 1, n + 1, n + 2, n + 2, n + 2);
		}
		base += ob->num_tris * 3;
	}
	return fclose(f) == 0;
}

struct sbuf { char *p; size_t n, cap; };

static void sb_add(struct sbuf *b, const void *d, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		size_t c = b->cap ? b->cap : 256;
		while (c < b->n + n + 1) c *= 2;
		char *g = realloc(b->p, c);
		if (!g) { free(b->p); b->p = NULL; b->n = b->cap = 0; return; }
		b->p = g; b->cap = c;
	}
	memcpy(b->p + b->n, d, n);
	b->n += n;
}

static void sb_printf(struct sbuf *b, const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (n > 0 && (size_t)n < sizeof tmp) sb_add(b, tmp, (size_t)n);
}

static void sb_f32(struct sbuf *b, float f)
{
	uint32_t u;
	memcpy(&u, &f, 4);
	uint8_t le[4] = {(uint8_t)u, (uint8_t)(u >> 8), (uint8_t)(u >> 16), (uint8_t)(u >> 24)};
	sb_add(b, le, 4);
}

static void sb_u32(struct sbuf *b, uint32_t u)
{
	uint8_t le[4] = {(uint8_t)u, (uint8_t)(u >> 8), (uint8_t)(u >> 16), (uint8_t)(u >> 24)};
	sb_add(b, le, 4);
}

static void sb_base64(struct sbuf *b, const uint8_t *d, size_t n)
{
	static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	for (size_t i = 0; i < n; i += 3) {
		uint32_t v = (uint32_t)d[i] << 16;
		if (i + 1 < n) v |= (uint32_t)d[i + 1] << 8;
		if (i + 2 < n) v |= d[i + 2];
		char o[4] = {t[v >> 18 & 63], t[v >> 12 & 63],
					 i + 1 < n ? t[v >> 6 & 63] : '=', i + 2 < n ? t[v & 63] : '='};
		sb_add(b, o, 4);
	}
}

/* One mesh per TMD object; Y and Z are negated so +Y is up (a proper rotation, so winding is kept). */
static int write_gltf(const char *path, const psxrom_tmd *t, int binary)
{
	struct sbuf bin = {0}, js = {0};
	size_t nmesh = 0;
	sb_printf(&js, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"psxview\"},"
				   "\"materials\":[{\"doubleSided\":true,\"pbrMetallicRoughness\":"
				   "{\"metallicFactor\":0,\"roughnessFactor\":1}}],");
	struct sbuf views = {0}, accs = {0}, meshes = {0}, nodes = {0};
	size_t nviews = 0;
	for (size_t o = 0; o < t->num_objects; o++) {
		const psxrom_tmd_object *ob = &t->objects[o];
		if (!ob->num_tris) continue;
		size_t count = ob->num_tris * 3;
		float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
		size_t off[4], len[4] = {count * 12, count * 12, count * 12, count * 8};
		for (int a = 0; a < 4; a++) {
			off[a] = bin.n;
			for (size_t i = 0; i < ob->num_tris; i++)
				for (int k = 0; k < 3; k++) {
					const psxrom_tmd_vertex *v = &ob->tris[i].v[k];
					if (a == 0) {
						float p[3] = {v->pos[0], -v->pos[1], -v->pos[2]};
						for (int c = 0; c < 3; c++) {
							if (p[c] < mn[c]) mn[c] = p[c];
							if (p[c] > mx[c]) mx[c] = p[c];
							sb_f32(&bin, p[c]);
						}
					} else if (a == 1) {
						sb_f32(&bin, v->normal[0]); sb_f32(&bin, -v->normal[1]); sb_f32(&bin, -v->normal[2]);
					} else if (a == 2) {
						/* textured polygons use 128 as the neutral modulation colour */
						float sc = ob->tris[i].textured ? 1.0f / 128.0f : 1.0f / 255.0f;
						for (int c = 0; c < 3; c++) {
							float f = v->color[c] * sc;
							sb_f32(&bin, f > 1.0f ? 1.0f : f);
						}
					} else {
						sb_f32(&bin, v->uv[0] / 256.0f); sb_f32(&bin, v->uv[1] / 256.0f);
					}
				}
		}
		static const char *names[4] = {"POSITION", "NORMAL", "COLOR_0", "TEXCOORD_0"};
		static const char *types[4] = {"VEC3", "VEC3", "VEC3", "VEC2"};
		for (int a = 0; a < 4; a++) {
			sb_printf(&views, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34962}",
					  nviews ? "," : "", off[a], len[a]);
			sb_printf(&accs, "%s{\"bufferView\":%zu,\"componentType\":5126,\"count\":%zu,\"type\":\"%s\"",
					  nviews ? "," : "", nviews, count, types[a]);
			if (a == 0)
				sb_printf(&accs, ",\"min\":[%.9g,%.9g,%.9g],\"max\":[%.9g,%.9g,%.9g]",
						  mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
			sb_add(&accs, "}", 1);
			nviews++;
		}
		size_t a0 = nviews - 4;
		sb_printf(&meshes, "%s{\"name\":\"object%zu\",\"primitives\":[{\"attributes\":"
				  "{\"%s\":%zu,\"%s\":%zu,\"%s\":%zu,\"%s\":%zu},\"material\":0,\"mode\":4}]}",
				  nmesh ? "," : "", o, names[0], a0, names[1], a0 + 1, names[2], a0 + 2, names[3], a0 + 3);
		sb_printf(&nodes, "%s{\"mesh\":%zu,\"name\":\"object%zu\"}", nmesh ? "," : "", nmesh, o);
		nmesh++;
	}
	int ok = bin.p && nmesh && views.p && accs.p && meshes.p && nodes.p;
	if (ok) {
		sb_printf(&js, "\"scene\":0,\"scenes\":[{\"nodes\":[");
		for (size_t i = 0; i < nmesh; i++) sb_printf(&js, "%s%zu", i ? "," : "", i);
		sb_printf(&js, "]}],\"nodes\":["); sb_add(&js, nodes.p, nodes.n);
		sb_printf(&js, "],\"meshes\":["); sb_add(&js, meshes.p, meshes.n);
		sb_printf(&js, "],\"accessors\":["); sb_add(&js, accs.p, accs.n);
		sb_printf(&js, "],\"bufferViews\":["); sb_add(&js, views.p, views.n);
		sb_printf(&js, "],\"buffers\":[{\"byteLength\":%zu", bin.n);
		if (!binary) {
			sb_printf(&js, ",\"uri\":\"data:application/octet-stream;base64,");
			sb_base64(&js, (const uint8_t *)bin.p, bin.n);
			sb_printf(&js, "\"");
		}
		sb_printf(&js, "}]}");
		ok = js.p != NULL;
	}
	if (ok && binary) {
		static const char pad_sp[4] = {' ', ' ', ' ', ' '}, pad_zero[4] = {0, 0, 0, 0};
		size_t jp = (4 - js.n % 4) % 4, bp = (4 - bin.n % 4) % 4;
		struct sbuf out = {0};
		sb_u32(&out, 0x46546C67);
		sb_u32(&out, 2);
		sb_u32(&out, (uint32_t)(12 + 8 + js.n + jp + 8 + bin.n + bp));
		sb_u32(&out, (uint32_t)(js.n + jp)); sb_u32(&out, 0x4E4F534A);
		sb_add(&out, js.p, js.n); sb_add(&out, pad_sp, jp);
		sb_u32(&out, (uint32_t)(bin.n + bp)); sb_u32(&out, 0x004E4942);
		sb_add(&out, bin.p, bin.n); sb_add(&out, pad_zero, bp);
		ok = out.p && write_file(path, out.p, out.n);
		free(out.p);
	} else if (ok) {
		ok = write_file(path, js.p, js.n);
	}
	free(bin.p); free(js.p); free(views.p); free(accs.p); free(meshes.p); free(nodes.p);
	return ok;
}

static int ext_is(const char *path, const char *ext)
{
	size_t a = strlen(path), b = strlen(ext);
	if (a < b) return 0;
	for (size_t i = 0; i < b; i++)
		if (tolower((unsigned char)path[a - b + i]) != ext[i]) return 0;
	return 1;
}

static int write_model(const char *path, const psxrom_tmd *t)
{
	if (ext_is(path, ".glb")) return write_gltf(path, t, 1);
	if (ext_is(path, ".gltf")) return write_gltf(path, t, 0);
	return write_obj(path, t);
}

static void describe_tmd(const psxrom_tmd *t)
{
	size_t total = 0;
	for (size_t o = 0; o < t->num_objects; o++) total += t->objects[o].num_tris;
	printf("TMD %zu object(s), %zu triangle(s)\n", t->num_objects, total);
}

static int has_tmd_ext(const char *s)
{
	return s[0] == '.' && toupper((unsigned char)s[1]) == 'T' &&
		   toupper((unsigned char)s[2]) == 'M' && toupper((unsigned char)s[3]) == 'D';
}

static int cb_tmd(const psxrom_entry *e, void *u)
{
	struct out_ctx *c = u;
	size_t len = strlen(e->path);
	if (e->is_dir || len < 4 || !has_tmd_ext(e->path + len - 4)) return 0;
	uint8_t *data; size_t n;
	psxrom_tmd t;
	psxrom_result r = psxrom_read_file(c->d, e, &data, &n);
	if (r == PSXROM_OK) {
		r = psxrom_tmd_parse(data, n, &t);
		free(data);
	}
	if (r == PSXROM_OK) {
		char out[2048];
		snprintf(out, sizeof out, "%s%s.%s", c->dir, e->path, c->ext);
		mkdirs_for(out);
		if (write_model(out, &t)) printf("%s\n", out);
		else r = PSXROM_ERR_IO;
		psxrom_tmd_free(&t);
	}
	if (r != PSXROM_OK) { fprintf(stderr, "%s: %s\n", e->path, psxrom_strerror(r)); c->errors++; }
	return 0;
}

static int media_command(psxrom_disc *d, int argc, char **argv, psxrom_result *r)
{
	const char *cmd = argv[2];
	if (!strcmp(cmd, "xa-list")) {
		uint32_t lba, count;
		psxrom_xa_stream *streams;
		size_t n;
		*r = sector_range(d, argc >= 4 ? argv[3] : NULL, &lba, &count);
		if (*r == PSXROM_OK) *r = psxrom_xa_scan(d, lba, count, &streams, &n);
		if (*r == PSXROM_OK) {
			for (size_t i = 0; i < n; i++) print_xa_stream(&streams[i]);
			free(streams);
		}
	} else if (!strcmp(cmd, "tmd") && argc >= 4) {
		psxrom_entry e;
		uint8_t *data; size_t n;
		if ((*r = psxrom_find(d, argv[3], &e)) == PSXROM_OK &&
			(*r = psxrom_read_file(d, &e, &data, &n)) == PSXROM_OK) {
			psxrom_tmd t;
			if ((*r = psxrom_tmd_parse(data, n, &t)) == PSXROM_OK) {
				describe_tmd(&t);
				if (argc >= 5 && !write_model(argv[4], &t)) *r = PSXROM_ERR_IO;
				psxrom_tmd_free(&t);
			}
			free(data);
		}
	} else if (!strcmp(cmd, "tmd-all") && argc >= 4) {
		struct out_ctx c = {d, argv[3], 0, argc >= 5 ? argv[4] : "glb"};
		MKDIR(argv[3]);
		*r = psxrom_walk(d, cb_tmd, &c);
		if (c.errors) *r = PSXROM_ERR_FORMAT;
	} else if (!strcmp(cmd, "xa") && argc >= 7) {
		uint32_t lba, count;
		psxrom_xa_pcm pcm;
		*r = sector_range(d, argv[3], &lba, &count);
		if (*r == PSXROM_OK)
			*r = psxrom_xa_decode(d, lba, count, (uint8_t)strtoul(argv[4], NULL, 0),
								  (uint8_t)strtoul(argv[5], NULL, 0), &pcm);
		if (*r == PSXROM_OK) {
			printf("%zu frames, %u channel(s), %u Hz\n", pcm.frames, pcm.channels, pcm.sample_rate);
			if (!write_wav(argv[6], &pcm)) *r = PSXROM_ERR_IO;
			psxrom_xa_pcm_free(&pcm);
		}
	} else if (!strcmp(cmd, "xa-all") && argc >= 5) {
		*r = cmd_xa_all(d, argv[3], argv[4]);
	} else if (!strcmp(cmd, "str-info") && argc >= 4) {
		*r = cmd_str_info(d, argv[3]);
	} else if (!strcmp(cmd, "str-frame") && argc >= 6) {
		*r = cmd_str_frames(d, argv[3], argv[5], 1, (uint32_t)strtoul(argv[4], NULL, 0), 1);
	} else if (!strcmp(cmd, "str-y4m") && argc >= 5) {
		*r = cmd_str_y4m(d, argv[3], argv[4]);
	} else if (!strcmp(cmd, "str-convert") && argc >= 5) {
		*r = cmd_str_convert(d, argv[3], argv[4]);
	} else if (!strcmp(cmd, "str-frames") && argc >= 5) {
		uint32_t first = argc >= 6 ? (uint32_t)strtoul(argv[5], NULL, 0) : 0;
		uint32_t num = argc >= 7 ? (uint32_t)strtoul(argv[6], NULL, 0) : 0xFFFFFFFFu;
		*r = cmd_str_frames(d, argv[3], argv[4], 0, first, num);
	} else {
		return 0;
	}
	return 1;
}

int main(int argc, char **argv)
{
	if (argc < 3) { usage(); return 2; }
	const char *cmd = argv[2];
	psxrom_disc *d;
	psxrom_result r = psxrom_disc_open(argv[1], &d);
	if (r != PSXROM_OK) { fprintf(stderr, "%s: %s\n", argv[1], psxrom_strerror(r)); return 1; }

	int rc = 0;
	if (!strcmp(cmd, "info")) {
		char label[64] = "";
		psxrom_volume_label(d, label, sizeof label);
		printf("label:   %s\nsectors: %u\n", label, psxrom_disc_sector_count(d));
		psxrom_entry e;
		if (psxrom_find(d, "SYSTEM.CNF", &e) == PSXROM_OK) {
			uint8_t *data; size_t n;
			if (psxrom_read_file(d, &e, &data, &n) == PSXROM_OK) {
				data[n] = 0;
				printf("SYSTEM.CNF:\n%s\n", (char *)data);
				free(data);
			}
		}
	} else if (!strcmp(cmd, "ls")) {
		r = psxrom_walk(d, cb_ls, NULL);
	} else if (!strcmp(cmd, "hex") && argc >= 4) {
		psxrom_entry e;
		uint8_t *data; size_t n;
		if ((r = psxrom_find(d, argv[3], &e)) == PSXROM_OK &&
			(r = psxrom_read_file(d, &e, &data, &n)) == PSXROM_OK) {
			size_t count = argc >= 5 ? strtoul(argv[4], NULL, 0) : 256;
			if (count > n) count = n;
			for (size_t i = 0; i < count; i += 16) {
				printf("%08zx  ", i);
				for (size_t j = 0; j < 16; j++) {
					if (i + j < count) printf("%02x ", data[i + j]); else printf("   ");
				}
				putchar(' ');
				for (size_t j = 0; j < 16 && i + j < count; j++)
					putchar(isprint(data[i + j]) ? data[i + j] : '.');
				putchar('\n');
			}
			free(data);
		}
	} else if (!strcmp(cmd, "extract") && argc >= 5) {
		psxrom_entry e;
		uint8_t *data; size_t n;
		if ((r = psxrom_find(d, argv[3], &e)) == PSXROM_OK &&
			(r = psxrom_read_file(d, &e, &data, &n)) == PSXROM_OK) {
			if (!write_file(argv[4], data, n)) r = PSXROM_ERR_IO;
			free(data);
		}
	} else if (!strcmp(cmd, "extract-all") && argc >= 4) {
		struct out_ctx c = {d, argv[3], 0};
		MKDIR(argv[3]);
		r = psxrom_walk(d, cb_extract, &c);
		if (c.errors) rc = 1;
	} else if (!strcmp(cmd, "tims") || (!strcmp(cmd, "tim-all") && argc >= 4)) {
		struct out_ctx c = {d, argc >= 4 ? argv[3] : NULL, 0};
		if (c.dir) MKDIR(c.dir);
		r = psxrom_walk(d, cb_tims, &c);
		if (c.errors) rc = 1;
	} else if (!strcmp(cmd, "tim") && argc >= 5) {
		psxrom_entry e;
		uint8_t *data; size_t n;
		if ((r = psxrom_find(d, argv[3], &e)) == PSXROM_OK &&
			(r = psxrom_read_file(d, &e, &data, &n)) == PSXROM_OK) {
			psxrom_tim t;
			uint8_t *rgba;
			if ((r = psxrom_tim_parse(data, n, &t)) == PSXROM_OK) {
				describe_tim(&t);
				uint32_t clut = argc >= 6 ? (uint32_t)strtoul(argv[5], NULL, 0) : 0;
				if ((r = psxrom_tim_to_rgba(&t, clut, &rgba)) == PSXROM_OK) {
					if (!write_bmp(argv[4], rgba, t.width, t.height)) r = PSXROM_ERR_IO;
					free(rgba);
				}
			}
			free(data);
		}
	} else if (!media_command(d, argc, argv, &r)) {
		usage();
		psxrom_disc_close(d);
		return 2;
	}

	if (r != PSXROM_OK) { fprintf(stderr, "error: %s\n", psxrom_strerror(r)); rc = 1; }
	psxrom_disc_close(d);
	return rc;
}
