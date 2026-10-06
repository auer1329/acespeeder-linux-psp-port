/* Race camera (original global at 0x44b4d0, 0x1a0 bytes). */
#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>

#include "gmath.h"

typedef struct {
	Vec3 pos;             /* 0x000 */
	Mat3 rot;             /* 0x00c final orientation (sent to the camera frame) */
	int16_t mode;         /* 0x030 chase view 0..3 (F2, F3, F4, F1) */
	int16_t pad0;
	float dist;           /* 0x034 current distance behind the machine */
	float target_dist;    /* 0x038 */
	Mat3 base;            /* 0x03c pitched machine orientation */
	Mat3 lag;             /* 0x060 lagging copy of base: the offset direction */
	Mat3 look;            /* 0x084 rows: right, up, direction to the target */
	Mat3 smooth;          /* 0x0a8 lagging copy of look */
	Vec3 target;          /* 0x0cc point looked at */
	/* replay camera: a random shot over a window of track progress */
	int16_t shot;         /* 0x0d8 shot type 0..6 */
	int16_t pad1;
	float shot_start;     /* 0x0dc progress (laps*segments + position) */
	float shot_end;       /* 0x0e0 */
	TrackPos shot_a;      /* 0x0e4 */
	TrackPos shot_b;      /* 0x108 */
	TrackPos shot_c;      /* 0x12c */
	Vec3 shot_p[3];       /* 0x150 */
	float orbit_a0;       /* 0x174 orbit shot: start angle */
	float orbit_da;       /* 0x178 angle swept */
	float orbit_r0;       /* 0x17c radius at start and end */
	float orbit_r1;       /* 0x180 */
	float sway_phase;     /* 0x184 */
	float finish_angle;   /* 0x188 */
	float finish_wobble;  /* 0x18c */
	float crash_wobble;   /* 0x190 */
	float timeover_wobble;/* 0x194 */
	float nopower_wobble; /* 0x198 */
	float replay_wobble;  /* 0x19c */
} Camera;

extern Camera g_camera;
/* Pose of the camera frame, set directly on the Direct3D frame by the original. */
extern Vec3 g_cameraFramePos;
extern Mat3 g_cameraFrameRot;

void Camera_Init(Camera *c);     /* 0x401180 */
void Camera_Chase(Camera *c);    /* 0x4011f0 */
void Camera_Intro(Camera *c, int variant); /* 0x401aa0 (variant 1) / 0x401ef0 (0): rolling start fly-in */
void Camera_StartRace(Camera *c);/* 0x402340: hand over to the chase camera */
void Camera_Finish(Camera *c);   /* 0x4024c0: orbit after the goal */
void Camera_CrashHigh(Camera *c);/* 0x402700 */
void Camera_CrashLow(Camera *c); /* 0x4027c0 */
void Camera_TimeOver(Camera *c); /* 0x4029d0 */
void Camera_NoPower(Camera *c);  /* 0x402ca0 */
void Camera_GameOver(Camera *c); /* 0x402f80 */
void Camera_ReplayReset(Camera *c);                                         /* 0x4032d0 */
void Camera_ReplayShot(Camera *c, TrackPos p, int lap, int dir, int shot); /* 0x4032f0 */
void Camera_Replay(Camera *c, TrackPos p, int lap);                       /* 0x403ed0 */

#endif
