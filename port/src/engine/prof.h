/* A small profiler for the PSP, where only the console shows the real costs:
 * named sections add up their time, and main.c writes the averages per frame
 * to rate.log with the frame rate (enabled by rate.txt / ACESPEEDER_RATE). */
#ifndef PROF_H
#define PROF_H

#include <stdint.h>

#include "../platform/platform.h"

enum { PROF_PLAYER, PROF_AI, PROF_WORLD, PROF_SCENE, PROF_CLIP, PROF_COUNT };

extern int g_prof_on;
void prof_add(int id, uint32_t us);
/* Writes "name ms ..." averaged over frames into buf and resets the sums. */
void prof_report(char *buf, int size, int frames);

#define PROF(id, stmt)                                     \
	do {                                                   \
		if (g_prof_on) {                                   \
			uint32_t prof_t0_ = plat_time_us();            \
			stmt;                                          \
			prof_add((id), plat_time_us() - prof_t0_);     \
		}                                                  \
		else {                                             \
			stmt;                                          \
		}                                                  \
	} while (0)

#endif
