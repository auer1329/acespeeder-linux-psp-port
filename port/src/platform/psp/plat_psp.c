/* PSP backend: sceGu rendering, sceCtrl input, pspaudiolib output. */
#include <malloc.h>
#include <math.h>
#include <pspaudiolib.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspdmac.h>
#include <psputility.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../../engine/prof.h"
#include "../clip.h"
#include "../mixer.h"
#include "../platform.h"

PSP_MODULE_INFO("AceSpeeder", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
/* A race needs about 9 MB. The rest of user RAM stays free for the system's
 * dialogs (save data), which load their code and images into it on a real
 * PSP and quit at once when they cannot (PPSSPP does not need it). */
PSP_HEAP_SIZE_KB(14 * 1024);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define VIRT_W 640
#define VIRT_H 480

static unsigned int __attribute__((aligned(16))) g_list[256 * 1024];
/* Triple buffering: one buffer on screen, one queued for the next vertical
 * blank, one being drawn, so the CPU never waits for the display. */
static void *g_fb[3], *g_zbp;
static int g_draw_idx;
static void *g_draw_buf; /* the buffer sceGu draws into this frame */
static uint32_t g_gpu_wait;
static int g_quit;
static SceUID g_audio_sema = -1;

/* ------------------------------------------------------------- callbacks */

static int exit_callback(int a, int b, void *c)
{
	(void)a, (void)b, (void)c;
	g_quit = 1;
	return 0;
}

static int callback_thread(SceSize args, void *argp)
{
	(void)args, (void)argp;
	int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);
	sceKernelSleepThreadCB();
	return 0;
}

/* ------------------------------------------------------------------ files */

/* Debug output (logs, screenshots) goes to a save/ folder next to the EBOOT.
 * sceIoMkdir does not resolve relative paths on the console, newlib's mkdir
 * does (through the working directory). */
static void make_save_dir(void)
{
	mkdir("save", 0777);
}

/* The data sits next to EBOOT.PBP (the working directory), or in a data/
 * folder there. */
FILE *plat_open_data(const char *name, const char *mode)
{
	FILE *f = fopen(name, mode);
	if (!f) {
		char p[256];
		snprintf(p, sizeof(p), "data/%.200s", name);
		f = fopen(p, mode);
	}
	return f;
}

/* The game's save files (gamedata.dat, settings.txt, replays) live in memory
 * and reach the Memory Stick through the system's save data dialogs (see
 * plat_save_load and plat_save_commit at the end of this file). */
#define SAVE_MAX_FILES 64

typedef struct {
	char name[32];
	char *data;
	size_t size;
} SaveFile;

static SaveFile g_save_files[SAVE_MAX_FILES];
static int g_nsave_files;

static SaveFile *save_file(const char *name, int create)
{
	for (int i = 0; i < g_nsave_files; i++) {
		if (!strcmp(g_save_files[i].name, name)) {
			return &g_save_files[i];
		}
	}
	if (!create || g_nsave_files == SAVE_MAX_FILES || strlen(name) >= sizeof(g_save_files[0].name)) {
		return NULL;
	}
	SaveFile *f = &g_save_files[g_nsave_files++];
	memset(f, 0, sizeof(*f));
	strcpy(f->name, name);
	return f;
}

static int is_debug_file(const char *name)
{
	size_t n = strlen(name);
	return n > 4 && !strcmp(name + n - 4, ".log");
}

FILE *plat_open_save(const char *name, const char *mode)
{
	if (is_debug_file(name)) {
		char p[256];
		snprintf(p, sizeof(p), "save/%.200s", name);
		return fopen(p, mode);
	}
	if (mode[0] == 'r') {
		SaveFile *f = save_file(name, 0);
		if (f && f->size) {
			return fmemopen(f->data, f->size, "r");
		}
		return plat_open_data(name, mode); /* shipped defaults (replays, gamedata.dat) */
	}
	SaveFile *f = save_file(name, 1);
	if (!f) {
		return NULL;
	}
	free(f->data);
	f->data = NULL;
	f->size = 0;
	return open_memstream(&f->data, &f->size);
}

/* ------------------------------------------------------------------ audio */

void mixer_lock(void)
{
	if (g_audio_sema >= 0) {
		sceKernelWaitSema(g_audio_sema, 1, NULL);
	}
}

void mixer_unlock(void)
{
	if (g_audio_sema >= 0) {
		sceKernelSignalSema(g_audio_sema, 1);
	}
}

static void audio_cb(void *buf, unsigned int frames, void *ud)
{
	(void)ud;
	mixer_render((int16_t *)buf, (int)frames);
}

/* -------------------------------------------------------------- lifecycle */

static void *vram_static(unsigned int size)
{
	static unsigned int next = 0;
	void *p = (void *)next;
	next += size;
	return p;
}

int plat_init(void)
{
	SceUID th = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
	if (th >= 0) {
		sceKernelStartThread(th, 0, 0);
	}
	scePowerSetClockFrequency(333, 333, 166);
	make_save_dir();

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

	for (int i = 0; i < 3; i++) {
		g_fb[i] = vram_static(BUF_W * SCR_H * 2);
	}
	g_zbp = vram_static(BUF_W * SCR_H * 2);

	sceGuInit();
	sceGuStart(GU_DIRECT, g_list);
	sceGuDrawBuffer(GU_PSM_5650, g_fb[0], BUF_W);
	g_draw_idx = 0;
	g_draw_buf = g_fb[0];
	sceGuDispBuffer(SCR_W, SCR_H, g_fb[2], BUF_W);
	sceGuDepthBuffer(g_zbp, BUF_W);
	sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
	sceGuViewport(2048, 2048, SCR_W, SCR_H);
	sceGuDepthRange(65535, 0);
	sceGuScissor(0, 0, SCR_W, SCR_H);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuDepthFunc(GU_GEQUAL);
	sceGuEnable(GU_DEPTH_TEST);
	sceGuFrontFace(GU_CW);
	sceGuShadeModel(GU_SMOOTH);
	sceGuEnable(GU_CLIP_PLANES);
	sceGuEnable(GU_DITHER);
	sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexWrap(GU_REPEAT, GU_REPEAT);
	sceGuAlphaFunc(GU_GREATER, 0x80, 0xff);
	sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
	sceGuLightMode(GU_SINGLE_COLOR);
	sceGuColorMaterial(GU_AMBIENT | GU_DIFFUSE);
	sceGuFinish();
	sceGuSync(0, 0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);

	mixer_init();
	g_audio_sema = sceKernelCreateSema("mixer", 0, 1, 1, NULL);
	pspAudioInit();
	pspAudioSetChannelCallback(0, audio_cb, NULL);
	return 0;
}

void plat_shutdown(void)
{
	pspAudioEnd();
	sceGuTerm();
	sceKernelExitGame();
}

/* 16.5 per vertical blank: a 33 ms logic tick every second blank (29.97 Hz
 * against the original's 30.3), so the motion is even at 30 frames a second. */
uint32_t plat_ticks_ms(void)
{
	return (uint32_t)(sceDisplayGetVcount() * 33u / 2u);
}

uint32_t plat_time_us(void)
{
	return (uint32_t)sceKernelGetSystemTimeLow();
}

void plat_sleep_ms(int ms)
{
	sceKernelDelayThread((SceUInt)ms * 1000);
}

int plat_should_quit(void)
{
	return g_quit;
}

/* ------------------------------------------------------------------ input */

/* Cross: accelerate and confirm. Circle: cancel. Start: pause in a race,
 * confirm elsewhere (see main.c). Triangle steps through the four chase views
 * (F2, F3, F4, F1 on the PC). Select toggles the performance meter. */
void plat_read_input(int32_t keys[IN_COUNT])
{
	static const int views[4] = {IN_F2, IN_F3, IN_F4, IN_F1};
	static int view, triangle_was_down;
	SceCtrlData pad;
	sceCtrlPeekBufferPositive(&pad, 1);
	unsigned int b = pad.Buttons;
	if ((b & PSP_CTRL_UP) || pad.Ly < 128 - 50) keys[IN_UP] = 1;
	if ((b & PSP_CTRL_DOWN) || pad.Ly > 128 + 50) keys[IN_DOWN] = 1;
	if ((b & PSP_CTRL_RIGHT) || pad.Lx > 128 + 50) keys[IN_RIGHT] = 1;
	if ((b & PSP_CTRL_LEFT) || pad.Lx < 128 - 50) keys[IN_LEFT] = 1;
	if (b & PSP_CTRL_CROSS) keys[IN_A] = 1;
	if (b & PSP_CTRL_CIRCLE) keys[IN_B] = 1;
	if (b & PSP_CTRL_START) keys[IN_START] = 1;
	if ((b & PSP_CTRL_SELECT) && !(b & PSP_CTRL_START)) keys[IN_PERF] = 1;
	if ((b & PSP_CTRL_TRIANGLE) && !triangle_was_down) {
		view = (view + 1) % 4;
		keys[views[view]] = 1;
	}
	triangle_was_down = (b & PSP_CTRL_TRIANGLE) != 0;
	if ((b & PSP_CTRL_SELECT) && (b & PSP_CTRL_START)) keys[IN_ESC] = 1;
}

/* --------------------------------------------------------------- graphics */

#define MAX_TEX 512

typedef struct {
	void *data;      /* swizzled 16-bit texels */
	int psm;         /* GU_PSM_5551 (3D) or GU_PSM_4444 (2D) */
	int w, h;        /* buffer size (power of two) */
	int img_w, img_h;/* original image size */
	float ku, kv;    /* image texel -> buffer texel, for 2D quads */
	int used;
} PspTex;

static PspTex g_tex[MAX_TEX];
static int g_bound = -1;
static int g_in_frame;

int gfx_screen_w(void) { return VIRT_W; }
int gfx_screen_h(void) { return VIRT_H; }
float gfx_pixel_aspect(void) { return ((float)SCR_W / SCR_H) / ((float)VIRT_W / VIRT_H); }

static void *vram_abs(void *rel)
{
	return (char *)sceGeEdramGetAddr() + (unsigned int)rel;
}

/* whether the display shows or is about to show this buffer */
static int on_display(void *rel)
{
	void *addr;
	int bw, fmt;
	unsigned int target = (unsigned int)vram_abs(rel) & 0x0fffffff;
	sceDisplayGetFrameBuf(&addr, &bw, &fmt, PSP_DISPLAY_SETBUF_IMMEDIATE);
	if (((unsigned int)addr & 0x0fffffff) == target) {
		return 1;
	}
	sceDisplayGetFrameBuf(&addr, &bw, &fmt, PSP_DISPLAY_SETBUF_NEXTFRAME);
	return ((unsigned int)addr & 0x0fffffff) == target;
}

void gfx_begin_frame(void)
{
	/* only when frames come faster than the display shows them */
	for (int i = 0; i < 3 && on_display(g_draw_buf); i++) {
		sceDisplayWaitVblankStart();
	}
	sceGuStart(GU_DIRECT, g_list);
	g_in_frame = 1;
	sceGuDrawBufferList(GU_PSM_5650, g_draw_buf, BUF_W);
	sceGuScissor(0, 0, SCR_W, SCR_H);
	sceGuClearColor(0xff000000);
	sceGuClearDepth(0);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void gfx_end_frame(void)
{

	sceGuFinish();
	uint32_t t0 = sceKernelGetSystemTimeLow();
	sceGuSync(0, 0);
	g_gpu_wait = sceKernelGetSystemTimeLow() - t0;
	g_in_frame = 0;
	sceDisplaySetFrameBuf(vram_abs(g_draw_buf), BUF_W, PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);
	g_draw_idx = (g_draw_idx + 1) % 3;
	g_draw_buf = g_fb[g_draw_idx];
}

uint32_t gfx_gpu_wait_us(void)
{
	return g_gpu_wait;
}

static void save_ppm(const char *path, const uint16_t *px)
{
	FILE *f = fopen(path, "wb");
	if (!f) {
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
	unsigned char row[SCR_W * 3];
	for (int y = 0; y < SCR_H; y++) {
		for (int x = 0; x < SCR_W; x++) {
			uint16_t c = px[y * BUF_W + x];
			row[x * 3] = (unsigned char)((c & 0x1f) << 3);
			row[x * 3 + 1] = (unsigned char)((c >> 5 & 0x3f) << 2);
			row[x * 3 + 2] = (unsigned char)((c >> 11 & 0x1f) << 3);
		}
		fwrite(row, 1, sizeof(row), f);
	}
	fclose(f);
}

void plat_screenshot(const char *path)
{
	/* Read the frame being drawn after the GPU is done with it. The copy goes
	 * through the DMA controller: emulators such as PPSSPP keep framebuffers on
	 * the host GPU and only write them back to VRAM for copies they can see. */
	sceGuFinish();
	sceGuSync(0, 0);
	size_t size = sizeof(uint16_t) * BUF_W * SCR_H;
	uint16_t *buf = memalign(64, size);
	if (buf) {
		sceKernelDcacheWritebackInvalidateRange(buf, size);
		sceDmacMemcpy(buf, (char *)sceGeEdramGetAddr() + (unsigned int)g_draw_buf, size);
		save_ppm(path, buf);
		free(buf);
	}
	sceGuStart(GU_DIRECT, g_list);
}

static void virt_rect(int x, int y, int w, int h, int *sx, int *sy, int *sw, int *sh)
{
	*sx = x * SCR_W / VIRT_W;
	*sy = y * SCR_H / VIRT_H;
	*sw = (x + w) * SCR_W / VIRT_W - *sx;
	*sh = (y + h) * SCR_H / VIRT_H - *sy;
}

void gfx_set_viewport(int x, int y, int w, int h)
{
	int sx, sy, sw, sh;
	virt_rect(x, y, w, h, &sx, &sy, &sw, &sh);
	sceGuViewport(2048 - SCR_W / 2 + sx + sw / 2, 2048 - SCR_H / 2 + sy + sh / 2, sw, sh);
	sceGuScissor(sx, sy, sx + sw, sy + sh);
}

void gfx_clear(uint32_t rgb, int color, int depth)
{
	uint32_t abgr = 0xff000000 | (rgb >> 16 & 0xff) | (rgb & 0xff00) | (rgb & 0xff) << 16;
	sceGuClearColor(abgr);
	sceGuClearDepth(0);
	sceGuClear((color ? GU_COLOR_BUFFER_BIT : 0) | (depth ? GU_DEPTH_BUFFER_BIT : 0));
}

/* Kept for the software clipping before 3D draws (see draw_clipped). */
static float g_mv[4][4];
static float g_fx = 1.0f, g_fy = 1.0f, g_near = 1.0f, g_far = 1000.0f;

/* The game's near plane is 1 with the far plane at 20000. The PSP's 16-bit
 * depth buffer then cannot separate surfaces a unit or so apart a few hundred
 * units away, and track pieces overlapping at junctions show through each
 * other. Nothing is drawn closer than about 8 units from the camera (measured
 * in replays and the chase views), so 8 gives 8 times the depth precision. */
#define MIN_NEAR 8.0f

void gfx_set_projection(float fx, float fy, float n, float f)
{
	if (n < MIN_NEAR) {
		n = MIN_NEAR;
	}
	g_fx = fx, g_fy = fy, g_near = n, g_far = f;
	ScePspFMatrix4 m;
	memset(&m, 0, sizeof(m));
	m.x.x = 1.0f / fx;
	m.y.y = 1.0f / fy;
	m.z.z = (f + n) / (f - n);
	m.z.w = 1.0f;
	m.w.z = -2.0f * f * n / (f - n);
	sceGumMatrixMode(GU_PROJECTION);
	sceGumLoadMatrix(&m);
	sceGumMatrixMode(GU_VIEW);
	sceGumLoadIdentity();
	sceGumMatrixMode(GU_MODEL);
}

void gfx_set_modelview(const float m[4][4])
{
	/* Same memory layout as GL (see the Linux backend). */
	memcpy(g_mv, m, sizeof(g_mv));
	sceGumMatrixMode(GU_MODEL);
	sceGumLoadMatrix((const ScePspFMatrix4 *)m);
}

static uint32_t abgr_from(GfxColor c)
{
#define C8(x) ((uint32_t)((x) <= 0.0f ? 0 : (x) >= 1.0f ? 255 : (int)((x) * 255.0f)))
	return C8(c.a) << 24 | C8(c.b) << 16 | C8(c.g) << 8 | C8(c.r);
#undef C8
}

void gfx_set_lighting(int on)
{
	if (on) {
		sceGuEnable(GU_LIGHTING);
	}
	else {
		sceGuDisable(GU_LIGHTING);
	}
}

void gfx_set_lights(GfxColor ambient, const GfxLight *lights, int count)
{
	ambient.a = 1.0f;
	sceGuAmbient(abgr_from(ambient));
	for (int i = 0; i < 4; i++) {
		if (i >= count) {
			sceGuDisable(GU_LIGHT0 + i);
			continue;
		}
		const GfxLight *l = &lights[i];
		ScePspFVector3 v;
		int type;
		if (l->type == GFX_LIGHT_DIRECTIONAL) {
			v.x = -l->dir[0], v.y = -l->dir[1], v.z = -l->dir[2];
			type = GU_DIRECTIONAL;
		}
		else {
			v.x = l->pos[0], v.y = l->pos[1], v.z = l->pos[2];
			type = GU_POINTLIGHT;
		}
		sceGuLight(i, type, GU_DIFFUSE_AND_SPECULAR, &v);
		GfxColor c = l->color;
		c.a = 1.0f;
		sceGuLightColor(i, GU_DIFFUSE, abgr_from(c));
		sceGuLightColor(i, GU_SPECULAR, abgr_from(c));
		sceGuLightAtt(i, l->att0 > 0 ? l->att0 : 1.0f, l->att1, l->att2);
		sceGuEnable(GU_LIGHT0 + i);
	}
}

void gfx_set_material(GfxColor diffuse, GfxColor emissive, GfxColor specular, float power)
{
	(void)diffuse;
	emissive.a = specular.a = 1.0f;
	sceGuModelColor(abgr_from(emissive), 0xffffffff, 0xffffffff, abgr_from(specular));
	sceGuSpecular(power > 0.0f ? power : 1.0f);
}

void gfx_set_fog(int on, uint32_t rgb, float start, float end)
{
	if (!on) {
		sceGuDisable(GU_FOG);
		return;
	}
	uint32_t abgr = 0xff000000 | (rgb >> 16 & 0xff) | (rgb & 0xff00) | (rgb & 0xff) << 16;
	sceGuFog(start, end, abgr);
	sceGuEnable(GU_FOG);
}

void gfx_set_depth(int test, int write)
{
	if (test) {
		sceGuEnable(GU_DEPTH_TEST);
	}
	else {
		sceGuDisable(GU_DEPTH_TEST);
	}
	sceGuDepthMask(write ? GU_FALSE : GU_TRUE); /* GU mask: TRUE disables writes */
}

void gfx_set_depth_offset(int on)
{
	/* depth units of 65536, larger is nearer (sceGuDepthRange(65535, 0)) */
	sceGuDepthOffset(on ? 4 : 0);
}

void gfx_set_cull(int on)
{
	if (on) {
		sceGuEnable(GU_CULL_FACE);
	}
	else {
		sceGuDisable(GU_CULL_FACE);
	}
}

void gfx_set_blend(int on)
{
	if (on) {
		sceGuEnable(GU_BLEND);
		if (on == GFX_BLEND_ADD) {
			sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffffff, 0xffffffff);
		}
		else {
			sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
		}
	}
	else {
		sceGuDisable(GU_BLEND);
	}
}

void gfx_set_alpha_test(int on)
{
	if (on) {
		sceGuEnable(GU_ALPHA_TEST);
	}
	else {
		sceGuDisable(GU_ALPHA_TEST);
	}
}

static int pow2(int v)
{
	int p = 8;
	while (p < v && p < 512) {
		p <<= 1;
	}
	return p;
}

/* 16-bit texels in 16-byte x 8-row blocks. */
static void swizzle16(uint16_t *out, const uint16_t *in, int w, int h)
{
	int row_bytes = w * 2;
	int blocks_x = row_bytes / 16;
	const uint8_t *src = (const uint8_t *)in;
	uint8_t *dst = (uint8_t *)out;
	for (int by = 0; by < h / 8; by++) {
		for (int bx = 0; bx < blocks_x; bx++) {
			for (int y = 0; y < 8; y++) {
				memcpy(dst, src + (by * 8 + y) * row_bytes + bx * 16, 16);
				dst += 16;
			}
		}
	}
}

/* 2D images (HUD bitmaps, the font) are drawn in 640x480 coordinates onto the
 * 480x272 screen. Shrunk on the fly with point sampling they lose whole rows
 * and columns, so they are shrunk once here with a box filter instead, keeping
 * 4-bit alpha for antialiased edges; colours are averaged over the opaque part
 * so transparent (colour-keyed) texels do not darken the edges. */
static uint16_t *downscale_2d(int w, int h, const uint32_t *rgba, int *ow, int *oh, int *tw, int *th)
{
	const float fx = (float)SCR_W / VIRT_W, fy = (float)SCR_H / VIRT_H;
	int dw = (int)ceilf(w * fx), dh = (int)ceilf(h * fy);
	*ow = dw, *oh = dh;
	*tw = pow2(dw), *th = pow2(dh);
	uint16_t *out = calloc((size_t)*tw * *th, sizeof(uint16_t));
	if (!out) {
		return NULL;
	}
	for (int y = 0; y < dh; y++) {
		float y0 = y / fy, y1 = (y + 1) / fy;
		for (int x = 0; x < dw; x++) {
			float x0 = x / fx, x1 = (x + 1) / fx;
			float sum_w = 0, sum_a = 0, sr = 0, sg = 0, sb = 0;
			for (int sy = (int)y0; sy < h && sy < y1; sy++) {
				float wy = fminf(y1, sy + 1.0f) - fmaxf(y0, (float)sy);
				for (int sx = (int)x0; sx < w && sx < x1; sx++) {
					float wgt = wy * (fminf(x1, sx + 1.0f) - fmaxf(x0, (float)sx));
					uint32_t c = rgba[sy * w + sx];
					float a = (float)(c >> 24) * wgt;
					sum_w += wgt;
					sum_a += a;
					sr += (float)(c & 0xff) * a;
					sg += (float)(c >> 8 & 0xff) * a;
					sb += (float)(c >> 16 & 0xff) * a;
				}
			}
			if (sum_w <= 0.0f || sum_a <= 0.0f) {
				continue;
			}
			/* thin strokes cover only part of a screen pixel: strengthen
			 * partial coverage so they stay visible */
			int a = (int)(255.0f * sqrtf(sum_a / sum_w / 255.0f) + 0.5f);
			int r = (int)(sr / sum_a + 0.5f), g = (int)(sg / sum_a + 0.5f), b = (int)(sb / sum_a + 0.5f);
			out[y * *tw + x] = (uint16_t)((a >> 4) << 12 | (b >> 4) << 8 | (g >> 4) << 4 | (r >> 4));
		}
	}
	return out;
}

int gfx_texture_create(int w, int h, const uint32_t *rgba, int filter_linear)
{
	int id = -1;
	for (int i = 0; i < MAX_TEX; i++) {
		if (!g_tex[i].used) {
			id = i;
			break;
		}
	}
	if (id < 0) {
		return -1;
	}
	int tw, th;
	uint16_t *lin;
	PspTex t = {0};
	if (filter_linear) {
		tw = pow2(w), th = pow2(h);
		lin = malloc(sizeof(uint16_t) * tw * th);
		if (!lin) {
			return -1;
		}
		for (int y = 0; y < th; y++) {
			for (int x = 0; x < tw; x++) {
				/* resample so UVs 0..1 still cover the image */
				uint32_t c = rgba[(y * h / th) * w + (x * w / tw)];
				uint32_t r = c & 0xff, g = c >> 8 & 0xff, b = c >> 16 & 0xff, a = c >> 24;
				lin[y * tw + x] = (uint16_t)((a >= 128) << 15 | (b >> 3) << 10 | (g >> 3) << 5 | (r >> 3));
			}
		}
		t.psm = GU_PSM_5551;
		t.img_w = w, t.img_h = h;
		t.ku = (float)tw / w, t.kv = (float)th / h;
	}
	else {
		int dw, dh;
		lin = downscale_2d(w, h, rgba, &dw, &dh, &tw, &th);
		if (!lin) {
			return -1;
		}
		t.psm = GU_PSM_4444;
		t.img_w = w, t.img_h = h;
		t.ku = (float)SCR_W / VIRT_W, t.kv = (float)SCR_H / VIRT_H;
	}
	uint16_t *sw = memalign(16, sizeof(uint16_t) * tw * th);
	if (!sw) {
		free(lin);
		return -1;
	}
	swizzle16(sw, lin, tw, th);
	free(lin);
	sceKernelDcacheWritebackRange(sw, sizeof(uint16_t) * tw * th);
	t.data = sw;
	t.w = tw, t.h = th;
	t.used = 1;
	g_tex[id] = t;
	return id;
}

void gfx_texture_destroy(int tex)
{
	if (tex >= 0 && tex < MAX_TEX && g_tex[tex].used) {
		sceGuTexFlush();
		free(g_tex[tex].data);
		g_tex[tex].used = 0;
		if (g_bound == tex) {
			g_bound = -1;
		}
	}
}

void gfx_texture_bind(int tex)
{
	if (tex >= 0 && tex < MAX_TEX && g_tex[tex].used) {
		PspTex *t = &g_tex[tex];
		sceGuEnable(GU_TEXTURE_2D);
		if (g_bound != tex) {
			sceGuTexMode(t->psm, 0, 0, 1);
			sceGuTexImage(0, t->w, t->h, t->w, t->data);
			sceGuTexFlush();
			g_bound = tex;
		}
	}
	else {
		sceGuDisable(GU_TEXTURE_2D);
	}
}

#define VTYPE_3D (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

/* persistent: the caller keeps the array unchanged until the frame ends */
static void draw_clipped(const GfxVertex *v, int count, int persistent, const float *box)
{
	if (count < 3) {
		return;
	}
	clip_set_view(g_mv, g_fx, g_fy, g_near, g_far);
	if (box ? clip_box_inside(box) : clip_all_inside(v, count)) {
		if (persistent) {
			sceKernelDcacheWritebackRange(v, sizeof(GfxVertex) * count);
			sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, count, 0, v);
		}
		else {
			GfxVertex *copy = sceGuGetMemory(sizeof(GfxVertex) * count);
			memcpy(copy, v, sizeof(GfxVertex) * count);
			sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, count, 0, copy);
		}
		return;
	}
	const GfxVertex *clipped;
	int n;
	PROF(PROF_CLIP, n = clip_triangles(v, count, &clipped));
	if (n <= 0) {
		return;
	}
	GfxVertex *copy = sceGuGetMemory(sizeof(GfxVertex) * n);
	memcpy(copy, clipped, sizeof(GfxVertex) * n);
	sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, n, 0, copy);
}

void gfx_draw_triangles(const GfxVertex *v, int count, int use_vertex_color)
{
	(void)use_vertex_color;
	draw_clipped(v, count, 1, NULL);
}

void gfx_draw_triangles_box(const GfxVertex *v, int count, int use_vertex_color, const float box[6])
{
	(void)use_vertex_color;
	draw_clipped(v, count, 1, box);
}

/* The GE reads vertices while the CPU goes on: copy arrays the caller reuses. */
void gfx_draw_triangles_temp(const GfxVertex *v, int count, int use_vertex_color)
{
	(void)use_vertex_color;
	draw_clipped(v, count, 0, NULL);
}

void gfx_draw_lines(const GfxVertex *v, int count)
{
	sceKernelDcacheWritebackRange(v, sizeof(GfxVertex) * count);
	sceGumDrawArray(GU_LINES, VTYPE_3D, count, 0, v);
}

typedef struct {
	float u, v;
	uint32_t color;
	float x, y, z;
} V2D;

#define VTYPE_2D (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)

void gfx_begin_2d(void)
{
	sceGuViewport(2048, 2048, SCR_W, SCR_H);
	sceGuScissor(0, 0, SCR_W, SCR_H);
	sceGuDisable(GU_LIGHTING);
	sceGuDisable(GU_DEPTH_TEST);
	sceGuDisable(GU_CULL_FACE);
	sceGuDisable(GU_FOG);
	sceGuEnable(GU_ALPHA_TEST);
	sceGuEnable(GU_BLEND);
	sceGuAlphaFunc(GU_GREATER, 0, 0xff); /* keep the antialiased edges */
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}

void gfx_draw_quad_2d(int tex, float sx, float sy, float sw, float sh,
                      float dx, float dy, float dw, float dh, uint32_t color)
{
	if (tex < 0 || tex >= MAX_TEX || !g_tex[tex].used) {
		return;
	}
	PspTex *t = &g_tex[tex];
	float ku = t->ku, kv = t->kv;
	float u0 = sx * ku, v0 = sy * kv, u1 = (sx + sw) * ku, v1 = (sy + sh) * kv;
	float x0 = dx * SCR_W / VIRT_W, y0 = dy * SCR_H / VIRT_H;
	float x1 = (dx + dw) * SCR_W / VIRT_W, y1 = (dy + dh) * SCR_H / VIRT_H;
	if (t->psm == GU_PSM_4444 && sw == dw && sh == dh) {
		/* An unscaled blit of an image already shrunk to screen scale: copy
		 * whole texels to whole pixels, as fractional offsets drop rows. */
		u0 = floorf(u0 + 0.5f), v0 = floorf(v0 + 0.5f), u1 = floorf(u1 + 0.5f), v1 = floorf(v1 + 0.5f);
		x0 = floorf(x0 + 0.5f), y0 = floorf(y0 + 0.5f);
		x1 = x0 + (u1 - u0), y1 = y0 + (v1 - v0);
	}
	V2D *q = sceGuGetMemory(sizeof(V2D) * 2);
	q[0] = (V2D){u0, v0, color, x0, y0, 0};
	q[1] = (V2D){u1, v1, color, x1, y1, 0};
	gfx_texture_bind(tex);
	sceGuDrawArray(GU_SPRITES, VTYPE_2D, 2, 0, q);
}

typedef struct {
	uint32_t color;
	float x, y, z;
} V2DC;

void gfx_fill_rect_2d(float x, float y, float w, float h, uint32_t color)
{
	V2DC *q = sceGuGetMemory(sizeof(V2DC) * 2);
	q[0] = (V2DC){color, x * SCR_W / VIRT_W, y * SCR_H / VIRT_H, 0};
	q[1] = (V2DC){color, (x + w) * SCR_W / VIRT_W, (y + h) * SCR_H / VIRT_H, 0};
	sceGuDisable(GU_TEXTURE_2D);
	sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, q);
}

void gfx_end_2d(void)
{
	sceGuAlphaFunc(GU_GREATER, 0x80, 0xff);
	sceGuDisable(GU_ALPHA_TEST);
	sceGuDisable(GU_BLEND);
	sceGuEnable(GU_DEPTH_TEST);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexWrap(GU_REPEAT, GU_REPEAT);
}

/* ------------------------------------------------------------- save data */

/* All save files packed in one DATA.BIN: "ASSAVE01", count, then for each
 * file its name length, name, size and contents. */
#define SAVE_CAP (512 * 1024) /* the save is about 40 KB with all replays */

static uint8_t *g_saved_blob;   /* as last loaded from or saved to the card */
static size_t g_saved_size;

static uint8_t *pack_saves(size_t *size)
{
	size_t n = 12;
	for (int i = 0; i < g_nsave_files; i++) {
		n += 1 + strlen(g_save_files[i].name) + 4 + g_save_files[i].size;
	}
	uint8_t *b = malloc(n), *p = b;
	if (!b) {
		return NULL;
	}
	memcpy(p, "ASSAVE01", 8), p += 8;
	uint32_t count = (uint32_t)g_nsave_files;
	memcpy(p, &count, 4), p += 4;
	for (int i = 0; i < g_nsave_files; i++) {
		const SaveFile *f = &g_save_files[i];
		uint8_t len = (uint8_t)strlen(f->name);
		uint32_t sz = (uint32_t)f->size;
		*p++ = len;
		memcpy(p, f->name, len), p += len;
		memcpy(p, &sz, 4), p += 4;
		if (sz) {
			memcpy(p, f->data, sz), p += sz;
		}
	}
	*size = n;
	return b;
}

static int unpack_saves(const uint8_t *b, size_t n)
{
	if (n < 12 || memcmp(b, "ASSAVE01", 8)) {
		return 0;
	}
	uint32_t count;
	memcpy(&count, b + 8, 4);
	size_t at = 12;
	for (uint32_t i = 0; i < count; i++) {
		if (at + 1 > n) {
			return 0;
		}
		uint8_t len = b[at++];
		char name[256];
		uint32_t sz;
		if (at + len + 4 > n) {
			return 0;
		}
		memcpy(name, b + at, len), name[len] = 0, at += len;
		memcpy(&sz, b + at, 4), at += 4;
		if (at + sz > n) {
			return 0;
		}
		SaveFile *f = save_file(name, 1);
		if (f) {
			free(f->data);
			f->data = malloc(sz ? sz : 1);
			if (f->data) {
				memcpy(f->data, b + at, sz);
			}
			f->size = f->data ? sz : 0;
		}
		at += sz;
	}
	return 1;
}

static void *load_whole(const char *name, SceSize *size)
{
	FILE *f = plat_open_data(name, "rb");
	void *buf = NULL;
	*size = 0;
	if (f) {
		fseek(f, 0, SEEK_END);
		long n = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (n > 0 && (buf = malloc((size_t)n)) != NULL && fread(buf, 1, (size_t)n, f) == (size_t)n) {
			*size = (SceSize)n;
		}
		fclose(f);
	}
	return buf;
}

/* Runs one save data dialog to its end, drawing a blank frame under it. */
static int savedata_dialog(int mode, void *buf, SceSize bufsize, SceSize datasize)
{
	static SceUtilitySavedataParam p;
	memset(&p, 0, sizeof(p));
	p.base.size = sizeof(p);
	sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &p.base.language);
	p.base.buttonSwap = PSP_UTILITY_ACCEPT_CROSS;
	p.base.graphicsThread = 0x11;
	p.base.accessThread = 0x13;
	p.base.fontThread = 0x12;
	p.base.soundThread = 0x10;
	p.mode = mode;
	p.overwrite = 1;
	p.focus = PSP_UTILITY_SAVEDATA_FOCUS_LATEST;
	/* A plain save. With the default 0 (or 2, 3) the firmware wants a 16-byte
	 * game key and fails with a parameter error without one; PPSSPP does not
	 * check it here, so this only showed on a console. */
	p.secureVersion = 1;
	strcpy(p.gameName, "ACESPEED0");
	strcpy(p.saveName, "0000");
	strcpy(p.fileName, "DATA.BIN");
	p.dataBuf = buf;
	p.dataBufSize = bufsize;
	p.dataSize = datasize;
	strcpy(p.sfoParam.title, "AceSpeeder!");
	strcpy(p.sfoParam.savedataTitle, "Game data");
	strcpy(p.sfoParam.detail, "Records, settings and replays");
	p.sfoParam.parentalLevel = 1;
	SceSize icon_size, pic_size;
	void *icon = load_whole("ICON0.PNG", &icon_size), *pic = load_whole("PIC1.PNG", &pic_size);
	if (icon) {
		p.icon0FileData.buf = icon;
		p.icon0FileData.bufSize = p.icon0FileData.size = icon_size;
	}
	if (pic) {
		p.pic1FileData.buf = pic;
		p.pic1FileData.bufSize = p.pic1FileData.size = pic_size;
	}
	int result = -1;
	int start = sceUtilitySavedataInitStart(&p);
	if (start == 0) {
		for (int running = 1; running && !g_quit;) {
			sceGuStart(GU_DIRECT, g_list);
			sceGuDrawBufferList(GU_PSM_5650, g_draw_buf, BUF_W);
			sceGuScissor(0, 0, SCR_W, SCR_H);
			sceGuClearColor(0xff000000);
			sceGuClear(GU_COLOR_BUFFER_BIT);
			sceGuFinish();
			sceGuSync(0, 0);
			switch (sceUtilitySavedataGetStatus()) {
			case PSP_UTILITY_DIALOG_VISIBLE:
				sceUtilitySavedataUpdate(1);
				break;
			case PSP_UTILITY_DIALOG_QUIT:
				sceUtilitySavedataShutdownStart();
				break;
			case PSP_UTILITY_DIALOG_NONE:
				running = 0;
				break;
			}
			sceDisplayWaitVblankStart();
			sceDisplaySetFrameBuf(vram_abs(g_draw_buf), BUF_W, PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME);
			g_draw_idx = (g_draw_idx + 1) % 3;
			g_draw_buf = g_fb[g_draw_idx];
		}
		result = p.base.result;
	}
	FILE *log = plat_open_save("savedata.log", "a");
	if (log) {
		fprintf(log, "mode %d start %08x result %08x size %u free %u\n", mode, (unsigned)start, (unsigned)result,
		        (unsigned)datasize, (unsigned)sceKernelTotalFreeMemSize());
		fclose(log);
	}
	free(icon);
	free(pic);
	return result;
}

void plat_save_load(void)
{
	SceSize cap = SAVE_CAP;
	uint8_t *buf = malloc(cap);
	if (!buf) {
		return;
	}
	memset(buf, 0, 12);
	if (savedata_dialog(PSP_UTILITY_SAVEDATA_LOAD, buf, cap, 0) == 0 && unpack_saves(buf, cap)) {
		free(g_saved_blob);
		g_saved_blob = pack_saves(&g_saved_size);
	}
	free(buf);
}

void plat_save_commit(void)
{
	size_t n;
	uint8_t *blob = pack_saves(&n);
	if (!blob || n > SAVE_CAP) {
		free(blob);
		return;
	}
	if (g_saved_blob && n == g_saved_size && !memcmp(blob, g_saved_blob, n)) {
		free(blob);
		return; /* nothing changed since the last load or save */
	}
	if (savedata_dialog(PSP_UTILITY_SAVEDATA_SAVE, blob, (SceSize)n, (SceSize)n) == 0) {
		free(g_saved_blob);
		g_saved_blob = blob;
		g_saved_size = n;
	}
	else {
		free(blob);
	}
}
