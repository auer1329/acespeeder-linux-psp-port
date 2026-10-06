/* The two-part shutter that closes over the view between screens (original
 * object at 0x44eeb8), shared by the title and the race. */
#ifndef DOORS_H
#define DOORS_H

#include "../engine/rm.h"
#include "gmath.h"

void Doors_Init(RMFrame *camera);  /* 0x40d920: frames attached to the camera */
void Doors_Free(void);
void Doors_Open(float t);          /* 0x40dc90 */
void Doors_Close(float t);         /* 0x40dd60 */
/* Renders the shutter lit from a fixed direction relative to the view
 * (0x40dad0 turns the sun, 0x40dc50 turns it back). */
void Doors_Render(RMViewport *view, RMFrame *scene, RMFrame *sun, Mat3 cam_rot);

#endif
