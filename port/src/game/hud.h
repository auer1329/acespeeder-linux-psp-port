/* 2D overlays drawn over the race: the big centre message (original 0x453150). */
#ifndef HUD_H
#define HUD_H

#include <stdint.h>

/* Pre-transformed vertex as Direct3D's D3DTLVERTEX, in 640x480 screen space. */
typedef struct {
	float sx, sy, sz, rhw;
	uint32_t color, specular;
	float tu, tv;
} TLVertex;

/* A quad textured with one cell of the 2x4 message atlas. */
typedef struct {
	TLVertex v[4];
} HudMsg;

extern HudMsg g_hudMsg;

/* Animations of the message quad; `id` picks the atlas cell, `s` runs 0..2
 * (fly in until 1, then fade out). */
void HudMsg_Init(HudMsg *m);               /* 0x41ce60 */
void HudMsg_Fail(HudMsg *m, int id, float s);     /* 0x41d100 crashed / time over / no power */
void HudMsg_Start(HudMsg *m, int id, float s);    /* 0x41d320 */
void HudMsg_Goal(HudMsg *m, int id, float s);     /* 0x41d5a0 */
void HudMsg_GameOver(HudMsg *m, int id, float s); /* 0x41d7c0 */
void HudMsg_Wide(HudMsg *m, int id, float s);     /* 0x41d9c0 full-width cell */
void HudMsg_Menu(HudMsg *m, float s);             /* 0x41ce90 the menu title logo */
void HudMsg_Logo(HudMsg *m, float s);             /* 0x41cfc0 the demo's closing logo */

#endif
