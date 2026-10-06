/* PSP backend: sceGu rendering, sceCtrl input, pspaudiolib output. */
#include <malloc.h>
#include <pspaudiolib.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../mixer.h"
#include "../platform.h"

PSP_MODULE_INFO("AceSpeeder", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024); /* all but 1 MB of user RAM for the heap */

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define VIRT_W 640
#define VIRT_H 480

static unsigned int __attribute__((aligned(16))) g_list[256 * 1024];
static void *g_fbp0, *g_fbp1, *g_zbp;
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

static void make_save_dir(void)
{
	sceIoMkdir("save", 0777);
}

FILE *plat_open_data(const char *name, const char *mode)
{
	return fopen(name, mode); /* cwd is the EBOOT folder */
}

FILE *plat_open_save(const char *name, const char *mode)
{
	char p[256];
	snprintf(p, sizeof(p), "save/%s", name);
	FILE *f = fopen(p, mode);
	if (!f && mode[0] == 'r') {
		f = fopen(name, mode); /* shipped defaults */
	}
	return f;
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

	g_fbp0 = vram_static(BUF_W * SCR_H * 2);
	g_fbp1 = vram_static(BUF_W * SCR_H * 2);
	g_zbp = vram_static(BUF_W * SCR_H * 2);

	sceGuInit();
	sceGuStart(GU_DIRECT, g_list);
	sceGuDrawBuffer(GU_PSM_5650, g_fbp0, BUF_W);
	sceGuDispBuffer(SCR_W, SCR_H, g_fbp1, BUF_W);
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

uint32_t plat_ticks_ms(void)
{
	return (uint32_t)(sceKernelGetSystemTimeWide() / 1000);
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

void plat_read_input(int32_t keys[IN_COUNT])
{
	SceCtrlData pad;
	sceCtrlPeekBufferPositive(&pad, 1);
	unsigned int b = pad.Buttons;
	if ((b & PSP_CTRL_UP) || pad.Ly < 128 - 50) keys[IN_UP] = 1;
	if ((b & PSP_CTRL_DOWN) || pad.Ly > 128 + 50) keys[IN_DOWN] = 1;
	if ((b & PSP_CTRL_RIGHT) || pad.Lx > 128 + 50) keys[IN_RIGHT] = 1;
	if ((b & PSP_CTRL_LEFT) || pad.Lx < 128 - 50) keys[IN_LEFT] = 1;
	if (b & (PSP_CTRL_CROSS | PSP_CTRL_START)) keys[IN_A] = 1;
	if (b & (PSP_CTRL_CIRCLE | PSP_CTRL_SQUARE)) keys[IN_B] = 1;
	if (b & PSP_CTRL_TRIANGLE) keys[IN_F1] = 1; /* placeholder mapping, revisit with game code */
	if ((b & PSP_CTRL_SELECT) && (b & PSP_CTRL_START)) keys[IN_ESC] = 1;
}

/* --------------------------------------------------------------- graphics */

#define MAX_TEX 512

typedef struct {
	void *data;      /* swizzled 5551 */
	int w, h;        /* buffer size (power of two) */
	int img_w, img_h;/* original image size */
	int used;
} PspTex;

static PspTex g_tex[MAX_TEX];
static int g_bound = -1;
static int g_in_frame;

int gfx_screen_w(void) { return VIRT_W; }
int gfx_screen_h(void) { return VIRT_H; }
float gfx_pixel_aspect(void) { return ((float)SCR_W / SCR_H) / ((float)VIRT_W / VIRT_H); }

void gfx_begin_frame(void)
{
	sceGuStart(GU_DIRECT, g_list);
	g_in_frame = 1;
	sceGuScissor(0, 0, SCR_W, SCR_H);
	sceGuClearColor(0xff000000);
	sceGuClearDepth(0);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void gfx_end_frame(void)
{
	sceGuFinish();
	sceGuSync(0, 0);
	g_in_frame = 0;
	sceDisplayWaitVblankStart();
	void *shown = sceGuSwapBuffers();
	(void)shown;
}

void plat_screenshot(const char *path)
{
	/* Read the frame just drawn (current draw buffer) after the GPU is done. */
	sceGuFinish();
	sceGuSync(0, 0);
	void *drawn = (void *)((unsigned int)sceGeEdramGetAddr() + (unsigned int)g_fbp0);
	/* sceGuSwapBuffers alternates the buffers; find which one we drew into. */
	void *disp_addr;
	int bw, fmt;
	sceDisplayGetFrameBuf(&disp_addr, &bw, &fmt, PSP_DISPLAY_SETBUF_NEXTFRAME);
	unsigned int base = (unsigned int)sceGeEdramGetAddr();
	unsigned int disp = ((unsigned int)disp_addr & 0x0fffffff) | (base & 0xf0000000);
	if (disp == (unsigned int)drawn) {
		drawn = (void *)(base + (unsigned int)g_fbp1);
	}
	uint16_t *px = (uint16_t *)((unsigned int)drawn | 0x40000000); /* uncached */
	FILE *f = fopen(path, "wb");
	if (f) {
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

void gfx_set_projection(float fx, float fy, float n, float f)
{
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
	int tw = pow2(w), th = pow2(h);
	uint16_t *lin = malloc(sizeof(uint16_t) * tw * th);
	for (int y = 0; y < th; y++) {
		for (int x = 0; x < tw; x++) {
			uint32_t c;
			if (filter_linear) { /* resample so UVs 0..1 still cover the image */
				c = rgba[(y * h / th) * w + (x * w / tw)];
			}
			else {
				c = (x < w && y < h) ? rgba[y * w + x] : 0;
			}
			uint32_t r = c & 0xff, g = c >> 8 & 0xff, b = c >> 16 & 0xff, a = c >> 24;
			lin[y * tw + x] = (uint16_t)((a >= 128) << 15 | (b >> 3) << 10 | (g >> 3) << 5 | (r >> 3));
		}
	}
	uint16_t *sw = memalign(16, sizeof(uint16_t) * tw * th);
	swizzle16(sw, lin, tw, th);
	free(lin);
	sceKernelDcacheWritebackRange(sw, sizeof(uint16_t) * tw * th);
	g_tex[id] = (PspTex){sw, tw, th, w, h, 1};
	if (!filter_linear) {
		g_tex[id].img_w = tw; /* padded: texel coordinates stay 1:1 */
		g_tex[id].img_h = th;
	}
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
			sceGuTexMode(GU_PSM_5551, 0, 0, 1);
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

void gfx_draw_triangles(const GfxVertex *v, int count, int use_vertex_color)
{
	(void)use_vertex_color;
	sceKernelDcacheWritebackRange(v, sizeof(GfxVertex) * count);
	sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, count, 0, v);
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
	sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}

void gfx_draw_quad_2d(int tex, float sx, float sy, float sw, float sh,
                      float dx, float dy, float dw, float dh, uint32_t color)
{
	if (tex < 0 || tex >= MAX_TEX || !g_tex[tex].used) {
		return;
	}
	PspTex *t = &g_tex[tex];
	float ku = (float)t->w / t->img_w, kv = (float)t->h / t->img_h;
	float x0 = dx * SCR_W / VIRT_W, y0 = dy * SCR_H / VIRT_H;
	float x1 = (dx + dw) * SCR_W / VIRT_W, y1 = (dy + dh) * SCR_H / VIRT_H;
	V2D *q = sceGuGetMemory(sizeof(V2D) * 2);
	q[0] = (V2D){sx * ku, sy * kv, color, x0, y0, 0};
	q[1] = (V2D){(sx + sw) * ku, (sy + sh) * kv, color, x1, y1, 0};
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
	sceGuDisable(GU_ALPHA_TEST);
	sceGuDisable(GU_BLEND);
	sceGuEnable(GU_DEPTH_TEST);
	sceGuTexFilter(GU_LINEAR, GU_LINEAR);
	sceGuTexWrap(GU_REPEAT, GU_REPEAT);
}
