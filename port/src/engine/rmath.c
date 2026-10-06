#include "rmath.h"

#include <string.h>

void m4_identity(Mat4 *m)
{
	memset(m, 0, sizeof(*m));
	m->m[0][0] = m->m[1][1] = m->m[2][2] = m->m[3][3] = 1.0f;
}

void m4_mul(Mat4 *out, const Mat4 *a, const Mat4 *b)
{
	Mat4 r;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] +
			            a->m[i][2] * b->m[2][j] + a->m[i][3] * b->m[3][j];
		}
	}
	*out = r;
}

Vec3 m4_transform(const Mat4 *m, Vec3 v)
{
	return v3(v.x * m->m[0][0] + v.y * m->m[1][0] + v.z * m->m[2][0] + m->m[3][0],
	          v.x * m->m[0][1] + v.y * m->m[1][1] + v.z * m->m[2][1] + m->m[3][1],
	          v.x * m->m[0][2] + v.y * m->m[1][2] + v.z * m->m[2][2] + m->m[3][2]);
}

Vec3 m4_rotate(const Mat4 *m, Vec3 v)
{
	return v3(v.x * m->m[0][0] + v.y * m->m[1][0] + v.z * m->m[2][0],
	          v.x * m->m[0][1] + v.y * m->m[1][1] + v.z * m->m[2][1],
	          v.x * m->m[0][2] + v.y * m->m[1][2] + v.z * m->m[2][2]);
}

void m4_inverse_rigid(Mat4 *out, const Mat4 *m)
{
	Mat4 r;
	m4_identity(&r);
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			r.m[i][j] = m->m[j][i];
		}
	}
	Vec3 t = v3(m->m[3][0], m->m[3][1], m->m[3][2]);
	for (int j = 0; j < 3; j++) {
		r.m[3][j] = -(t.x * r.m[0][j] + t.y * r.m[1][j] + t.z * r.m[2][j]);
	}
	*out = r;
}

void m4_inverse(Mat4 *out, const Mat4 *m)
{
	/* Affine: invert the 3x3 part with cofactors, then the translation. */
	const float(*a)[4] = m->m;
	float c00 = a[1][1] * a[2][2] - a[1][2] * a[2][1];
	float c01 = a[1][2] * a[2][0] - a[1][0] * a[2][2];
	float c02 = a[1][0] * a[2][1] - a[1][1] * a[2][0];
	float det = a[0][0] * c00 + a[0][1] * c01 + a[0][2] * c02;
	float inv = det != 0.0f ? 1.0f / det : 0.0f;
	Mat4 r;
	m4_identity(&r);
	r.m[0][0] = c00 * inv;
	r.m[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * inv;
	r.m[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * inv;
	r.m[1][0] = c01 * inv;
	r.m[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * inv;
	r.m[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * inv;
	r.m[2][0] = c02 * inv;
	r.m[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * inv;
	r.m[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * inv;
	Vec3 t = v3(a[3][0], a[3][1], a[3][2]);
	for (int j = 0; j < 3; j++) {
		r.m[3][j] = -(t.x * r.m[0][j] + t.y * r.m[1][j] + t.z * r.m[2][j]);
	}
	*out = r;
}
