/* Title sequence (original object at 0x452da0), game state 900. */
#ifndef TITLE_H
#define TITLE_H

#include <stdint.h>

#include "gmath.h"

typedef struct {
	int16_t state;  /* 20 = needs setup, 10 = running */
	Mat3 rot;       /* 0x452da8 camera orientation */
	Vec3 cam;       /* 0x452dd0 camera position */
} Title;

extern Title g_title;

void Title_Update(Title *t);  /* 0x419f80 */
void Title_Render(Title *t);  /* 0x41bbf0 */
void Title_Free(Title *t);    /* 0x4199a0 */

#endif
