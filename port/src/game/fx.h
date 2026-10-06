/* The effects bank (original 0x45a6b0): pre-lit, additively blended visuals
 * drawn through a user visual, selected by a mode before each render. */
#ifndef FX_H
#define FX_H

#include "../engine/rm.h"

enum {
	FX_BACKFIRE = 0, /* engine flame, param = throttle share */
	FX_SPEED = 1,    /* pickup auras, param 0..1 over their duration */
	FX_JUMPUP = 2,
	FX_SPECIAL = 3,
	FX_BOARDS = 4,   /* pickup boards on the track (world space) */
	FX_RING = 5,     /* shock ring of a destroyed machine */
	FX_SPARKS = 6,
	FX_SMOKE = 7,
	FX_INFO = 8,     /* centre message quad, info.bmp */
	FX_ACE = 9,      /* centre message quad, ace.bmp */
	FX_SCENERY = 10,
};

void Fx_Init(void);                 /* 0x4246b0 */
void Fx_Free(void);
RMUserVisual *Fx_CreateVisual(void); /* 0x4245f0 */
void Fx_SetMode(int mode);           /* 0x424670 */
void Fx_SetModeParam(int mode, double param); /* 0x424680 */
void Fx_PreparePickups(void);        /* 0x419670 quads of the pickup boards */
void Fx_PrepareSparks(Mat3 cam);     /* 0x410f00 */
void Fx_PrepareSmoke(Mat3 cam, Vec3 campos); /* 0x41e7d0 */
int Fx_SparkCount(void);
int Fx_SmokeCount(void);

#endif
