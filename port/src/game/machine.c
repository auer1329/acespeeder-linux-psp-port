/* Per-machine physics, ported from Machine_Update (0x404aa0) and its helpers.
 *
 * The original ran on an x87 FPU that Direct3D had switched to single precision,
 * so plain float arithmetic matches it; keep the operation order of the original
 * and build with -ffp-contract=off. Verified against the original binary with
 * re/scripts/difftest_machine.py. */
#include "machine.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "track.h"

Machine g_machines[NUM_MACHINES];
Vec3 g_playerFramePos;
Mat3 g_playerFrameRot;
Pickups g_pickups;
Sparks g_sparks;
int32_t g_input[14];
int32_t g_inputPrev[14];
double g_phaseTime;

/* Per-model tables (0x44a344..0x44a3e0); index 7 doubles as the autopilot set. */
static const float k_max_throttle[8] = {2.85f, 2.55f, 2.7f, 3.0f, 2.55f, 2.85f, 2.7f, 3.0f};
static const float k_boost[8] = {8, 6, 10, 8, 10, 6, 8, 10};
static const float k_jump_up[8] = {4, 3, 5, 4, 5, 3, 4, 5};
static const float k_jump_fwd[8] = {4, 3, 5, 4, 5, 3, 4, 5};

/* x87 code loads many constants as doubles: the operation then rounds to float
 * once. Doing it in double and narrowing reproduces that (the FPU ran at 24 bits). */

#define IN_A_ 4
#define IN_RIGHT_ 2
#define IN_LEFT_ 3

void Track_Move(TrackPos *p, float dx, float dy, float dz)
{
	TrackPos_Update(p);
	Mat3 f = Track_Frame(&g_track, *p);
	Vec3 p0 = Track_Point(&g_track, *p);
	p->t = DADD(p->t, 0.05);
	Vec3 p1 = Track_Point(&g_track, *p);
	float t0 = DADD(p->t, -0.05);
	float ex = p1.x - p0.x, ey = p1.y - p0.y, ez = p1.z - p0.z;
	p->t = t0;
	int k = p->slot + p->seg * 8;
	/* sums run z, y, x like the original */
	p->h = ((f.m[5] * dz + f.m[4] * dy) + f.m[3] * dx) + p->h;
	p->t = ((f.m[8] * dz + f.m[7] * dy) + f.m[6] * dx) / (sqrtf((ez * ez + ey * ey) + ex * ex) * 20.0f) + t0;
	p->u = ((f.m[2] * dz + f.m[1] * dy) + f.m[0] * dx) /
	       ((g_track.ax[k + 1] - g_track.ax[k]) * p->omt + (g_track.bx[k + 1] - g_track.bx[k]) * t0) + p->u;
}

void Track_PointFrame(TrackPos p, Vec3 *pos, Mat3 *rot)
{
	*rot = Track_Frame(&g_track, p);
	*pos = Track_Point(&g_track, p);
}

TrackPos Pickups_PathPos(float timer, int idx, TrackPos start)
{
	const TrackPos *a = &g_pickups.from[idx], *b = &g_pickups.to[idx];
	float k = timer / g_pickups.dur[idx];
	k = k * k;
	float w = (1.0f - k) * (1.0f - k);
	float s0 = (float)a->seg + a->t;
	float s = (((float)start.seg + start.t) - s0) * w + (((float)b->seg + b->t) - s0) * k + s0;
	float l0 = (float)a->slot + a->u;
	float l = (((float)start.slot + start.u) - l0) * w + (((float)b->slot + b->u) - l0) * k + l0;
	TrackPos out = start; /* the original leaves the derived fields uninitialised */
	out.h = (start.h - a->h) * w + (b->h - a->h) * k + a->h;
	out.seg = (int16_t)ftol(floor(s));
	out.t = s - (float)out.seg;
	out.slot = (int16_t)ftol(floor(l));
	out.u = l - (float)out.slot;
	return out;
}

Vec3 Pickups_PathPoint(float t2, int idx, TrackPos start)
{
	float f = t2 / g_pickups.dur[idx];
	Vec3 p0 = Track_Point(&g_track, start);
	Vec3 p1 = Track_Point(&g_track, g_pickups.from[idx]);
	Vec3 p2 = Track_Point(&g_track, g_pickups.to[idx]);
	return Vec3_Quad(p0, p1, p2, f);
}

int Sparks_Add(Vec3 v)
{
	int i = g_sparks.next;
	g_sparks.active[i] = 1;
	g_sparks.life[i] = 0.0f;
	g_sparks.pos[i] = v;
	g_sparks.vel[i].x = (float)cos((double)crt_rand());
	g_sparks.vel[i].z = (float)sin((double)crt_rand());
	double c = cos((double)crt_rand()); /* stays in an x87 register: not rounded */
	g_sparks.vel[i].y = x87_add(x87_add(x87_add(c, 1.5), c), 1.5);
	g_sparks.next = (int16_t)((i + 1) % 50);
	return (i + 1) / 50;
}

static void sparks4(Vec3 v)
{
	for (int i = 0; i < 4; i++) {
		Sparks_Add(v);
	}
}

/* Push the machine off a wall: slow down, lose power, reflect the lateral
 * velocity in the track frame and throw sparks towards the wall. */
static void wall_bounce(Machine *m, float u_new, float push, float target_slot)
{
	m->tp.u = u_new;
	m->throttle = DMUL(m->throttle, 0.97);
	m->power = DADD(m->power, -0.6);
	m->vel = Mat3_MulVecT(m->vel, Track_Frame(&g_track, m->tp));
	m->vel.x = DMUL(m->vel.x, 0.9) + push;
	m->vel.y = DMUL(m->vel.y, 0.9);
	m->vel.z = DMUL(m->vel.z, 0.9);
	m->vel = Mat3_MulVec(m->vel, Track_Frame(&g_track, m->tp));
	TrackPos tp = TrackPos_Make((float)m->tp.seg + m->tp.t, target_slot, m->tp.h);
	Vec3 tgt = Track_Point(&g_track, tp);
	Vec3 d = v3(tgt.x - m->pos.x, tgt.y - m->pos.y, tgt.z - m->pos.z);
	d = Mat3_MulVecT(d, m->rot);
	m->hit_time = 0.0f;
	d.y = 0.0f;
	d.x = d.x * 20.0f / sqrtf(d.x * d.x + d.z * d.z);
	d.z = d.z * 10.0f / sqrtf(d.x * d.x + d.z * d.z);
	if (m->is_ai == 0) {
		Sound_SetPan(3, ftol(d.x * 450.0f));
		Sound_Play(3);
		for (int i = 0; i < 4; i++) {
			Sparks_Add(v3(d.x, 10.0f, d.z - 15.0f));
		}
	}
}

/* Riding into the road edge from outside: snap back without slowing down. */
static void edge_snap(Machine *m, float u_new)
{
	m->tp.u = u_new;
	if (m->is_ai == 0) {
		sparks4(v3(0.0f, 10.0f, -5.0f));
	}
}

void Machine_Update(Machine *m)
{
	/* --- special moves started by pads: boost, jump, path ride */
	if (m->sp_active == 1) {
		if (m->sp_kind == 2) {
			if (m->sp_timer < g_pickups.dur[m->path_idx]) {
				m->tp = Pickups_PathPos(m->sp_timer, m->path_idx, m->path_tp);
				Track_PointFrame(m->tp, &m->pos, &m->rot);
				m->pos = Pickups_PathPoint(m->sp_timer * m->sp_timer, m->path_idx, m->path_tp);
				m->sp_timer = m->sp_timer + 1.0f;
				return;
			}
			m->sp_active = 0;
			m->steer = 0.0f;
			m->vel = Mat3_MulVec(v3(0.0f, 0.0f, 50.0f), m->rot);
		}
		else if (m->sp_kind == 1) {
			if (m->sp_timer < 16.0f) {
				int mi = m->autopilot == 1 ? 7 : m->model;
				Vec3 j = Mat3_MulVec(v3(0.0f, k_jump_up[mi], k_jump_fwd[mi]), m->rot);
				m->vel = Vec3_Add(m->vel, j);
				m->sp_timer = m->sp_timer + 1.0f;
			}
			else {
				m->sp_active = 0;
			}
		}
		else if (m->sp_kind == 0) {
			if (m->sp_timer < 16.0f) {
				int mi = m->autopilot == 1 ? 7 : m->model;
				Vec3 b = Mat3_MulVec(v3(0.0f, 0.0f, k_boost[mi]), m->rot);
				m->vel = Vec3_Add(m->vel, b);
				m->sp_timer = m->sp_timer + 1.0f;
			}
			else {
				m->sp_active = 0;
			}
		}
	}

	/* --- throttle (all human-driven machines read the same input) */
	if (m->autopilot == 0) {
		if (g_input[IN_A_] != 0 && !(g_track.reftime - m->time <= 0.0)) {
			m->throttle = DADD(m->throttle, 0.08);
			if (m->throttle > k_max_throttle[m->model]) {
				m->throttle = k_max_throttle[m->model];
			}
		}
		else {
			m->throttle = DADD(m->throttle, -0.08);
			if (m->throttle < 0.0f) {
				m->throttle = 0.0f;
			}
		}
	}
	else {
		m->throttle = k_max_throttle[7];
	}
	if (m->is_ai == 0) {
		Sound_SetFreq(8, ftol(m->throttle * 20000.0f + 3000.0f));
		Sound_SetPan(8, ftol(m->steer * -25000.0f));
	}

	/* --- steering */
	if (g_input[IN_LEFT_] != 0) {
		m->steer = DADD(m->steer, -0.004);
	}
	if (g_input[IN_RIGHT_] != 0) {
		m->steer = DADD(m->steer, 0.004);
	}
	if (g_input[IN_LEFT_] == 0 && g_input[IN_RIGHT_] == 0) {
		m->steer = DMUL(m->steer, 0.8);
	}
	if (m->steer > 0.03) {
		m->steer = 0.03f;
	}
	if (m->steer < -0.03) {
		m->steer = -0.03f;
	}
	float turn = m->steer < 0.0f ? -(m->steer * m->steer) * 50.0f : m->steer * m->steer * 50.0f;
	m->rot = Mat3_Mul(Mat3_Rotate(turn, 1), m->rot);

	/* --- gravity: towards the road surface while on it, straight down otherwise */
	Mat3 frame = Track_Frame(&g_track, m->tp);
	Vec3 grav;
	if (m->alive == 1) {
		float gy;
		if (g_track.ptype[m->tp.seg] == 2 && m->on_road == 1 && m->grounded == 1) {
			gy = m->tp.h < 20.0f ? DMUL(m->tp.h - 20.0f, -0.02) : -1.0f;
		}
		else {
			gy = -1.3f;
		}
		grav = Mat3_MulVec(v3(0.0f, gy, 0.0f), frame);
	}
	else {
		grav = v3(0.0f, -1.65f, 0.0f);
	}
	Vec3 drive = v3(m->steer * 8.0f, 0.0f, DMUL(m->throttle, 1.65));

	/* --- pads */
	for (int i = 0; i < g_pickups.count; i++) {
		if ((float)m->tp.seg != g_pickups.seg[i] || !(m->tp.t < g_pickups.trange[i][0]) ||
		    m->tp.t <= g_pickups.trange[i][1] || m->tp.u <= g_pickups.lrange[i][0] ||
		    !(m->tp.u < g_pickups.lrange[i][1]) || !(m->tp.h < 30.0f)) {
			continue;
		}
		if (m->is_ai == 0) {
			Sound_Play(1);
		}
		m->sp_active = 1;
		m->sp_timer = 0.0f;
		m->sp_kind = g_pickups.type[i];
		if (m->sp_kind == 0 || m->sp_kind == 1) {
			/* drop the vertical part of the velocity */
			Vec3 l = Mat3_MulVecT(m->vel, m->rot);
			l.y = 0.0f;
			m->vel = Mat3_MulVec(l, m->rot);
		}
		else if (m->sp_kind == 2) {
			m->path_idx = (int16_t)i;
			m->steer = 0.0f;
			m->path_tp = m->tp;
		}
		break;
	}
	m->sp_timer = m->sp_timer + 1.0f;

	/* --- integrate */
	Vec3 a = Mat3_MulVec(drive, m->rot);
	m->accel = v3(a.x + grav.x, a.y + grav.y, a.z + grav.z);
	Vec3 lv = Mat3_MulVecT(m->vel, m->rot);
	lv = v3(DMUL(lv.x, 0.88), DMUL(lv.y, 0.88), DMUL(lv.z, 0.88));
	m->vel = Mat3_MulVec(lv, m->rot);

	if (m->autopilot == 1) {
		TrackPos tp = m->tp;
		tp.t = DADD(m->tp.t, 0.075);
		tp.slot = 3;
		tp.u = 0.5f;
		Vec3 tgt = Track_Point(&g_track, tp);
		Vec3 d = v3(tgt.x - m->pos.x, tgt.y - m->pos.y, tgt.z - m->pos.z);
		Vec3 dl = Mat3_MulVecT(d, m->rot);
		if (dl.x > 100.0f) {
			m->steer = DADD(m->steer, 0.008);
		}
		else if (dl.x < -100.0f) {
			m->steer = DADD(m->steer, -0.008);
		}
		else {
			m->steer = DMUL(m->steer, 0.97);
		}
		if (m->steer > 0.025) {
			m->steer = 0.025f;
		}
		if (m->steer < -0.025) {
			m->steer = -0.025f;
		}
		float len = DMUL(sqrtf(d.z * d.z + d.y * d.y + d.x * d.x), 0.3);
		m->accel = v3(d.x / len + grav.x, d.y / len + grav.y, d.z / len + grav.z);
		m->rot = Track_Frame(&g_track, m->tp);
	}

	m->vel.x = m->vel.x + m->accel.x;
	m->vel.y = m->accel.y + m->vel.y;
	m->vel.z = m->vel.z + m->accel.z;
	Track_Move(&m->tp, DMUL(m->vel.x, 1.65), DMUL(m->vel.y, 1.65), DMUL(m->vel.z, 1.65));
	TrackPos_Update(&m->tp);

	/* --- road edges, walls and ceilings */
	int s = m->tp.seg;
	int k = m->tp.slot + s * 8;
	float w = (g_track.ptype[s] == 2 ? 18.0f : 25.0f) /
	          ((g_track.bx[k + 1] - g_track.bx[k]) * m->tp.t + (g_track.ax[k + 1] - g_track.ax[k]) * m->tp.omt);
	float c0 = (g_track.ax[s * 8 + 3] - g_track.ax[s * 8 + 2]) /
	           ((g_track.bx[s * 8 + 4] - g_track.bx[s * 8 + 3]) * m->tp.t +
	            (g_track.ax[s * 8 + 4] - g_track.ax[s * 8 + 3]) * m->tp.omt);
	float a1 = g_track.ay[s * 8 + 1] - g_track.ay[s * 8 + 3];
	float a2 = g_track.ay[s * 8 + 2] - g_track.ay[s * 8 + 3];
	switch (g_track.ptype[s]) {
	case 0:
	case 3:
	case 4:
	case 5:
		if (m->on_road == 1) {
			if (m->grounded != 1) {
				break;
			}
			if (m->tp.h <= 20.0f) {
				m->tp.h = 20.0f;
			}
			if (m->tp.h <= a2 + 20.0f) {
				if (m->tp.u <= w) {
					wall_bounce(m, w, 2.0f, 3.0f);
				}
				if (1.0f - w <= m->tp.u) {
					wall_bounce(m, 1.0f - w, -2.0f, 4.0f);
				}
			}
			if (g_track.ptype[m->tp.seg] == 3) {
				int sc = m->tp.seg;
				float ceil = g_track.ay[sc * 8 + 2] - g_track.ay[sc * 8 + 3];
				if (ceil <= m->tp.h) {
					m->hit_time = 0.0f;
					m->tp.h = ceil;
					if (m->is_ai == 0) {
						Sound_SetPan(3, 0);
						Sound_Play(3);
						sparks4(v3(0.0f, 10.0f, -5.0f));
					}
				}
			}
		}
		else if (a1 < m->tp.h && m->tp.h < a2) {
			if (m->grounded == 1) {
				if (0.5f <= m->tp.u) {
					if (m->tp.u < c0 + 1.0f) {
						edge_snap(m, c0 + w + 1.0f);
					}
				}
				else if (-c0 < m->tp.u) {
					edge_snap(m, -c0 - w);
				}
			}
			else {
				if (0.5f <= m->tp.u) {
					if (m->tp.u <= c0 + 1.0f) {
						edge_snap(m, c0 + w + 1.0f);
					}
				}
				else if (-c0 <= m->tp.u) {
					edge_snap(m, -c0 - w);
				}
			}
		}
		break;
	case 1:
		if (m->on_road == 1) {
			if (m->grounded == 1 && m->tp.h <= 20.0f) {
				m->tp.h = 20.0f;
			}
		}
		else if (a2 < m->tp.h && m->tp.h < 0.0f) {
			if (0.5f <= m->tp.u) {
				if (m->tp.u < w + 1.0f) {
					edge_snap(m, w + 1.0f);
				}
			}
			else if (-w < m->tp.u) {
				edge_snap(m, -w);
			}
		}
		break;
	case 2:
		if (m->on_road == 1) {
			if (m->grounded != 1) {
				break;
			}
			if (m->tp.h > 100.0f) {
				m->tp.h = 100.0f;
			}
			if (m->tp.h < -100.0f) {
				m->tp.h = -100.0f;
			}
			if (m->tp.h <= a1 || !(m->tp.h < a2)) {
				if (m->tp.u < w) {
					m->tp.u = w;
				}
				if (!(m->tp.u <= 1.0f - w)) {
					m->tp.u = 1.0f - w;
				}
			}
			else {
				if (m->tp.u <= w) {
					wall_bounce(m, w, 2.0f, 3.0f);
				}
				if (1.0f - w <= m->tp.u) {
					wall_bounce(m, 1.0f - w, -2.0f, 4.0f);
				}
			}
		}
		else if (m->grounded == 1 && a1 < m->tp.h && m->tp.h < a2) {
			if (0.5f <= m->tp.u) {
				if (c0 + 1.0f < m->tp.u) { /* sic: the original tests the opposite side here */
					edge_snap(m, c0 + w + 1.0f);
				}
			}
			else if (-c0 < m->tp.u) {
				edge_snap(m, -c0 - w);
			}
		}
		break;
	}

	/* --- crossing segment boundaries (and the finish line) */
	TrackPos_Update(&m->tp);
	if (!(m->tp.t < 1.0f)) {
		int so = m->tp.seg;
		float by3 = g_track.by[so * 8 + 3];
		float lat = (g_track.bx[so * 8 + 4] - g_track.bx[so * 8 + 3]) * m->tp.u + g_track.bx[so * 8 + 3];
		m->tp.seg = (int16_t)(so + 1);
		m->tp.t = m->tp.t - 1.0f;
		if (m->tp.seg > g_track.n) {
			if (m->is_ai == 0 && m->lap > 0) {
				Sound_Play(6);
			}
			m->lap++;
			m->tp.seg = 0;
		}
		int sn = m->tp.seg;
		m->tp.h = m->tp.h - g_track.ay[sn * 8 + 3] + by3;
		m->tp.u = (lat - g_track.ax[sn * 8 + 3]) / (g_track.ax[sn * 8 + 4] - g_track.ax[sn * 8 + 3]);
	}
	if (m->tp.t <= 0.0f) {
		int so = m->tp.seg;
		float ay3 = g_track.ay[so * 8 + 3];
		float lat = (g_track.ax[so * 8 + 4] - g_track.ax[so * 8 + 3]) * m->tp.u + g_track.ax[so * 8 + 3];
		m->tp.seg = (int16_t)(so - 1);
		m->tp.t = m->tp.t + 1.0f;
		if (m->tp.seg < 0) {
			m->tp.seg = g_track.n;
			m->lap--;
			if (m->lap < 0) {
				m->lap = 0;
			}
		}
		int sn = m->tp.seg;
		m->tp.h = m->tp.h - g_track.by[sn * 8 + 3] + ay3;
		m->tp.u = (lat - g_track.bx[sn * 8 + 3]) / (g_track.bx[sn * 8 + 4] - g_track.bx[sn * 8 + 3]);
	}
	TrackPos_Update(&m->tp);

	/* --- road state flags */
	s = m->tp.seg;
	switch (g_track.ptype[s]) {
	case 0:
	case 3:
	case 4:
	case 5:
		m->on_road = (-c0 < m->tp.u && m->tp.u < c0 + 1.0f) ? 1 : 0;
		m->grounded = m->tp.h < 20.0f ? 0 : 1;
		break;
	case 1:
		m->on_road = (0.0f < m->tp.u && m->tp.u < 1.0f) ? 1 : 0;
		m->grounded = m->tp.h < 20.0f ? 0 : 1;
		break;
	case 2:
		m->on_road = (-c0 < m->tp.u && m->tp.u < c0 + 1.0f) ? 1 : 0;
		m->grounded = (-120.0f < m->tp.h && m->tp.h < 120.0f) ? 1 : 0;
		break;
	case 6:
		m->on_road = 0;
		m->grounded = 0;
		break;
	}
	if (m->autopilot == 1) {
		m->on_road = 1;
		m->grounded = 1;
	}

	/* --- falling off the course */
	if (g_track.ptype[s] == 2) {
		if (m->tp.h < -150.0f) {
			if (m->tp.u < 0.0f) {
				m->alive = 0;
			}
			if (!(m->tp.u <= 1.0f)) {
				m->alive = 0;
			}
		}
	}
	else {
		int d1 = -abs(ftol(g_track.ay[s * 8 + 3] - g_track.by[s * 8 + 3]));
		int d2 = -abs(ftol(g_track.by[s * 8 + 3] - g_track.ay[(s + 1) * 8 + 3]));
		if ((float)d1 - 150.0f > m->tp.h && (float)d2 - 150.0f > m->tp.h) {
			m->alive = 0;
		}
	}

	/* --- landing */
	if (g_track.ptype[s] != 2 && m->grounded == 1) {
		if (m->tp.h <= 20.0f) {
			if (m->airborne == 1) {
				if (m->is_ai == 0) {
					Sound_Play(11);
				}
				m->air_time = 0.0f;
			}
			m->airborne = 0;
			goto counters;
		}
		if (m->airborne == 0) {
			m->air_time = 0.0f;
		}
	}
	m->airborne = 1;
counters:
	m->air_time = m->air_time + 1.0f;
	m->hit_time = m->hit_time + 1.0f;
}

/* Display pose: tilt from how the machine sits relative to the road, hover bob,
 * steering lean, wall-hit shake, and the shadow on the road below. */
void Machine_UpdatePose(Machine *m)
{
	TrackPos_Update(&m->tp);
	Mat3 rel = Mat3_MulT(Track_Frame(&g_track, m->tp), m->rot);
	float tz = DMUL(rel.m[3], 0.16);
	float tx = DMUL(DMUL(rel.m[5], 0.16), 1.65);
	m->rot = Mat3_Mul(Mat3_Rotate(tx, 0), m->rot);
	m->rot = Mat3_Mul(Mat3_Rotate(DMUL(tz, 1.65), 2), m->rot);
	Vec3 p = Track_Point(&g_track, m->tp);
	m->pos = p;
	if (m->accel.z == 0.0f) {
		m->dpos = p;
	}
	else {
		TrackPos tp = m->tp;
		float r = DMUL(RandF(), 0.03);
		/* fsin is not affected by the precision setting; the multiplies are */
		double sn = sin((double)x87_muld(g_phaseTime, 1.2));
		float bob = DMUL(x87_mul(m->throttle, sn), 0.05);
		tp.h = (r + bob) + tp.h;
		if (m->airborne == 0 && m->air_time < 10.0f) {
			float dip = (float)sin((double)DMUL(m->air_time, 0.31400000000000006));
			tp.h = tp.h - (dip + dip);
		}
		m->dpos = Track_Point(&g_track, tp);
	}
	m->drot = Mat3_Mul(Mat3_Rotate(m->steer * 8.0f, 2), m->rot);
	m->drot = Mat3_Mul(Mat3_Rotate(m->steer * 2.5f, 1), m->drot);
	Vec3 shake = v3(0, 0, 0);
	if (m->hit_time < 15.0f) {
		float k = DADD(-DMUL(m->hit_time, 0.06666666666666667), 1.0);
		shake.x = DMUL((float)(crt_rand() % 1000) * k, 0.0015);
		k = DADD(-DMUL(m->hit_time, 0.06666666666666667), 1.0);
		shake.z = DMUL((float)(crt_rand() % 1000) * k, 0.0015);
		shake = Mat3_MulVec(shake, m->rot);
		m->dpos = Vec3_Add(shake, m->dpos);
	}
	if (m->is_ai == 0) {
		g_playerFramePos = m->dpos;
		g_playerFrameRot = m->drot;
	}

	int s = m->tp.seg;
	m->shadow_vis = 0;
	if (g_track.edge[s * 8 + 3] != 0) {
		float margin = g_track.ptype[s] == 1 ? 0.05f : 0.02f;
		if (m->tp.u < DADD(-margin, 1.0) && margin < m->tp.u && m->tp.h > 10.0f) {
			m->shadow_vis = 1;
			TrackPos tp = m->tp;
			tp.h = 18.0f;
			Mat3 road;
			Track_PointFrame(tp, &m->shadow_pos, &road);
			if (m->hit_time < 15.0f) {
				m->shadow_pos = Vec3_Add(shake, m->shadow_pos);
			}
			Mat3 rr = Mat3_MulT(road, m->drot);
			float l = sqrtf(rr.m[6] * rr.m[6] + rr.m[8] * rr.m[8]);
			Mat3 yaw = Mat3_Identity();
			yaw.m[2] = rr.m[8] / l;
			yaw.m[0] = rr.m[6] / l;
			yaw.m[6] = -yaw.m[0];
			yaw.m[8] = yaw.m[2];
			m->shadow_rot = Mat3_Mul(yaw, road);
		}
	}
}

void Machine_Init(Machine *m)
{
	m->model = 7;
	m->tp.seg = 0;
	m->tp.t = 0.0f;
	m->tp.slot = 3;
	m->tp.u = 0.5f;
	m->tp.h = 20.0f;
	TrackPos_Update(&m->tp);
	m->pos = Track_Point(&g_track, m->tp);
	m->rot = Track_Frame(&g_track, m->tp);
	m->dpos = m->pos;
	m->air_time = 1000.0f;
	m->hit_time = 1000.0f;
	m->sp_timer = 1000.0f;
	m->drot = m->rot;
	m->alive = 1;
	m->on_road = 1;
	m->grounded = 1;
	m->lap = 0;
	m->power = 100.0f;
	m->airborne = 0;
	m->sp_active = 0;
	m->sp_kind = 0;
	m->time = 0.0;
	memset(m->lap_time, 0, sizeof(m->lap_time));
	m->best_lap = 999.99;
	m->rank = 7;
	m->throttle = 0.0f;
	m->accel = v3(0, 0, 0);
	m->vel = v3(0, 0, 0);
	m->steer = 0.0f;
	m->shadow_vis = 0;
	m->shadow_pos = v3(0, 0, 0);
	m->shadow_rot = Mat3_Identity();
	m->autopilot = 1;
	m->is_ai = 0;
}

/* Start of the race proper: full throttle and a kick along the first segment. */
void Machine_Launch(Machine *m)
{
	m->throttle = k_max_throttle[m->model];
	m->accel = v3(0.0f, 0.0f, 10.0f);
	m->vel = Mat3_MulVec(v3(0.0f, 0.0f, 30.0f), Track_Frame(&g_track, TrackPos_Make(0.0f, 3.0f, 0.0f)));
	memset(g_input, 0, sizeof(g_input));
	memset(g_inputPrev, 0, sizeof(g_inputPrev));
}

/* After falling off: tumble down, explode when hitting y = 0. */
void Machine_Crash(Machine *m)
{
	if (m->pos.y <= 0.0f) {
		if (m->pos.y < -1.0f) {
			Sound_Play(2);
			m->sp_timer = 0.0f;
			m->sp_kind = 3;
		}
		m->pos.y = 0.0f;
		g_playerFramePos = v3(m->pos.x, 0.0f, m->pos.z);
	}
	else {
		m->vel.x = DMUL(m->vel.x, 0.95);
		m->vel.z = DMUL(m->vel.z, 0.95);
		m->vel.y = DADD(DMUL(m->vel.y, 0.95), -1.5);
		m->shadow_vis = 0;
		m->pos.x = m->vel.x + m->pos.x;
		m->pos.y = m->vel.y + m->pos.y;
		m->pos.z = m->vel.z + m->pos.z;
		m->rot = Mat3_Mul(Mat3_Rotate(0.05f, 2), m->rot);
		m->rot = Mat3_Mul(Mat3_Rotate(0.03f, 0), m->rot);
		m->dpos = m->pos;
		m->drot = m->rot;
		g_playerFramePos = m->pos;
	}
	g_playerFrameRot = m->rot;
}

void Machine_BestLap(Machine *m)
{
	m->best_lap = m->time;
	for (int i = 0; i < g_track.laps; i++) {
		if (m->lap_time[i] < m->best_lap) {
			m->best_lap = m->lap_time[i];
		}
	}
}

/* The player bumping into the other machines (only the player collides). */
void Race_Collide(void)
{
	Machine *p = &g_machines[0];
	for (int i = 1; i < NUM_MACHINES; i++) {
		Machine *o = &g_machines[i];
		float dx = o->pos.x - p->pos.x;
		float dy = o->pos.y - p->pos.y;
		float dz = o->pos.z - p->pos.z;
		float dist = sqrtf((dz * dz + dy * dy) + dx * dx);
		if (!(dist < 40.0f)) {
			continue;
		}
		p->vel.x = DMUL(o->vel.x, 1.9) - p->vel.x;
		p->vel.y = DMUL(o->vel.y, 1.9) - p->vel.y;
		p->vel.z = DMUL(o->vel.z, 1.9) - p->vel.z;
		if (p->autopilot == 0) {
			p->power = DADD(p->power, -0.6);
		}
		float k = DADD(dist, -40.0);
		float px = dx * k / dist, py = dy * k / dist, pz = dz * k / dist;
		Track_Move(&p->tp, px, py, pz);
		p->pos = Track_Point(&g_track, p->tp);
		Sound_SetPan(3, ftol(DMUL(px, 450.0)));
		Sound_Play(3);
		Vec3 v = Mat3_MulVecT(v3(DMUL(dx, 20.0) / dist, DMUL(dy, 20.0) / dist, DMUL(dz, 20.0) / dist), p->rot);
		for (int j = 0; j < 5; j++) {
			Sparks_Add(v);
		}
	}
}

/* Number of machines ahead of the player (laps*segments + position). */
int Race_PlayerRank(void)
{
	int n = g_track.n + 1;
	float prog[NUM_MACHINES - 1];
	for (int i = 1; i < NUM_MACHINES; i++) {
		prog[i - 1] = (float)(g_machines[i].lap * n + g_machines[i].tp.seg) + g_machines[i].tp.t;
	}
	float me = (float)(g_machines[0].lap * n + g_machines[0].tp.seg) + g_machines[0].tp.t;
	int ahead = 0;
	for (int i = 0; i < NUM_MACHINES - 1; i++) {
		if (me < prog[i]) {
			ahead++;
		}
	}
	return ahead;
}
