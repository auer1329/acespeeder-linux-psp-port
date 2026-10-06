/* Race cameras, ported from 0x401180.. with the same floating point operation
 * order as the original (see machine.c). Verified by re/scripts/difftest_camera.py. */
#include "camera.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "machine.h"

_Static_assert(offsetof(Camera, mode) == 0x30, "Camera layout");
_Static_assert(offsetof(Camera, target) == 0xcc, "Camera layout");
_Static_assert(offsetof(Camera, sway_phase) == 0x184, "Camera layout");
_Static_assert(sizeof(Camera) == 0x1a0, "Camera layout");

Camera g_camera;
Vec3 g_cameraFramePos;
Mat3 g_cameraFrameRot;

#define DMUL(a, c) x87_mul((a), (c))
#define DADD(a, c) x87_add((a), (c))

/* Per-model camera stiffness (0x44a364). */
static const float k_cam_stiff[8] = {2.0f, 1.8f, 1.9f, 2.1f, 1.8f, 2.0f, 1.9f, 2.1f};
/* Chase views: pitch and distance (switch at 0x401209). */
static const uint32_t k_pitch_bits[4] = {0x3efa1564, 0x3ee8386f, 0x3f05f92c, 0x3ed65b7a};
static const float k_view_dist[4] = {100.0f, 150.0f, 300.0f, 75.0f};

static float bits_to_float(uint32_t b)
{
	float f;
	memcpy(&f, &b, 4);
	return f;
}

void Camera_Init(Camera *c)
{
	c->pos = v3(0, 0, 0);
	c->rot = Mat3_Identity();
	c->mode = 0;
	c->dist = 1000.0f;
	c->target_dist = 200.0f;
	c->base = Mat3_Identity();
	c->lag = Mat3_Identity();
	c->look = Mat3_Identity();
	c->smooth = Mat3_Identity();
	c->target = v3(0, 0, 0);
}

void Camera_Chase(Camera *c)
{
	Machine *p = &g_machines[0];
	if (c->mode >= 0 && c->mode <= 3) {
		c->base = Mat3_Mul(Mat3_Rotate(bits_to_float(k_pitch_bits[c->mode]), 0), p->rot);
		c->target_dist = k_view_dist[c->mode];
	}
	if (g_input[8] != 0 && g_inputPrev[8] == 0) {
		c->mode = 3;
	}
	if (g_input[9] != 0 && g_inputPrev[9] == 0) {
		c->mode = 0;
	}
	if (g_input[10] != 0 && g_inputPrev[10] == 0) {
		c->mode = 1;
	}
	if (g_input[11] != 0 && g_inputPrev[11] == 0) {
		c->mode = 2;
	}

	/* the offset direction lags behind the machine */
	Mat3 d = Mat3_MulT(c->base, c->lag);
	float a1 = DMUL(k_cam_stiff[p->model] * d.m[5], 0.08);
	float a2 = DMUL(d.m[6], 0.08);
	float a3 = DMUL(-d.m[1], 0.15);
	c->lag = Mat3_Mul(Mat3_Rotate(DMUL(a2, 1.65), 1), c->lag);
	c->lag = Mat3_Mul(Mat3_Rotate(DMUL(a1, 1.65), 0), c->lag);
	c->lag = Mat3_Mul(Mat3_Rotate(DMUL(a3, 1.65), 2), c->lag);

	/* distance: eases to the view's distance, stretched during boosts/jumps/paths */
	if (p->sp_active == 0) {
		c->dist = c->dist - DMUL(c->dist - c->target_dist, 0.1);
	}
	else if (p->sp_kind == 0 || p->sp_kind == 1) {
		float f = DADD(-DMUL(p->sp_timer, 0.041666666666666664), 1.0);
		f = f * f;
		c->target_dist = DADD(DMUL(DADD(-f, 1.0), 0.5), 1.0) * c->target_dist;
		c->dist = c->dist - DMUL(c->dist - c->target_dist, 0.15);
	}
	else if (p->sp_kind == 2) {
		float f = DADD(-(p->sp_timer / g_pickups.dur[p->path_idx]), 1.0);
		f = f * f;
		f = DADD(-f, 1.0);
		c->target_dist = DADD(f + f, 1.0) * c->target_dist;
		c->dist = c->dist - DMUL(c->dist - c->target_dist, 0.1);
	}

	/* sideways sway while steering hard */
	c->sway_phase = x87_muld(g_phaseTime, 1.81818);
	float sway = 0.0f;
	if (p->steer > 0.025) {
		sway = DMUL(x87_mul(DADD(p->steer, -0.025), sin((double)c->sway_phase)), 60.0);
	}
	if (p->steer < -0.025) {
		sway = DMUL(x87_mul(DADD(p->steer, 0.025), sin((double)c->sway_phase)), 60.0);
	}
	Vec3 off = Mat3_MulVec(v3(0.0f, sway, -c->dist), c->lag);
	c->pos.x = p->pos.x + off.x;
	c->pos.y = p->pos.y + off.y;
	c->pos.z = p->pos.z + off.z;

	/* look at a point above and ahead of the machine */
	Vec3 t = Mat3_MulVec(v3(0.0f, 30.0f, 25.0f), p->rot);
	c->target.x = p->pos.x + t.x;
	c->target.y = p->pos.y + t.y;
	c->target.z = p->pos.z + t.z;
	float dx = c->target.x - c->pos.x;
	float dy = c->target.y - c->pos.y;
	float dz = c->target.z - c->pos.z;
	float len = sqrtf((dz * dz + dy * dy) + dx * dx);
	float *L = c->look.m;
	L[6] = dx / len;
	L[7] = dy / len;
	L[8] = dz / len;
	L[3] = p->rot.m[3];
	L[4] = p->rot.m[4];
	L[5] = p->rot.m[5];
	L[0] = L[8] * L[4] - L[7] * L[5];
	L[1] = L[6] * L[5] - L[8] * L[3];
	L[2] = L[7] * L[3] - L[6] * L[4];
	len = sqrtf((L[2] * L[2] + L[1] * L[1]) + L[0] * L[0]);
	L[0] = L[0] / len;
	L[1] = L[1] / len;
	L[2] = L[2] / len;
	L[3] = L[2] * L[7] - L[1] * L[8];
	L[4] = L[0] * L[8] - L[2] * L[6];
	L[5] = L[6] * L[1] - L[0] * L[7];
	len = sqrtf((L[3] * L[3] + L[4] * L[4]) + L[5] * L[5]);
	L[3] = L[3] / len;
	L[4] = L[4] / len;
	L[5] = L[5] / len;

	/* the view direction lags too */
	d = Mat3_MulT(c->look, c->smooth);
	float b1 = DMUL(d.m[5], 0.3);
	float b2 = DMUL(d.m[6], 0.3);
	float b3 = DMUL(-d.m[1], 0.15);
	c->smooth = Mat3_Mul(Mat3_Rotate(DMUL(b2, 1.65), 1), c->smooth);
	c->smooth = Mat3_Mul(Mat3_Rotate(DMUL(b1, 1.65), 0), c->smooth);
	c->smooth = Mat3_Mul(Mat3_Rotate(DMUL(b3, 1.65), 2), c->smooth);
	c->rot = c->smooth;
	g_cameraFramePos = c->pos;
	g_cameraFrameRot = c->rot;
}
