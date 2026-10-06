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
	uint8_t ud8[0xac];    /* 0x0d8 used by the other camera modes */
	float sway_phase;     /* 0x184 */
	uint8_t u188[0x18];   /* 0x188 */
} Camera;

extern Camera g_camera;
/* Pose of the camera frame, set directly on the Direct3D frame by the original. */
extern Vec3 g_cameraFramePos;
extern Mat3 g_cameraFrameRot;

void Camera_Init(Camera *c);     /* 0x401180 */
void Camera_Chase(Camera *c);    /* 0x4011f0 */

#endif
