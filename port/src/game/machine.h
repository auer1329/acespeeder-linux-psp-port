/* Racing machines (original array at 0x44b670, 7 x 0x178 bytes; [0] = player)
 * and the per-machine physics. Layout matches the original. */
#ifndef MACHINE_H
#define MACHINE_H

#include <stdint.h>

#include "gmath.h"

typedef struct {
	int16_t model;        /* 0x000 machine type 0..7 */
	int16_t pad0;
	TrackPos tp;          /* 0x004 position on the track */
	Vec3 pos;             /* 0x028 world position */
	Mat3 rot;             /* 0x034 world orientation */
	float u58[12];        /* 0x058 */
	float throttle;       /* 0x088 */
	Vec3 accel;           /* 0x08c */
	Vec3 vel;             /* 0x098 */
	float steer;          /* 0x0a4 */
	int16_t alive;        /* 0x0a8 */
	int16_t on_road;      /* 0x0aa inside the road edges */
	int16_t grounded;     /* 0x0ac near the road surface */
	int16_t lap;          /* 0x0ae */
	float power;          /* 0x0b0 energy, lost on wall hits */
	int16_t airborne;     /* 0x0b4 */
	int16_t pad1;
	float air_time;       /* 0x0b8 */
	int16_t path_idx;     /* 0x0bc pickup index while riding a path */
	int16_t pad2;
	TrackPos path_tp;     /* 0x0c0 position when the path started */
	float hit_time;       /* 0x0e4 ticks since last wall hit */
	int16_t sp_active;    /* 0x0e8 special move running */
	int16_t sp_kind;      /* 0x0ea 0 boost, 1 jump, 2 path */
	float sp_timer;       /* 0x0ec */
	double time;          /* 0x0f0 race time */
	uint8_t uf8[0x7c];    /* 0x0f8 */
	int16_t autopilot;    /* 0x174 follows the racing line (AI / demo) */
	int16_t is_ai;        /* 0x176 */
} Machine;

/* Boost/jump pads and path boards (original 0x451f78). The data files store
 * the first 0x600 bytes; to[15] would extend past that but no course has 16. */
typedef struct {
	int16_t count;
	int16_t pad;
	float seg[16];        /* 0x004 */
	float trange[16][2];  /* 0x044 t window (high, low) */
	float lrange[16][2];  /* 0x0c4 lateral window */
	int16_t type[16];     /* 0x144 0 boost, 1 jump, 2 path */
	float dur[16];        /* 0x164 path duration (ticks) */
	TrackPos from[16];    /* 0x1a4 */
	TrackPos to[16];      /* 0x3e4 */
} Pickups;

/* Wall-hit sparks (original 0x44f490), only the part the physics writes. */
typedef struct {
	int16_t next;         /* 0x000 ring index */
	int16_t active[50];   /* 0x002 */
	float life[50];       /* 0x068 */
	Vec3 pos[50];         /* 0x130 */
	Vec3 vel[50];         /* 0x388 */
	uint8_t rest[0x1a00]; /* 0x5e0 render data */
} Sparks;

extern Machine g_machines[7];
extern Pickups g_pickups;
extern Sparks g_sparks;
extern int32_t g_input[14];
extern int32_t g_inputPrev[14];

void Machine_Update(Machine *m);                                     /* 0x404aa0 */
void Track_Move(TrackPos *p, float dx, float dy, float dz);          /* 0x4082d0 */
void Track_PointFrame(TrackPos p, Vec3 *pos, Mat3 *rot);             /* 0x407f50 */
TrackPos Pickups_PathPos(float timer, int idx, TrackPos start);      /* 0x4193e0 */
Vec3 Pickups_PathPoint(float t2, int idx, TrackPos start);           /* 0x419550 */
int Sparks_Add(Vec3 v);                                              /* 0x410e50 */

/* sound effects (Sound_* in the original, ids in re/NOTES.md) */
void Sound_Play(int id);
void Sound_PlayLoop(int id);
void Sound_Stop(int id);
void Sound_SetPan(int id, int pan);
void Sound_SetFreq(int id, int hz);

#endif
