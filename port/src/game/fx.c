/* Effects bank: Effects_RenderVisual (0x423d00) and its setup. The mesh
 * effects rebuild their vertices from the loaded .x each time; the quad
 * effects (boards, sparks, smoke) are built by the prepare functions. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/image.h"
#include "../engine/xfile.h"
#include "../platform/platform.h"
#include "effects.h"
#include "fx.h"
#include "hud.h"
#include "machine.h"
#include "track.h"

#define FX_MODES 11

typedef struct {
	int nfaces;
	int *fcount;    /* vertices per face (drawn as fans) */
	int nverts;
	Vec3 *pos;      /* source positions, scaled as at load */
	float (*uv)[2];
	GfxVertex *v;   /* vertices for drawing */
} FxMesh;

static FxMesh meshes[FX_MODES];
static int textures[FX_MODES] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static int tex_w[FX_MODES], tex_h[FX_MODES];
static int mode;
static double param;

/* pickup boards, sparks, smoke: quads of 4 vertices */
static GfxVertex boards[16][4];
static GfxVertex sparks[50][4];
static int nsparks;
static GfxVertex smoke[50][4];
static int nsmoke;

static const char *mesh_names[FX_MODES] = {"backfire.x", "speed.x", "jumpup.x", "special.x", NULL, "ring.x",
                                           NULL, NULL, NULL, NULL, "search.x"};
static const char *tex_names[FX_MODES] = {"backfire.bmp", "speed.bmp", "jumpup.bmp", "special.bmp", "jboard.bmp",
                                          "ring.bmp", "fire.bmp", "smoke.bmp", "info.bmp", "ace.bmp", "search.bmp"};

static uint32_t abgr(uint32_t argb)
{
	return (argb & 0xff00ff00u) | (argb >> 16 & 0xff) | (argb & 0xff) << 16;
}

static uint32_t grey(int c, int alpha)
{
	return abgr((uint32_t)alpha << 24 | (uint32_t)(c & 0xff) * 0x010101u);
}

static void load_mesh(FxMesh *m, const char *name)
{
	XMesh x;
	memset(m, 0, sizeof(*m));
	if (xmesh_load(&x, name) != 0) {
		return;
	}
	m->nfaces = x.nfaces;
	m->fcount = malloc(sizeof(int) * (size_t)x.nfaces);
	int total = 0;
	for (int f = 0; f < x.nfaces; f++) {
		m->fcount[f] = x.face_count[f];
		total += x.face_count[f];
	}
	m->nverts = total;
	m->pos = malloc(sizeof(Vec3) * (size_t)total);
	m->uv = malloc(sizeof(float[2]) * (size_t)total);
	m->v = calloc((size_t)total, sizeof(GfxVertex));
	for (int i = 0; i < total; i++) {
		int k = x.idx[i];
		m->pos[i] = x.verts[k];
		m->uv[i][0] = x.uv ? x.uv[k][0] : 0.0f;
		m->uv[i][1] = x.uv ? x.uv[k][1] : 0.0f;
		m->v[i].u = m->uv[i][0];
		m->v[i].v = m->uv[i][1];
		m->v[i].color = grey(255, 0x80);
	}
	xmesh_free(&x);
}

static void scale_mesh(FxMesh *m, float sx, float sy, float sz, float oy, float oz)
{
	for (int i = 0; i < m->nverts; i++) {
		m->pos[i] = v3(m->pos[i].x * sx, m->pos[i].y * sy + oy, m->pos[i].z * sz + oz);
		m->v[i].x = m->pos[i].x, m->v[i].y = m->pos[i].y, m->v[i].z = m->pos[i].z;
	}
}

void Fx_Init(void)
{
	for (int i = 0; i < FX_MODES; i++) {
		Image img;
		if (image_load_bmp(&img, tex_names[i]) == 0) {
			textures[i] = gfx_texture_create(img.w, img.h, img.px, 1);
			tex_w[i] = img.w, tex_h[i] = img.h;
			image_free(&img);
		}
		if (mesh_names[i]) {
			load_mesh(&meshes[i], mesh_names[i]);
		}
	}
	scale_mesh(&meshes[FX_BACKFIRE], 24, 24, 32, 8, -18);
	scale_mesh(&meshes[FX_SPEED], 20, 20, 20, 0, 0);
	scale_mesh(&meshes[FX_JUMPUP], 150, 150, 150, 0, 0);
	scale_mesh(&meshes[FX_SPECIAL], 20, 20, 50, 10, 0);
	scale_mesh(&meshes[FX_RING], 1, 1, 1, 0, 0);
	scale_mesh(&meshes[FX_SCENERY], 1, 1, 1, 0, 0);
}

void Fx_Free(void)
{
	for (int i = 0; i < FX_MODES; i++) {
		free(meshes[i].fcount);
		free(meshes[i].pos);
		free(meshes[i].uv);
		free(meshes[i].v);
		memset(&meshes[i], 0, sizeof(meshes[i]));
		if (textures[i] >= 0) {
			gfx_texture_destroy(textures[i]);
			textures[i] = -1;
		}
	}
}

void Fx_SetMode(int m)
{
	mode = m;
}

void Fx_SetModeParam(int m, double p)
{
	mode = m;
	param = p;
}

static void update_mesh(void)
{
	FxMesh *m = &meshes[mode];
	float p = (float)param;
	switch (mode) {
	case FX_BACKFIRE: { /* zoom the flame texture with the throttle, with a flicker */
		float s = (float)((4.0 - param) * 0.125 + sin(g_phaseTime * 1.81818) * 0.02 + 1.15);
		for (int i = 0; i < m->nverts; i++) {
			m->v[i].u = (m->uv[i][0] - 0.5f) * s + 0.5f;
			m->v[i].v = (m->uv[i][1] - 0.5f) * s + 0.5f;
		}
		break;
	}
	case FX_SPEED:
	case FX_JUMPUP: {
		uint32_t c = grey((int)((1.0f - p * p) * 255.0f), 0x64);
		for (int i = 0; i < m->nverts; i++) {
			m->v[i].x = m->pos[i].x * p * 80.0f;
			m->v[i].y = p * m->pos[i].y * -25.0f;
			m->v[i].z = m->pos[i].z * p * 150.0f;
			m->v[i].color = c;
		}
		break;
	}
	case FX_SPECIAL: {
		uint32_t c = grey((int)((1.0 - param) * 255.0), 0x64);
		float k = 1.2f - p;
		for (int i = 0; i < m->nverts; i++) {
			m->v[i].v = (m->uv[i][1] - 0.5f) * k + 0.5f;
			m->v[i].u = (m->uv[i][0] - 0.5f) * k + 0.5f;
			m->v[i].x = m->pos[i].x * p * 500.0f;
			m->v[i].y = m->pos[i].y * p * 500.0f;
			m->v[i].z = m->pos[i].z * p * 500.0f + 50.0f;
			m->v[i].color = c;
		}
		break;
	}
	case FX_RING: {
		float q = (float)(1.0 - (1.0 - param) * (1.0 - param));
		uint32_t c = grey((int)(q * 255.0f), 0x64);
		for (int i = 0; i < m->nverts; i++) {
			m->v[i].x = m->pos[i].x * q * q * 800.0f;
			m->v[i].y = m->pos[i].y;
			m->v[i].z = m->pos[i].z * q * q * 800.0f;
			m->v[i].color = c;
		}
		break;
	}
	case FX_SCENERY: {
		int r = (int)(g_scenery.color[0] * 255.0f), b = (int)(g_scenery.color[2] * 255.0f);
		uint32_t c = abgr(0x64u << 24 | (uint32_t)r << 16 | (uint32_t)b << 8 | (uint32_t)b);
		for (int i = 0; i < m->nverts; i++) {
			m->v[i].x = m->pos[i].x * 2000.0f;
			m->v[i].y = m->pos[i].y * 6000.0f;
			m->v[i].z = m->pos[i].z * 2000.0f;
			m->v[i].color = c;
		}
		break;
	}
	}
}

static void draw_fan(const GfxVertex *v, int n)
{
	GfxVertex tri[3 * 64];
	int k = 0;
	for (int i = 1; i + 1 < n && k + 3 <= 3 * 64; i++) {
		tri[k++] = v[0];
		tri[k++] = v[i];
		tri[k++] = v[i + 1];
	}
	gfx_draw_triangles_temp(tri, k, 1);
}

static void render_cb(RMUserVisual *uv, void *arg, RMViewport *view)
{
	(void)uv, (void)arg, (void)view;
	gfx_set_lighting(0);
	gfx_set_blend(GFX_BLEND_ADD);
	/* the original leaves Direct3D's default back-face culling on: drawing the
	 * back faces too would hide parts of the front ones through the z-buffer */
	gfx_texture_bind(textures[mode]);
	if (mode < 4 || mode == FX_RING || mode == FX_SCENERY) {
		FxMesh *m = &meshes[mode];
		update_mesh();
		for (int f = 0, at = 0; f < m->nfaces; f++) {
			draw_fan(m->v + at, m->fcount[f]);
			at += m->fcount[f];
		}
	}
	else if (mode == FX_BOARDS) {
		int frame = (int)(g_phaseTime * 0.08333333333333333) % 16;
		float u = (float)(frame % 4) * 0.25f, v = (float)(frame / 4) * 0.25f;
		for (int i = 0; i < g_pickups.count && i < 16; i++) {
			GfxVertex *q = boards[i];
			q[0].u = u, q[0].v = v;
			q[1].u = u + 0.25f, q[1].v = v;
			q[2].u = u + 0.25f, q[2].v = v + 0.25f;
			q[3].u = u, q[3].v = v + 0.25f;
			draw_fan(q, 4);
		}
	}
	else if (mode == FX_SPARKS) {
		for (int i = 0; i < nsparks; i++) {
			draw_fan(sparks[i], 4);
		}
	}
	else if (mode == FX_SMOKE) {
		for (int i = 0; i < nsmoke; i++) {
			draw_fan(smoke[i], 4);
		}
	}
	else if (mode == FX_INFO || mode == FX_ACE) {
		/* the centre message: a pre-transformed quad in 640x480 coordinates */
		const TLVertex *t = g_hudMsg.v;
		int tw = tex_w[mode] ? tex_w[mode] : 1, th = tex_h[mode] ? tex_h[mode] : 1;
		gfx_begin_2d();
		gfx_set_blend(GFX_BLEND_ADD);
		gfx_draw_quad_2d(textures[mode], t[0].tu * tw, t[0].tv * th, (t[2].tu - t[0].tu) * tw,
		                 (t[2].tv - t[0].tv) * th, t[0].sx, t[0].sy, t[2].sx - t[0].sx, t[2].sy - t[0].sy,
		                 abgr(t[0].color | 0xff000000u));
		gfx_end_2d();
	}
	gfx_set_blend(GFX_BLEND_OFF);
	gfx_set_lighting(1);
}

RMUserVisual *Fx_CreateVisual(void)
{
	return rm_user_visual_create(render_cb, NULL);
}

/* One board per pickup over its track range, 5 units above the surface. */
void Fx_PreparePickups(void)
{
	for (int i = 0; i < g_pickups.count && i < 16; i++) {
		float s0 = g_pickups.seg[i] + g_pickups.trange[i][0], s1 = g_pickups.seg[i] + g_pickups.trange[i][1];
		float l0 = g_pickups.lrange[i][0] + 3.0f, l1 = g_pickups.lrange[i][1] + 3.0f;
		float ss[4] = {s0, s0, s1, s1}, ll[4] = {l0, l1, l1, l0};
		for (int k = 0; k < 4; k++) {
			Vec3 p = Track_Point(&g_track, TrackPos_Make(ss[k], ll[k], 5.0f));
			boards[i][k] = (GfxVertex){0, 0, 0xffffffffu, 0, 0, 1, p.x, p.y, p.z};
		}
	}
}

static void billboard(GfxVertex q[4], Vec3 c, Mat3 cam, float hw, float hh, uint32_t color)
{
	static const float cx[4] = {-1, 1, 1, -1}, cy[4] = {1, 1, -1, -1}, us[4] = {0, 1, 1, 0}, vs[4] = {0, 0, 1, 1};
	for (int k = 0; k < 4; k++) {
		Vec3 o = Mat3_MulVec(v3(cx[k] * hw, cy[k] * hh, 0.0f), cam);
		q[k] = (GfxVertex){us[k], vs[k], color, 0, 0, 1, c.x + o.x, c.y + o.y, c.z + o.z};
	}
}

/* Sparks live in the player's frame; each is a small camera-facing quad
 * that fades over its 25 ticks (sizes and colours approximate the original). */
void Fx_PrepareSparks(Mat3 cam)
{
	const Machine *p = &g_machines[0];
	nsparks = 0;
	for (int i = 0; i < 50; i++) {
		if (!g_sparks.active[i] || g_sparks.life[i] > 25.0f) {
			continue;
		}
		float g = 1.0f - g_sparks.life[i] * 0.04f;
		Vec3 w = Vec3_Add(Mat3_MulVec(g_sparks.pos[i], p->rot), p->pos);
		int c = (int)(g * 255.0f);
		billboard(sparks[nsparks++], w, cam, 0.5f, 1.0f, abgr(0x80000000u | (uint32_t)c << 16 | (uint32_t)(c * 3 / 4) << 8 | (uint32_t)(c / 3)));
	}
}

int Fx_SparkCount(void)
{
	return nsparks;
}

/* Explosion smoke: growing, fading camera-facing puffs (approximation of 0x41e7d0). */
void Fx_PrepareSmoke(Mat3 cam, Vec3 campos)
{
	(void)campos;
	nsmoke = 0;
	for (int i = 0; i < 50; i++) {
		if (!g_explosion.active[i]) {
			continue;
		}
		float t = g_explosion.life[i] / 40.0f;
		int c = (int)((1.0f - t) * 255.0f);
		float s = 20.0f + t * 60.0f;
		billboard(smoke[nsmoke++], g_explosion.pos[i], cam, s, s, grey(c < 0 ? 0 : c, 0x80));
	}
}

int Fx_SmokeCount(void)
{
	return nsmoke;
}
