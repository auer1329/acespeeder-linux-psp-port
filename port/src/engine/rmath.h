/* Vector/matrix helpers in Direct3D RM conventions:
 * left-handed, +Y up, camera looks down +Z, row vectors (v' = v * M),
 * translation in the bottom row. */
#ifndef RMATH_H
#define RMATH_H

#include <math.h>

typedef struct {
	float x, y, z;
} Vec3;

/* m[row][col]; rows 0..2 are the X/Y/Z axes, row 3 the position. */
typedef struct {
	float m[4][4];
} Mat4;

static inline Vec3 v3(float x, float y, float z)
{
	Vec3 r = {x, y, z};
	return r;
}

static inline Vec3 v3_add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 v3_sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 v3_scale(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float v3_len(Vec3 a) { return sqrtf(v3_dot(a, a)); }

static inline Vec3 v3_cross(Vec3 a, Vec3 b)
{
	return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static inline Vec3 v3_norm(Vec3 a)
{
	float l = v3_len(a);
	return l > 0.0f ? v3_scale(a, 1.0f / l) : a;
}

void m4_identity(Mat4 *m);
void m4_mul(Mat4 *out, const Mat4 *a, const Mat4 *b); /* out = a * b (apply a, then b) */
Vec3 m4_transform(const Mat4 *m, Vec3 v);              /* point: includes translation */
Vec3 m4_rotate(const Mat4 *m, Vec3 v);                 /* direction: ignores translation */
void m4_inverse_rigid(Mat4 *out, const Mat4 *m);       /* inverse of rotation+translation (+uniform scale ignored) */
void m4_inverse(Mat4 *out, const Mat4 *m);             /* general affine inverse */

#endif
