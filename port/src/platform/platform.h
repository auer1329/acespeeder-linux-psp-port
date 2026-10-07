/* Platform layer: everything that differs between Linux (SDL2 + OpenGL 1.x)
 * and PSP (sceGu). The game and engine only talk to the hardware through here. */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stdio.h>

/* ---- lifecycle / time ---- */
int plat_init(void);
void plat_shutdown(void);
/* Clock of the logic ticks. On the PSP it follows the vertical blanks (33 per
 * two), so each 33 ms tick lines up with a 30 Hz frame. */
uint32_t plat_ticks_ms(void);
uint32_t plat_time_us(void); /* wall clock in microseconds, for profiling */
void plat_sleep_ms(int ms);
int plat_should_quit(void); /* window closed / HOME pressed */
void plat_screenshot(const char *path); /* binary PPM of the current back buffer (debug aid) */

/* ---- files ---- */
/* Game data lives next to the executable (read-only); saves may live elsewhere. */
FILE *plat_open_data(const char *name, const char *mode);
FILE *plat_open_save(const char *name, const char *mode);

/* ---- input: the original's 14-slot key array (see re/NOTES.md) ---- */
enum {
	IN_UP, IN_DOWN, IN_RIGHT, IN_LEFT, IN_A, IN_B, IN_6, IN_ESC,
	IN_F1, IN_F2, IN_F3, IN_F4, IN_12, IN_13, IN_COUNT
};
/* Port-only keys in the unused slots: START pauses or confirms (see main.c),
 * the performance meter toggles. */
enum {
	IN_START = IN_6, IN_PERF = IN_13
};
void plat_read_input(int32_t keys[IN_COUNT]); /* sets held keys to 1, leaves others untouched */

/* ---- graphics ---- */
/* Vertex layout matches sceGu's GU_TEXTURE_32BITF|GU_COLOR_8888|GU_NORMAL_32BITF|GU_VERTEX_32BITF
 * ordering so both backends can draw the same arrays. */
typedef struct {
	float u, v;
	uint32_t color; /* 0xAABBGGRR */
	float nx, ny, nz;
	float x, y, z;
} GfxVertex;

typedef struct {
	float r, g, b, a;
} GfxColor;

enum { GFX_LIGHT_AMBIENT, GFX_LIGHT_POINT, GFX_LIGHT_SPOT, GFX_LIGHT_DIRECTIONAL };
#define GFX_MAX_LIGHTS 4

typedef struct {
	int type;
	GfxColor color;
	float pos[3]; /* view space */
	float dir[3]; /* view space, direction the light travels */
	float att0, att1, att2;
	float range;
} GfxLight;

int gfx_screen_w(void); /* virtual 2D screen: always 640x480 like the original */
int gfx_screen_h(void);
float gfx_pixel_aspect(void); /* physical display aspect / virtual aspect (PSP: 16:9 vs 4:3) */
void gfx_begin_frame(void);
void gfx_end_frame(void); /* present */
uint32_t gfx_gpu_wait_us(void); /* time the last gfx_end_frame waited for the GPU */
void gfx_set_viewport(int x, int y, int w, int h);
void gfx_clear(uint32_t rgb, int color, int depth); /* rgb = 0x00RRGGBB (D3DCOLOR order) */

/* Matrices in RM row-vector convention (see rmath.h), left-handed view space. */
void gfx_set_projection(float field_x, float field_y, float znear, float zfar);
void gfx_set_modelview(const float m[4][4]);

void gfx_set_lighting(int on);
void gfx_set_lights(GfxColor ambient, const GfxLight *lights, int count);
void gfx_set_material(GfxColor diffuse, GfxColor emissive, GfxColor specular, float power);
void gfx_set_fog(int on, uint32_t rgb, float start, float end); /* rgb = 0x00RRGGBB */
void gfx_set_depth(int test, int write);
/* Pulls what follows slightly towards the camera in depth, for decals on a
 * surface (the PSP's 16-bit depth buffer cannot tell them apart otherwise). */
void gfx_set_depth_offset(int on);
void gfx_set_cull(int on);
enum { GFX_BLEND_OFF, GFX_BLEND_ALPHA, GFX_BLEND_ADD };
void gfx_set_blend(int mode); /* GFX_BLEND_ALPHA: src alpha / one minus src alpha; ADD: one / one */
void gfx_set_alpha_test(int on);

/* Textures are RGBA8888, any size up to 512x512. On the PSP non-power-of-two sizes are
 * resampled when filter_linear (3D textures, UVs 0..1) and padded otherwise (2D sprites). */
int gfx_texture_create(int w, int h, const uint32_t *rgba, int filter_linear);
void gfx_texture_destroy(int tex);
void gfx_texture_bind(int tex); /* -1 = untextured */

/* prims: triangles; use_vertex_color: diffuse comes from GfxVertex.color instead of material.
 * The GPU may read the array after the call returns: it must stay unchanged until the
 * frame ends. gfx_draw_triangles_temp copies it first, for arrays that are reused. */
void gfx_draw_triangles(const GfxVertex *v, int count, int use_vertex_color);
void gfx_draw_triangles_temp(const GfxVertex *v, int count, int use_vertex_color);
/* gfx_draw_triangles with the bounds of the vertices known (min xyz, max xyz) */
void gfx_draw_triangles_box(const GfxVertex *v, int count, int use_vertex_color, const float box[6]);
void gfx_draw_lines(const GfxVertex *v, int count);

/* 2D in the original's 640x480 virtual screen. Textured quad from texel rect to screen rect. */
void gfx_begin_2d(void);
void gfx_draw_quad_2d(int tex, float sx, float sy, float sw, float sh,
                      float dx, float dy, float dw, float dh, uint32_t color);
void gfx_fill_rect_2d(float x, float y, float w, float h, uint32_t color);
void gfx_end_2d(void);

/* ---- audio ---- */
int snd_load_wav(const char *name); /* returns handle or -1 */
void snd_play(int h, int loop);
void snd_stop(int h);
void snd_set_pan(int h, int pan);   /* DirectSound-style: -10000 (left) .. 10000 (right) */
void snd_set_freq(int h, int hz);   /* playback frequency in Hz, 0 = original */
void mus_play(const char *name, int loop);
void mus_stop(void);
void snd_set_volumes(float music, float sfx); /* 0..1 each */

#endif
