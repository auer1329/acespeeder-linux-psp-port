/* The original game's small math library (0x41c550..0x41ce60), same names and
 * semantics. Mat3 is row-major; rows are the local X, Y, Z axes. */
#ifndef GMATH_H
#define GMATH_H

#include <stdint.h>

#include "../engine/rmath.h"

typedef struct {
	float m[9];
} Mat3;

/* A position on the track, 36 bytes like the original (passed by value there). */
typedef struct {
	int16_t seg;   /* segment index */
	int16_t pad0;
	float t;       /* 0..1 along the segment */
	int16_t slot;  /* cross-section edge 0..7 */
	int16_t pad1;
	float u;       /* 0..1 across the edge */
	float h;       /* height above the surface */
	float t2;      /* t*t */
	float omt;     /* 1-t */
	float omt2;    /* (1-t)^2 */
	float omu;     /* 1-u */
} TrackPos;

void TrackPos_Update(TrackPos *p);                              /* 0x41c550 */
TrackPos TrackPos_Make(float s, float l, float h);               /* 0x41c7f0 */
Mat3 Mat3_Identity(void);                                        /* 0x41c580 */
Mat3 Mat3_Mul(Mat3 a, Mat3 b);                                   /* 0x41c5b0: a*b */
Mat3 Mat3_MulT(Mat3 a, Mat3 b);                                  /* 0x41c6d0: a*transpose(b) */
Vec3 Vec3_Set(float x, float y, float z);                        /* 0x41c880 */
float Vec3_Dist(Vec3 a, Vec3 b);                                 /* 0x41c8c0 */
Vec3 Vec3_Lerp(Vec3 a, Vec3 b, float t);                         /* 0x41c900 */
Vec3 Vec3_Quad(Vec3 a, Vec3 b, Vec3 c, float t);                 /* 0x41c970 */
Vec3 Vec3_Add(Vec3 a, Vec3 b);                                   /* 0x41ca10 */
Mat3 Mat3_Rotate(float angle, int axis);                         /* 0x41ca60: axis 0=X 1=Y 2=Z */
Mat3 Mat3_LookDir(Vec3 from, Vec3 to);                           /* 0x41cb00 */
Vec3 Mat3_MulVec(Vec3 v, Mat3 m);                                /* 0x41cc50: v*m */
Vec3 Mat3_MulVecT(Vec3 v, Mat3 m);                               /* 0x41ccc0: v*transpose(m) */
float RandF(void);                                               /* 0x41cd30: [0,1) step 1e-4 */
float Wave_Decay(float x);                                       /* 0x41cd50 */
float Wave_Bounce(float x);                                      /* 0x41cd90 */
float Wave_EaseIn(float x);                                      /* 0x41ce30 */

/* MSVC runtime rand()/srand(), same LCG so seeded sequences match the original. */
void crt_srand(uint32_t seed);
int crt_rand(void);

/* x87 ops with a double operand (constants the original keeps as qwords) under
 * 24-bit precision control: the exact result rounded once to float. */
float x87_mul(float a, double c);
float x87_add(double a, double c);
float x87_div(double a, double c);
float x87_muld(double a, double c); /* both operands double, e.g. a qword global */
#define DMUL(a, c) x87_mul((a), (c))
#define DADD(a, c) x87_add((a), (c))

/* MSVC's __ftol truncates toward zero. */
static inline int ftol(double v) { return (int)v; }

#endif
