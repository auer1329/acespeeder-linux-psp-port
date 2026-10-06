/* The centre message quad. Every animation computes a half size, a centre and
 * a grey level, truncating with MSVC's __ftol, then fills the same quad. */
#include <math.h>

#include "gmath.h"
#include "hud.h"

HudMsg g_hudMsg;

void HudMsg_Init(HudMsg *m)
{
	for (int i = 0; i < 4; i++) {
		TLVertex *v = &m->v[i];
		v->sx = v->sy = v->sz = 0.0f;
		v->rhw = 1.0f;
		v->color = 0x80000000u;
		v->specular = 0;
		v->tu = v->tv = 0.0f;
	}
}

/* 1 - s with the x87's single-precision rounding */
static float one_minus(float s)
{
	return x87_add(1.0, -(double)s);
}

static void fill(HudMsg *m, int cx, int cy, int w, int h, int id, int grey, float du)
{
	int col = id % 2;
	float u = (float)col * 0.5f;
	float v = (float)(floor((double)(id / 2)) * 0.25);
	float u2 = DADD(u, du), v2 = DADD(v, 0.25);
	uint32_t c = 0x80000000u | (uint32_t)(grey & 0xff) << 16 | (uint32_t)(grey & 0xff) << 8 | (uint32_t)(grey & 0xff);
	const float x[4] = {(float)(cx - w), (float)(cx + w), (float)(cx + w), (float)(cx - w)};
	const float y[4] = {(float)(cy - h), (float)(cy - h), (float)(cy + h), (float)(cy + h)};
	const float tu[4] = {u, u2, u2, u}, tv[4] = {v, v, v2, v2};
	for (int i = 0; i < 4; i++) {
		m->v[i].sx = x[i];
		m->v[i].sy = y[i];
		m->v[i].color = c;
		m->v[i].tu = tu[i];
		m->v[i].tv = tv[i];
	}
}

static float clamp2(float s)
{
	return s > 2.0f ? 2.0f : s;
}

/* fade in quickly, hold, fade out after 1 */
static int grey_fast(float s)
{
	if (s < 0.2f) {
		return (int)DMUL(s * 200.0f, 5.0);
	}
	if (s < 1.0f) {
		return 200;
	}
	return (int)DMUL(x87_add(2.0, -(double)s), 200.0);
}

static int grey_linear(float s)
{
	if (s < 1.0f) {
		return (int)(s * 200.0f);
	}
	return (int)DMUL(x87_add(2.0, -(double)s), 200.0);
}

void HudMsg_Fail(HudMsg *m, int id, float s)
{
	int w = 240, h = 40, cy = 120;
	s = clamp2(s);
	if (s < 1.0f) {
		float g = one_minus(s), g2 = g * g;
		w = (int)DADD(DMUL(g2, 1000.0), 240.0);
		h = (int)x87_add(40.0, -(double)DMUL(g2, 45.0));
		cy = (int)DADD(DMUL(g * g, 230.0), 120.0);
	}
	fill(m, 320, cy, w, h, id, grey_fast(s), 0.5f);
}

void HudMsg_Start(HudMsg *m, int id, float s)
{
	int w, h, cx = 320, cy = 240;
	s = clamp2(s);
	if (s < 1.0f) {
		float g = one_minus(s), g2 = g * g;
		w = (int)DADD(DMUL(g2, 1000.0), 240.0);
		h = (int)x87_add(40.0, -(double)DMUL(g2, 45.0));
		float k = DMUL(x87_mul(g, sin((double)DMUL(g, -4.0))), 600.0);
		cx = (int)x87_add(320.0, -(double)k);
	}
	else {
		float q = one_minus(DADD(s, -1.0)), r = one_minus(q * q);
		w = (int)DADD(DMUL(r, 960.0), 240.0);
		h = (int)x87_add(40.0, -(double)DMUL(r, 40.0));
		cy = (int)x87_add(240.0, -(double)DMUL(DADD(s, -1.0), 60.0));
	}
	fill(m, cx, cy, w, h, id, grey_fast(s), 0.5f);
}

void HudMsg_Goal(HudMsg *m, int id, float s)
{
	int w = 240, h, cy = 240;
	s = clamp2(s);
	if (s < 1.0f) {
		float g = one_minus(s);
		h = (int)DADD(DMUL(g * g, 160.0), 40.0);
		cy = (int)x87_add(240.0, -(double)DMUL(g * g, 300.0));
	}
	else {
		float g = DADD(s, -1.0);
		w = (int)DADD(DMUL(g, 2400.0), 240.0);
		h = (int)DADD(DMUL(g, 400.0), 40.0);
	}
	fill(m, 320, cy, w, h, id, grey_linear(s), 0.5f);
}

void HudMsg_GameOver(HudMsg *m, int id, float s)
{
	int w = 240, h = 40, cy = 100;
	s = clamp2(s);
	if (s < 1.0f) {
		float g = one_minus(s), g2 = g * g;
		w = (int)DADD(DMUL(g2, 720.0), 240.0);
		h = (int)DADD(DMUL(g2, 120.0), 40.0);
		cy = (int)DADD(DMUL(g * g, 140.0), 100.0);
	}
	fill(m, 320, cy, w, h, id, grey_linear(s), 0.5f);
}

void HudMsg_Wide(HudMsg *m, int id, float s)
{
	int w = 240, h = 40, cx = 320, cy = 140;
	s = clamp2(s);
	if (s < 1.0f) {
		if (s < 0.25f) {
			w = 1440, h = 240;
		}
		else {
			double c = cos((double)DMUL(DADD(s, -0.25), 2.0933333333333333));
			w = (int)DADD(x87_muld(c, 1200.0), 240.0);
			h = (int)DADD(x87_muld(c, 200.0), 40.0);
		}
		double c = cos((double)DMUL(s, 3.14));
		float k;
		if (s < 0.2f) {
			k = x87_muld(c, 1760.0);
		}
		else {
			float f = x87_add(1.0, -(double)DMUL(DADD(s, -0.2), 1.25));
			k = DMUL(x87_mul(f, c), 1760.0);
		}
		cx = (int)DADD(k, 320.0);
		if (s >= 0.5f) {
			float d = DADD(s, -0.5);
			cy = (int)DADD(DMUL(x87_add(1.0, -(double)(d + d)), 100.0), 140.0);
		}
		else {
			cy = 240;
		}
	}
	fill(m, cx, cy, w, h, id, s < 1.0f ? 200 : (int)DMUL(x87_add(2.0, -(double)s), 200.0), 1.0f);
}
