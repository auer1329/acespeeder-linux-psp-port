/* Race cameras, ported from 0x401180.. with the same floating point operation
 * order as the original (see machine.c). Verified by re/scripts/difftest_camera.py. */
#include "camera.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "machine.h"
#include "track.h"

_Static_assert(offsetof(Camera, mode) == 0x30, "Camera layout");
_Static_assert(offsetof(Camera, target) == 0xcc, "Camera layout");
_Static_assert(offsetof(Camera, sway_phase) == 0x184, "Camera layout");
_Static_assert(offsetof(Camera, shot_p) == 0x150, "Camera layout");
_Static_assert(sizeof(Camera) == 0x1a0, "Camera layout");

Camera g_camera;
Vec3 g_cameraFramePos;
Mat3 g_cameraFrameRot;


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

static void set_frame(Camera *c)
{
	g_cameraFramePos = c->pos;
	g_cameraFrameRot = c->rot;
}

/* Three slow sine wobbles applied to the camera rotation (X, Z, Y). */
static void wobble(Camera *c, float *phase, double kz_phase, double kz, double ky_phase, double ky)
{
	*phase = DADD(*phase, 0.03);
	float r = *phase;
	c->rot = Mat3_Mul(Mat3_Rotate(x87_muld(sin((double)r), 0.2), 0), c->rot);
	c->rot = Mat3_Mul(Mat3_Rotate(x87_muld(sin((double)DMUL(r, kz_phase)), kz), 2), c->rot);
	c->rot = Mat3_Mul(Mat3_Rotate(x87_muld(sin((double)DMUL(r, ky_phase)), ky), 1), c->rot);
}

/* Keyframed fly-in during the rolling start, in the player's frame. */
void Camera_Intro(Camera *c, int variant)
{
	static const float key_t[7] = {0, 250, 350, 450, 550, 650, 10000};
	Vec3 key_p[7] = {{300, 200, 100}, {-150, 20, 100}, {-30, 20, 0}, {0, 50, -85},
	                 {0, 50, -85}, {0, 50, -85}, {0, 50, -85}};
	Vec3 key_q[7] = {{0, 10, 0}, {0, 10, 0}, {0, 0, 0}, {0, 30, 25}, {0, 30, 25}, {0, 30, 25}, {0, 30, 25}};
	if (variant == 0) {
		key_p[0] = v3(-300, 5000, 100);
		key_q[0] = key_q[1] = v3(0, 0, 0);
	}
	Machine *p = &g_machines[0];
	int k, idx = 0;
	float f = 0.0f;
	for (k = 1; k <= 6; k++) {
		if ((double)key_t[k] > g_phaseTime) {
			break;
		}
	}
	if (k <= 6) {
		f = x87_add(g_phaseTime, -(double)key_t[k - 1]) / (key_t[k] - key_t[k - 1]);
		idx = k - 1;
	}
	else {
		idx = 5; /* the original reads uninitialised locals here; the race never gets this far */
		f = 1.0f;
	}
	float g = 1.0f - f;
	Vec3 a = key_p[idx], b = key_p[idx + 1];
	Vec3 from = v3(f * b.x + g * a.x, f * b.y + g * a.y, f * b.z + g * a.z);
	a = key_q[idx], b = key_q[idx + 1];
	Vec3 to = v3(f * b.x + g * a.x, f * b.y + g * a.y, f * b.z + g * a.z);
	Mat3 l = Mat3_LookDir(from, to);
	Vec3 o = Mat3_MulVec(from, p->rot);
	c->pos = v3(o.x + p->pos.x, o.y + p->pos.y, o.z + p->pos.z);
	c->rot = Mat3_Mul(l, p->rot);
	set_frame(c);
}

/* Point the lagging chase matrix from the current camera to the machine, so the
 * chase camera takes over smoothly. */
void Camera_StartRace(Camera *c)
{
	Machine *p = &g_machines[0];
	float dx = p->pos.x - c->pos.x, dy = p->pos.y - c->pos.y, dz = p->pos.z - c->pos.z;
	float len = sqrtf((dz * dz + dy * dy) + dx * dx);
	c->dist = len;
	float *L = c->lag.m;
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
	len = sqrtf((L[5] * L[5] + L[3] * L[3]) + L[4] * L[4]);
	L[3] = L[3] / len;
	L[4] = L[4] / len;
	L[5] = L[5] / len;
	c->smooth = c->rot;
}

/* Slow orbit around the machine after crossing the line. */
void Camera_Finish(Camera *c)
{
	Machine *p = &g_machines[0];
	float a = DMUL(c->finish_angle, 0.02);
	Vec3 v = v3(x87_muld(sin((double)a), 200.0), 100.0f, x87_muld(cos((double)a), 200.0));
	Vec3 o = Mat3_MulVec(v, p->rot);
	c->pos = v3(p->pos.x + o.x, p->pos.y + o.y, p->pos.z + o.z);
	c->rot = Mat3_LookDir(c->pos, p->pos);
	wobble(c, &c->finish_wobble, 0.5, 0.3, 0.7, 0.3);
	set_frame(c);
	c->finish_angle = DADD(c->finish_angle, 1.65);
}

void Camera_CrashHigh(Camera *c)
{
	c->rot = Mat3_LookDir(c->pos, g_machines[0].pos);
	set_frame(c);
}

void Camera_CrashLow(Camera *c)
{
	Machine *p = &g_machines[0];
	double s = sin((double)x87_muld(g_phaseTime, 0.02));
	float r = x87_add(x87_muld(s, 300.0), 400.0);
	c->pos.x = x87_mul(r, s) + p->pos.x;
	c->pos.y = x87_add(x87_muld(cos((double)x87_muld(g_phaseTime, 0.025)), 100.0), 200.0);
	c->pos.z = x87_mul(r, cos((double)x87_muld(g_phaseTime, 0.02))) + p->pos.z;
	c->rot = Mat3_LookDir(c->pos, p->pos);
	wobble(c, &c->crash_wobble, 0.5, 0.5, 0.7, 0.3);
	set_frame(c);
}

/* Shared tail of the time-over and power-out cameras: an offset in the
 * machine's frame, looking back at it. */
static void orbit_machine(Camera *c, Vec3 v, float *ph)
{
	Machine *p = &g_machines[0];
	Mat3 l = Mat3_LookDir(v, v3(0, 0, 0));
	Vec3 o = Mat3_MulVec(v, p->rot);
	c->pos = Vec3_Add(o, p->pos);
	c->rot = Mat3_Mul(l, p->rot);
	wobble(c, ph, 0.5, 0.3, 0.7, 0.3);
	set_frame(c);
}

void Camera_TimeOver(Camera *c)
{
	float x = x87_add(x87_muld(g_phaseTime, 0.02), 3.15);
	float r = x87_add(DMUL(x87_add(cos((double)x), 1.0), 500.0), 5.0);
	float a = x87_muld(g_phaseTime, 0.01);
	Vec3 v;
	v.x = x87_mul(r, cos((double)a));
	float y = x87_add(x87_muld(g_phaseTime, 0.005), 3.15);
	v.y = x87_add(DMUL(x87_add(cos((double)y), 1.0), 400.0), 50.0);
	v.z = x87_mul(r, sin((double)a));
	orbit_machine(c, v, &c->timeover_wobble);
}

void Camera_NoPower(Camera *c)
{
	float r = DMUL(x87_add(x87_add(cos((double)x87_muld(g_phaseTime, 0.02)), 1.0), 1.0), 100.0);
	float b = x87_muld(g_phaseTime, 0.012);
	double k = x87_add(sin((double)b), 1.2);
	float a = x87_muld(g_phaseTime, 0.01);
	Vec3 v;
	v.x = x87_muld(x87_mul(r, cos((double)a)), k);
	v.y = DMUL(x87_add(cos((double)b), 1.2) * r, 0.7);
	v.z = x87_muld(x87_mul(r, sin((double)a)), k);
	orbit_machine(c, v, &c->nopower_wobble);
}

/* Drifts towards the start line, alternating sides. */
void Camera_GameOver(Camera *c)
{
	Vec3 P;
	Mat3 F;
	Track_PointFrame(TrackPos_Make(0.0f, 3.5f, 180.0f), &P, &F);
	Vec3 d = Mat3_MulVecT(v3(c->pos.x - P.x, c->pos.y - P.y, c->pos.z - P.z), F);
	Vec3 g;
	g.x = x87_muld(sin((double)x87_muld(g_phaseTime, 0.02)), 25.0);
	if (!(d.z >= 0.0f)) {
		g.y = x87_muld(cos((double)x87_muld(g_phaseTime, 0.015)), 10.0);
		g.z = x87_add(x87_muld(sin((double)x87_muld(g_phaseTime, 0.01)), 30.0), -140.0);
	}
	else {
		g.y = x87_muld(cos((double)x87_muld(g_phaseTime, 0.03)), 10.0);
		g.z = x87_add(x87_muld(sin((double)x87_muld(g_phaseTime, 0.01)), 30.0), 140.0);
	}
	Vec3 goal = Vec3_Add(Mat3_MulVec(g, F), P);
	c->pos.x = DMUL(c->pos.x - goal.x, 0.9) + goal.x;
	c->pos.y = DMUL(c->pos.y - goal.y, 0.9) + goal.y;
	c->pos.z = DMUL(c->pos.z - goal.z, 0.9) + goal.z;
	Vec3 l = Mat3_MulVecT(v3(c->pos.x - P.x, c->pos.y - P.y, c->pos.z - P.z), F);
	Vec3 t = v3(x87_muld(sin((double)x87_muld(g_phaseTime, 0.02)), 25.0),
	            x87_muld(cos((double)x87_muld(g_phaseTime, 0.015)), 10.0), 0.0f);
	c->rot = Mat3_Mul(Mat3_LookDir(l, t), F);
	set_frame(c);
}

void Camera_ReplayReset(Camera *c)
{
	c->shot = 0;
	c->shot_start = 0.0f;
	c->shot_end = 0.0f;
}

static float wrap_progress(float v)
{
	int n = g_track.n;
	if ((double)n < floor((double)v)) {
		v = v - (float)(n + 1);
	}
	if (v < 0.0f) {
		v = (float)n + v + 1.0f;
	}
	return v;
}

static Vec3 rand_track_point(TrackPos *store, float s, float h)
{
	float l = x87_add(DMUL(RandF(), 0.8), 3.1);
	*store = TrackPos_Make(s, l, h);
	return Track_Point(&g_track, *store);
}

/* An offset in the track frame at `tp`, made absolute. */
static Vec3 track_offset(Vec3 v, TrackPos *store, float s)
{
	*store = TrackPos_Make(s, 3.5f, 0.0f);
	Vec3 o = Mat3_MulVec(v, Track_Frame(&g_track, *store));
	return Vec3_Add(o, Track_Point(&g_track, *store));
}

/* Pick the next replay shot covering a window around the machine. dir: 0 first
 * shot, 1 moving forward, 2 moving backward; shot 99 = random. */
void Camera_ReplayShot(Camera *c, TrackPos p, int lap, int dir, int shot)
{
	float w = (2500.0f / g_track.radius[p.seg] + 0.2f) - x87_mul(RandF(), 0.2);
	float base = (float)((g_track.n + 1) * lap + p.seg) + p.t;
	if (dir == 0) {
		c->shot_start = base - DMUL(w, 0.25);
		c->shot_end = DMUL(w, 0.75) + base;
	}
	else if (dir == 1) {
		c->shot_start = c->shot_end;
		c->shot_end = base + w;
	}
	else if (dir == 2) {
		c->shot_end = c->shot_start;
		c->shot_start = base - w;
	}
	float laps = (float)((g_track.n + 1) * lap);
	float s0 = c->shot_start - laps;
	float s1 = c->shot_end - laps;
	float mid = DMUL(s1 + s0, 0.5);
	s0 = wrap_progress(s0);
	s1 = wrap_progress(s1);
	mid = wrap_progress(mid);
	c->shot = (int16_t)(shot == 99 ? crt_rand() % 7 : shot);
	/* no high or wide shots inside tunnels */
	if (g_track.ptype[(int16_t)ftol(floor(s0))] == 3 && c->shot > 2) {
		c->shot = 0;
	}
	if (g_track.ptype[(int16_t)ftol(floor(s1))] == 3 && c->shot > 2) {
		c->shot = 0;
	}
	switch (c->shot) {
	case 0:
		c->shot_p[0] = rand_track_point(&c->shot_a, mid - 0.15f, 200.0f);
		c->shot_p[2] = rand_track_point(&c->shot_c, mid + 0.15f, 150.0f);
		break;
	case 1:
		c->shot_p[0] = rand_track_point(&c->shot_a, mid, 150.0f);
		break;
	case 2: {
		float z = DADD(DMUL(RandF(), 100.0), -50.0);
		float y = DADD(DMUL(RandF(), 50.0), 20.0);
		float x = DADD(DMUL(RandF(), 100.0), -50.0);
		c->shot_p[0] = v3(x, y, z);
		break;
	}
	case 3: {
		float x = DADD(DMUL(RandF(), 1000.0), -500.0);
		c->shot_p[0] = track_offset(v3(x, 100.0f, 0.0f), &c->shot_a, DMUL(mid + s0, 0.5));
		x = DADD(DMUL(RandF(), 1000.0), -500.0);
		c->shot_p[1] = track_offset(v3(x, 200.0f, 0.0f), &c->shot_b, mid);
		RandF();
		float y = DADD(DMUL(RandF(), 800.0), 200.0);
		x = DADD(DMUL(RandF(), 1000.0), -500.0);
		c->shot_p[2] = track_offset(v3(x, y, 0.0f), &c->shot_c, mid);
		break;
	}
	case 4:
		c->shot_p[0] = rand_track_point(&c->shot_a, mid - 0.15f, 600.0f);
		c->shot_p[2] = rand_track_point(&c->shot_c, mid + 0.15f, 100.0f);
		break;
	case 5:
		for (int i = 0; i < 3; i++) {
			float z = RandF() * 400.0f - 200.0f;
			float y = RandF() * 120.0f + 50.0f;
			float x = RandF() * 400.0f - 200.0f;
			c->shot_p[i] = v3(x, y, z);
		}
		break;
	case 6:
		c->orbit_a0 = DMUL(RandF(), 6.28);
		c->orbit_da = DADD(DMUL(RandF(), 6.28), -3.14);
		c->orbit_r0 = DADD(DMUL(RandF(), 2000.0), 20.0);
		c->orbit_r1 = DADD(DMUL(RandF(), 100.0), 20.0);
		break;
	}
}

void Camera_Replay(Camera *c, TrackPos p, int lap)
{
	Machine *m = &g_machines[0];
	float prog = (float)((g_track.n + 1) * lap) + (float)p.seg + p.t;
	if (c->shot_end < prog) {
		Camera_ReplayShot(c, p, lap, 1, 99);
	}
	if (prog < c->shot_start) {
		Camera_ReplayShot(c, p, lap, 2, 99);
	}
	float k = 1.0f - (prog - c->shot_start) / (c->shot_end - c->shot_start);
	k = 1.0f - k * k;
	Mat3 frame = Track_Frame(&g_track, m->tp);
	switch (c->shot) {
	case 0:
	case 4:
		c->pos = Vec3_Lerp(c->shot_p[0], c->shot_p[2], k);
		break;
	case 1:
		c->pos = c->shot_p[0];
		break;
	case 2:
		c->pos = Vec3_Add(Mat3_MulVec(c->shot_p[0], frame), m->pos);
		break;
	case 3:
		c->pos = Vec3_Quad(c->shot_p[0], c->shot_p[1], c->shot_p[2], k);
		break;
	case 5:
		c->pos = Vec3_Quad(c->shot_p[0], c->shot_p[1], c->shot_p[2], k);
		c->pos = Vec3_Add(Mat3_MulVec(c->pos, frame), m->pos);
		break;
	case 6: {
		float r = k * c->orbit_r1 + c->orbit_r0 * (1.0f - k);
		float a = k * c->orbit_da + c->orbit_a0;
		Vec3 v = v3(x87_mul(r, cos((double)a)), (1.0f - k) * 300.0f + k * 20.0f, x87_mul(r, sin((double)a)));
		c->pos = Vec3_Add(Mat3_MulVec(v, frame), m->pos);
		break;
	}
	default:
		goto wob;
	}
	c->rot = Mat3_LookDir(c->pos, m->pos);
wob:
	c->replay_wobble = DADD(c->replay_wobble, 0.03);
	{
		float r = c->replay_wobble;
		c->rot = Mat3_Mul(Mat3_Rotate(x87_muld(sin((double)r), 0.2), 0), c->rot);
		c->rot = Mat3_Mul(Mat3_Rotate(x87_muld(sin((double)DMUL(r, 0.2)), 0.3), 2), c->rot);
		c->rot = Mat3_Mul(Mat3_Rotate(x87_muld(sin((double)DMUL(r, 0.3)), 0.3), 1), c->rot);
	}
	set_frame(c);
}
