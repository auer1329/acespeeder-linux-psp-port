/* Particle effects and animated course decorations updated by the race logic. */
#ifndef EFFECTS_H
#define EFFECTS_H

#include <stdint.h>

#include "gmath.h"

/* Explosion smoke when the player's machine is destroyed (original 0x458718). */
typedef struct {
	int16_t next;         /* 0x000 ring index */
	int16_t active[50];   /* 0x002 */
	int16_t life[50];     /* 0x066 ticks since spawned */
	Vec3 pos[50];         /* 0x0cc */
	uint8_t rest[0x100];  /* 0x324 render data */
} Explosion;

/* Rotating course decorations (original 0x44f048): only the update part. */
typedef struct {
	uint32_t frame[16];   /* 0x000 Direct3D frames in the original; unused here */
	int16_t count;        /* 0x040 */
	int16_t pad0;
	Vec3 pos[16];         /* 0x044 */
	Mat3 rot[16];         /* 0x104 */
	float angle[16];      /* 0x344 */
	float speed[16];      /* 0x384 */
} Gates;

/* Distant scenery objects (original 0x458008). */
typedef struct {
	int16_t count;        /* 0x000 */
	int16_t pad0;
	float color[3];       /* 0x004 */
	Vec3 pos[32];         /* 0x010 */
	uint8_t u190[0x480];  /* 0x190 render data */
	float yaw[32];        /* 0x610 */
	float spin[32];       /* 0x690 */
} Scenery;

extern Explosion g_explosion;
extern Scenery g_scenery;
extern Gates g_gates;

void Sparks_Update(void);                         /* 0x410db0 */
void Explosion_Add(Vec3 pos);                     /* 0x41e740 (its velocity argument is unused) */
void Explosion_Update(void);                      /* 0x41e690 */
void Scenery_Init(void);                          /* 0x41e0e0 */
void Scenery_Add(float s, float side, float yaw);  /* 0x41e100 */
void Scenery_Populate(int n);                     /* 0x41e280 */
void Scenery_Update(void);                        /* 0x41e340 */
void Gates_Update(void);                          /* 0x40e4c0 via 0x43d9f0 */

#endif
