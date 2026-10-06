#include "mixer.h"

#include <stdlib.h>
#include <string.h>

#include "platform.h"

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "../third_party/stb_vorbis.c"

#define MAX_SOUNDS 32

typedef struct {
	int16_t *pcm; /* mono */
	int len;      /* frames */
	int rate;     /* original sample rate */
	/* playback state (one voice per sound, like a DirectSound buffer) */
	int playing, loop;
	double pos;
	int freq; /* 0 = original */
	int pan;  /* -10000 .. 10000 */
} Sound;

static Sound g_sounds[MAX_SOUNDS];
static int g_nsounds;

static stb_vorbis *g_music;
static unsigned char *g_music_data;
static int g_music_loop;
static int g_music_channels;
static float g_music_buf[4096];
static int g_music_buf_len, g_music_buf_pos; /* in frames */
static double g_music_pos, g_music_step;

void mixer_init(void)
{
	memset(g_sounds, 0, sizeof(g_sounds));
	g_nsounds = 0;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static unsigned char *read_all(FILE *f, long *size)
{
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *buf = malloc(*size ? *size : 1);
	if (fread(buf, 1, *size, f) != (size_t)*size) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

int snd_load_wav(const char *name)
{
	if (g_nsounds >= MAX_SOUNDS) {
		return -1;
	}
	FILE *f = plat_open_data(name, "rb");
	if (!f) {
		fprintf(stderr, "snd: cannot open %s\n", name);
		return -1;
	}
	long size;
	uint8_t *buf = read_all(f, &size);
	if (!buf || size < 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) {
		free(buf);
		return -1;
	}
	int channels = 1, rate = 22050, bits = 16;
	const uint8_t *data = NULL;
	uint32_t datalen = 0;
	for (long p = 12; p + 8 <= size;) {
		uint32_t len = rd32(buf + p + 4);
		if (!memcmp(buf + p, "fmt ", 4) && p + 24 <= size) {
			channels = rd16(buf + p + 10);
			rate = (int)rd32(buf + p + 12);
			bits = rd16(buf + p + 22);
		}
		else if (!memcmp(buf + p, "data", 4)) {
			data = buf + p + 8;
			datalen = len;
			if ((long)(p + 8 + datalen) > size) {
				datalen = (uint32_t)(size - p - 8);
			}
		}
		p += 8 + len + (len & 1);
	}
	if (!data || channels < 1 || (bits != 8 && bits != 16)) {
		free(buf);
		return -1;
	}
	int bps = bits / 8 * channels;
	int frames = (int)(datalen / bps);
	int16_t *pcm = malloc(sizeof(int16_t) * (frames ? frames : 1));
	for (int i = 0; i < frames; i++) {
		int acc = 0;
		for (int c = 0; c < channels; c++) {
			const uint8_t *s = data + i * bps + c * (bits / 8);
			acc += bits == 8 ? ((int)s[0] - 128) << 8 : (int16_t)rd16(s);
		}
		pcm[i] = (int16_t)(acc / channels);
	}
	free(buf);
	mixer_lock();
	int h = g_nsounds++;
	Sound *s = &g_sounds[h];
	memset(s, 0, sizeof(*s));
	s->pcm = pcm;
	s->len = frames;
	s->rate = rate;
	mixer_unlock();
	return h;
}

void snd_play(int h, int loop)
{
	if (h < 0 || h >= g_nsounds) {
		return;
	}
	mixer_lock();
	g_sounds[h].playing = 1;
	g_sounds[h].loop = loop;
	g_sounds[h].pos = 0.0;
	mixer_unlock();
}

void snd_stop(int h)
{
	if (h < 0 || h >= g_nsounds) {
		return;
	}
	mixer_lock();
	g_sounds[h].playing = 0;
	mixer_unlock();
}

void snd_set_pan(int h, int pan)
{
	if (h >= 0 && h < g_nsounds) {
		g_sounds[h].pan = pan < -10000 ? -10000 : pan > 10000 ? 10000 : pan;
	}
}

void snd_set_freq(int h, int hz)
{
	if (h >= 0 && h < g_nsounds) {
		g_sounds[h].freq = hz;
	}
}

void mus_play(const char *name, int loop)
{
	mus_stop();
	FILE *f = plat_open_data(name, "rb");
	if (!f) {
		fprintf(stderr, "mus: cannot open %s\n", name);
		return;
	}
	long size;
	unsigned char *data = read_all(f, &size);
	if (!data) {
		return;
	}
	int err;
	stb_vorbis *v = stb_vorbis_open_memory(data, (int)size, &err, NULL);
	if (!v) {
		free(data);
		return;
	}
	stb_vorbis_info info = stb_vorbis_get_info(v);
	mixer_lock();
	g_music = v;
	g_music_data = data;
	g_music_loop = loop;
	g_music_channels = info.channels;
	g_music_buf_len = g_music_buf_pos = 0;
	g_music_pos = 0.0;
	g_music_step = (double)info.sample_rate / MIXER_RATE;
	mixer_unlock();
}

void mus_stop(void)
{
	mixer_lock();
	stb_vorbis *v = g_music;
	unsigned char *d = g_music_data;
	g_music = NULL;
	g_music_data = NULL;
	mixer_unlock();
	if (v) {
		stb_vorbis_close(v);
	}
	free(d);
}

/* Fetches the next decoded music frame (stereo), refilling from the decoder. */
static int music_frame(float *l, float *r)
{
	while (g_music_buf_pos >= g_music_buf_len) {
		int n = stb_vorbis_get_samples_float_interleaved(g_music, 2, g_music_buf, 4096);
		if (n <= 0) {
			if (!g_music_loop) {
				return 0;
			}
			stb_vorbis_seek_start(g_music);
			continue;
		}
		g_music_buf_len = n;
		g_music_buf_pos = 0;
	}
	*l = g_music_buf[g_music_buf_pos * 2];
	*r = g_music_buf[g_music_buf_pos * 2 + 1];
	return 1;
}

void mixer_render(int16_t *out, int frames)
{
	static int32_t acc[4096 * 2];
	if (frames > 4096) {
		frames = 4096;
	}
	memset(acc, 0, sizeof(int32_t) * frames * 2);
	mixer_lock();
	for (int h = 0; h < g_nsounds; h++) {
		Sound *s = &g_sounds[h];
		if (!s->playing || !s->len) {
			continue;
		}
		double step = (double)(s->freq > 0 ? s->freq : s->rate) / MIXER_RATE;
		/* DirectSound pan attenuates the opposite side in hundredths of dB. */
		float lg = 1.0f, rg = 1.0f;
		if (s->pan > 0) {
			lg = powf(10.0f, -s->pan / 2000.0f);
		}
		else if (s->pan < 0) {
			rg = powf(10.0f, s->pan / 2000.0f);
		}
		int li = (int)(lg * 256), ri = (int)(rg * 256);
		for (int i = 0; i < frames; i++) {
			int p = (int)s->pos;
			if (p >= s->len) {
				if (!s->loop) {
					s->playing = 0;
					break;
				}
				s->pos -= s->len;
				p = (int)s->pos;
			}
			int v = s->pcm[p];
			acc[i * 2] += v * li >> 8;
			acc[i * 2 + 1] += v * ri >> 8;
			s->pos += step;
		}
	}
	if (g_music) {
		for (int i = 0; i < frames; i++) {
			float l, r;
			if (!music_frame(&l, &r)) {
				break;
			}
			acc[i * 2] += (int32_t)(l * 26000.0f);
			acc[i * 2 + 1] += (int32_t)(r * 26000.0f);
			g_music_pos += g_music_step;
			while (g_music_pos >= 1.0) {
				g_music_pos -= 1.0;
				g_music_buf_pos++;
			}
		}
	}
	mixer_unlock();
	for (int i = 0; i < frames * 2; i++) {
		int32_t v = acc[i];
		out[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
	}
}
