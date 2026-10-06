/* Replays (original object at 0x4531d0): one 3-bit input code per logic tick
 * (accelerate*4 + right*2 + left), saved run-length encoded as count*10+code. */
#ifndef REPLAY_H
#define REPLAY_H

#include <stdint.h>

#define REPLAY_MAX 10000

typedef struct {
	int32_t pos;                 /* 0x0000 */
	int32_t count;               /* 0x0004 */
	int16_t data[REPLAY_MAX];    /* 0x0008 */
	int16_t machine;             /* 0x4e28 */
	int16_t pad[3];
	double time;                 /* 0x4e30 */
} Replay;

extern Replay g_replay;

void Replay_Clear(Replay *r);                                   /* 0x41dc50 */
void Replay_Record(Replay *r, int a, int right, int left);      /* 0x41dc80 */
void Replay_Play(Replay *r, int32_t *a, int32_t *right, int32_t *left); /* 0x41dcb0 */
void Input_Zero3(int32_t *a, int32_t *b, int32_t *c);           /* 0x41dd50 */
int Replay_AtEnd(const Replay *r);                              /* 0x41dd80 */
void Replay_Load(Replay *r, int stage, int course);             /* 0x41dda0 (0-based) */
void Replay_Save(Replay *r, int stage, int course);             /* 0x41de60 */
int Replay_Exists(int stage, int course);                       /* 0x41e050 */

#endif
