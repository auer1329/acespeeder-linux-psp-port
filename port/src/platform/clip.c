#include <stdlib.h>
#include <string.h>

#include "clip.h"

#define CLIP_PLANES 6
#define CLIP_MAX (3 + CLIP_PLANES)

typedef struct {
	GfxVertex v;
	float x, y, z;  /* view space */
} ClipVertex;

static ClipVertex *g_cv;      /* per-vertex view-space positions of a draw */
static int g_cv_cap;
static GfxVertex *g_clip_out; /* clipped triangles of a draw */
static int g_clip_cap;

static float g_mv[4][4];
static float g_fx = 1.0f, g_fy = 1.0f, g_near = 1.0f, g_far = 1000.0f;
static float g_guard = 7.0f;

void clip_set_guard(float guard)
{
	g_guard = guard;
}

void clip_set_view(const float mv[4][4], float fx, float fy, float n, float f)
{
	memcpy(g_mv, mv, sizeof(g_mv));
	g_fx = fx, g_fy = fy, g_near = n, g_far = f;
}

static void to_view(const GfxVertex *v, float *x, float *y, float *z)
{
	*x = v->x * g_mv[0][0] + v->y * g_mv[1][0] + v->z * g_mv[2][0] + g_mv[3][0];
	*y = v->x * g_mv[0][1] + v->y * g_mv[1][1] + v->z * g_mv[2][1] + g_mv[3][1];
	*z = v->x * g_mv[0][2] + v->y * g_mv[1][2] + v->z * g_mv[2][2] + g_mv[3][2];
}

/* signed distance to clip plane k (inside when >= 0) */
static float plane_dist(const ClipVertex *c, int k)
{
	switch (k) {
	case 0: return c->z - g_near * 1.001f;
	case 1: return g_far * 0.999f - c->z;
	case 2: return c->x + g_guard * g_fx * c->z;
	case 3: return g_guard * g_fx * c->z - c->x;
	case 4: return c->y + g_guard * g_fy * c->z;
	default: return g_guard * g_fy * c->z - c->y;
	}
}

static uint32_t lerp_color(uint32_t a, uint32_t b, float t)
{
	uint32_t out = 0;
	for (int s = 0; s < 32; s += 8) {
		float ca = (float)(a >> s & 0xff), cb = (float)(b >> s & 0xff);
		out |= (uint32_t)(int)(ca + (cb - ca) * t + 0.5f) << s;
	}
	return out;
}

static ClipVertex lerp_vertex(const ClipVertex *a, const ClipVertex *b, float t)
{
	ClipVertex r;
	r.v.u = a->v.u + (b->v.u - a->v.u) * t;
	r.v.v = a->v.v + (b->v.v - a->v.v) * t;
	r.v.color = lerp_color(a->v.color, b->v.color, t);
	r.v.nx = a->v.nx + (b->v.nx - a->v.nx) * t;
	r.v.ny = a->v.ny + (b->v.ny - a->v.ny) * t;
	r.v.nz = a->v.nz + (b->v.nz - a->v.nz) * t;
	r.v.x = a->v.x + (b->v.x - a->v.x) * t;
	r.v.y = a->v.y + (b->v.y - a->v.y) * t;
	r.v.z = a->v.z + (b->v.z - a->v.z) * t;
	r.x = a->x + (b->x - a->x) * t;
	r.y = a->y + (b->y - a->y) * t;
	r.z = a->z + (b->z - a->z) * t;
	return r;
}

static int reserve(void **buf, int *cap, int need, size_t size)
{
	if (need <= *cap) {
		return 1;
	}
	int n = *cap ? *cap : 1024;
	while (n < need) {
		n *= 2;
	}
	void *p = realloc(*buf, size * (size_t)n);
	if (!p) {
		return 0;
	}
	*buf = p;
	*cap = n;
	return 1;
}

/* Whether every vertex of the draw is safely inside: tested on the corners
 * of its bounding box, which is all most draws need. */
int clip_all_inside(const GfxVertex *v, int count)
{
	float lo[3] = {v[0].x, v[0].y, v[0].z}, hi[3] = {v[0].x, v[0].y, v[0].z};
	for (int i = 1; i < count; i++) {
		const float p[3] = {v[i].x, v[i].y, v[i].z};
		for (int k = 0; k < 3; k++) {
			if (p[k] < lo[k]) lo[k] = p[k];
			if (p[k] > hi[k]) hi[k] = p[k];
		}
	}
	for (int c = 0; c < 8; c++) {
		ClipVertex cv;
		cv.v.x = c & 1 ? hi[0] : lo[0];
		cv.v.y = c & 2 ? hi[1] : lo[1];
		cv.v.z = c & 4 ? hi[2] : lo[2];
		to_view(&cv.v, &cv.x, &cv.y, &cv.z);
		for (int k = 0; k < CLIP_PLANES; k++) {
			if (plane_dist(&cv, k) < 0.0f) {
				return 0;
			}
		}
	}
	return 1;
}

int clip_triangles(const GfxVertex *v, int count, const GfxVertex **out_v)
{
	*out_v = NULL;
	if (!reserve((void **)&g_cv, &g_cv_cap, count, sizeof(ClipVertex))) {
		return -1;
	}
	for (int i = 0; i < count; i++) {
		g_cv[i].v = v[i];
		to_view(&v[i], &g_cv[i].x, &g_cv[i].y, &g_cv[i].z);
	}
	int out = 0;
	for (int t = 0; t + 2 < count; t += 3) {
		const ClipVertex *tri = &g_cv[t];
		int outside_any = 0, culled = 0;
		for (int k = 0; k < CLIP_PLANES && !culled; k++) {
			int n_out = 0;
			for (int i = 0; i < 3; i++) {
				n_out += plane_dist(&tri[i], k) < 0.0f;
			}
			culled = n_out == 3;
			outside_any |= n_out > 0;
		}
		if (culled) {
			continue;
		}
		if (!reserve((void **)&g_clip_out, &g_clip_cap, out + 3 * (CLIP_MAX - 2), sizeof(GfxVertex))) {
			return -1;
		}
		if (!outside_any) {
			for (int i = 0; i < 3; i++) {
				g_clip_out[out++] = tri[i].v;
			}
			continue;
		}
		/* Sutherland-Hodgman against each plane */
		ClipVertex poly[2][CLIP_MAX];
		int n = 3, cur = 0;
		for (int i = 0; i < 3; i++) {
			poly[0][i] = tri[i];
		}
		for (int k = 0; k < CLIP_PLANES && n >= 3; k++) {
			ClipVertex *in = poly[cur], *res = poly[cur ^ 1];
			int m = 0;
			for (int i = 0; i < n; i++) {
				const ClipVertex *a = &in[i], *b = &in[(i + 1) % n];
				float da = plane_dist(a, k), db = plane_dist(b, k);
				if (da >= 0.0f) {
					res[m++] = *a;
				}
				if ((da >= 0.0f) != (db >= 0.0f) && m < CLIP_MAX) {
					res[m++] = lerp_vertex(a, b, da / (da - db));
				}
			}
			n = m;
			cur ^= 1;
		}
		for (int i = 1; i + 1 < n; i++) {
			g_clip_out[out++] = poly[cur][0].v;
			g_clip_out[out++] = poly[cur][i].v;
			g_clip_out[out++] = poly[cur][i + 1].v;
		}
	}
	*out_v = g_clip_out;
	return out;
}
