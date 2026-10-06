/* Race flow: Race_Update (0x411530) is the logic of game state 500. */
#ifndef RACE_H
#define RACE_H

#include <stdint.h>

#include "doors.h"
#include "gmath.h"

/* The race object (original 0x4513e0). */
typedef struct {
	int16_t state;        /* 0x00: 20 = needs setup, 10 = running (see MainFrame) */
	int16_t pad0;
	Vec3 title_pos;       /* 0x04 where the course title board stands */
	uint8_t u10[0x24];
	int16_t sub;          /* 0x34 substate within the race phase */
	int16_t pad1;
	double sub_time;      /* 0x38 grows 1.65 per tick */
	int16_t choice;       /* 0x40 yes/no selection in the pause and result menus */
	uint8_t u42[6];
	int16_t a_now;        /* 0x48 accelerate button this tick */
	int16_t a_prev;       /* 0x4a ... and the previous tick */
	int32_t last_sec;     /* 0x4c last countdown second beeped */
	uint8_t u50[8];
} Race;

/* Race phases (g_racePhase, original 0x451f68). */
enum {
	PH_SETUP = 0, PH_ROLLING = 10, PH_RACING = 20, PH_FINISHED = 30, PH_CRASHED = 40, PH_TIMEOVER = 42,
	PH_NOPOWER = 44, PH_FELL = 46, PH_NEXT = 90, PH_PAUSED = 100, PH_QUIT = 110, PH_GAMEOVER = 200,
};

/* Game states (MainFrame, original 0x44a228). */
enum { GS_INIT = 100, GS_MENU = 200, GS_RACE = 500, GS_ENDING = 700, GS_QUIT = 800, GS_TITLE = 900 };

extern Race g_race;
extern int32_t g_racePhase;
extern int32_t g_gameState;

void Race_Update(Race *r);   /* 0x411530 */
void Race_Setup(Race *r);    /* 0x417070 builds the race scene */
/* The game-state part of Race_Setup, before and after the course is built. */
void Race_SetupStart(Race *r);
void Race_SetupGrid(void);
void Race_Cleanup(Race *r);  /* 0x414fe0 */
void Race_Render(Race *r);   /* 0x412f40 draws the race and its HUD */


void Music_Play(int id);     /* 0x41c510 */
void Music_Stop(void);       /* 0x41c540 */

#endif
