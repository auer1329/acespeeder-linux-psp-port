/* Staff roll (original object at 0x44f40c), game state 700. */
#ifndef ENDING_H
#define ENDING_H

#include <stdint.h>

typedef struct {
	int16_t state;    /* 20 = needs setup, 10 = running */
	int16_t variant;  /* 0 Japanese credits, 1 English, picked at random */
} Ending;

extern Ending g_ending;

void Ending_Update(Ending *e);  /* 0x40f310 */
void Ending_Render(Ending *e);  /* 0x40f570 */
void Ending_Free(Ending *e);     /* 0x410610 */

#endif
