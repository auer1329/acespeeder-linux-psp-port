#include "gmath.h"

#include <math.h>

/* Round p+e (the exact value, e the tiny remainder) to float with a single rounding:
 * (float)p alone can double-round when p lies exactly between two floats. */
static float round_exact(double p, double e)
{
	float r = (float)p;
	double d = (double)r - p;
	if (e != 0.0 && d != 0.0) {
		float other = nextafterf(r, d > 0.0 ? -INFINITY : INFINITY);
		if ((double)other - p == -d && (e > 0.0) == (other > r)) {
			r = other;
		}
	}
	return r;
}

float x87_mul(float a, double c)
{
	double p = (double)a * c;
	return round_exact(p, fma((double)a, c, -p));
}

float x87_add(float a, double c)
{
	double s = (double)a + c;
	double bb = s - (double)a; /* TwoSum: exact error of the addition */
	double e = ((double)a - (s - bb)) + (c - bb);
	return round_exact(s, e);
}

void TrackPos_Update(TrackPos *p)
{
	p->t2 = p->t * p->t;
	p->omt = 1.0f - p->t;
	p->omt2 = p->omt * p->omt;
	p->omu = 1.0f - p->u;
}

TrackPos TrackPos_Make(float s, float l, float h)
{
	TrackPos p = {0};
	p.seg = (int16_t)ftol(floor(s));
	p.t = s - (float)floor(s);
	p.slot = (int16_t)ftol(floor(l));
	p.u = l - (float)floor(l);
	p.h = h;
	TrackPos_Update(&p);
	return p;
}

Mat3 Mat3_Identity(void)
{
	Mat3 m = {{1, 0, 0, 0, 1, 0, 0, 0, 1}};
	return m;
}

Mat3 Mat3_Mul(Mat3 a, Mat3 b)
{
	Mat3 r;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			/* summation order of the original: third term first */
			r.m[i * 3 + j] = (b.m[6 + j] * a.m[i * 3 + 2] + b.m[3 + j] * a.m[i * 3 + 1]) + b.m[j] * a.m[i * 3];
		}
	}
	return r;
}

Mat3 Mat3_MulT(Mat3 a, Mat3 b)
{
	Mat3 r;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r.m[i * 3 + j] = (b.m[j * 3 + 2] * a.m[i * 3 + 2] + b.m[j * 3 + 1] * a.m[i * 3 + 1]) +
			                 b.m[j * 3] * a.m[i * 3];
		}
	}
	return r;
}

Vec3 Vec3_Set(float x, float y, float z)
{
	return v3(x, y, z);
}

float Vec3_Dist(Vec3 a, Vec3 b)
{
	float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
	return sqrtf(dz * dz + dy * dy + dx * dx);
}

Vec3 Vec3_Lerp(Vec3 a, Vec3 b, float t)
{
	float s = 1.0f - t;
	return v3(b.x * t + a.x * s, b.y * t + a.y * s, b.z * t + a.z * s);
}

Vec3 Vec3_Quad(Vec3 a, Vec3 b, Vec3 c, float t)
{
	float s = (1.0f - t) * (1.0f - t), t2 = t * t;
	return v3((a.x - b.x) * s + (c.x - b.x) * t2 + b.x,
	          (a.y - b.y) * s + (c.y - b.y) * t2 + b.y,
	          (a.z - b.z) * s + (c.z - b.z) * t2 + b.z);
}

Vec3 Vec3_Add(Vec3 a, Vec3 b)
{
	return v3(b.x + a.x, b.y + a.y, b.z + a.z);
}

Mat3 Mat3_Rotate(float angle, int axis)
{
	Mat3 m = Mat3_Identity();
	/* fsin/fcos are not affected by the x87 precision setting: round once to float */
	float c = (float)cos((double)angle), s = (float)sin((double)angle);
	if (axis == 0) {
		m.m[4] = c, m.m[5] = s, m.m[7] = -s, m.m[8] = c;
	}
	else if (axis == 1) {
		m.m[8] = c, m.m[6] = s, m.m[2] = -s, m.m[0] = c;
	}
	else if (axis == 2) {
		m.m[0] = c, m.m[1] = -s, m.m[3] = s, m.m[4] = c;
	}
	return m;
}

Mat3 Mat3_LookDir(Vec3 from, Vec3 to)
{
	Mat3 m;
	Vec3 d = v3(to.x - from.x, to.y - from.y, to.z - from.z);
	float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
	d = v3(d.x / l, d.y / l, d.z / l);
	Vec3 r = v3(d.z - 0.0f, 0.0f, 0.0f - d.x);
	l = sqrtf(r.x * r.x + r.z * r.z + 0.0f);
	r = v3(r.x / l, 0.0f / l, r.z / l);
	Vec3 u = v3(r.z * d.y - r.y * d.z, r.x * d.z - r.z * d.x, r.y * d.x - r.x * d.y);
	l = sqrtf(u.x * u.x + u.y * u.y + u.z * u.z);
	m.m[0] = r.x, m.m[1] = r.y, m.m[2] = r.z;
	m.m[3] = u.x / l, m.m[4] = u.y / l, m.m[5] = u.z / l;
	m.m[6] = d.x, m.m[7] = d.y, m.m[8] = d.z;
	return m;
}

Vec3 Mat3_MulVec(Vec3 v, Mat3 m)
{
	return v3((m.m[6] * v.z + m.m[3] * v.y) + m.m[0] * v.x,
	          (m.m[7] * v.z + m.m[4] * v.y) + m.m[1] * v.x,
	          (m.m[8] * v.z + m.m[5] * v.y) + m.m[2] * v.x);
}

Vec3 Mat3_MulVecT(Vec3 v, Mat3 m)
{
	return v3((m.m[2] * v.z + m.m[1] * v.y) + m.m[0] * v.x,
	          (m.m[5] * v.z + m.m[4] * v.y) + m.m[3] * v.x,
	          (m.m[8] * v.z + m.m[7] * v.y) + m.m[6] * v.x);
}

uint32_t g_holdrand = 1; /* MSVC rand() state (0x44a82c), shared for tests */

void crt_srand(uint32_t seed)
{
	g_holdrand = seed;
}

int crt_rand(void)
{
	g_holdrand = g_holdrand * 0x343fd + 0x269ec3;
	return (int)(g_holdrand >> 16 & 0x7fff);
}

float RandF(void)
{
	return (float)((crt_rand() % 10000) * 0.0001);
}

float Wave_Decay(float x)
{
	float v = x * 1.8f;
	if (v > 3.14f) {
		return 0.0f;
	}
	return (float)((3.14 - v) * (cos(v) + 0.5) * 0.6666666666666666);
}

float Wave_Bounce(float x)
{
	/* The original rebuilds this table on every call (its "initialised" flag is never set). */
	static float t[200];
	t[0] = 1.0f;
	t[1] = 0.08f;
	for (int i = 2; i < 200; i++) {
		t[i] = t[i - 1] * 0.7f;
	}
	double v = (double)x + 0.5;
	int i = 0;
	while (i < 200 && !(v < t[i])) {
		v -= t[i];
		i++;
	}
	if (i > 198) {
		return 0.0f;
	}
	return (float)((v / t[i]) * (v / t[i] - 1.0) * t[i] * -4.0);
}

float Wave_EaseIn(float x)
{
	if (x > 1.57f) {
		return 1.0f;
	}
	double s = sin(1.57 - x);
	return (float)(1.0 - s * s);
}
