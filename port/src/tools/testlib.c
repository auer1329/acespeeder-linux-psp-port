/* Build of the game logic as a shared library for re/scripts/difftest_*.py:
 * no platform layer, sound calls are recorded instead of played. */
#include <stdint.h>
#include <stdio.h>

#include "../game/machine.h"

int32_t test_sound_log[64][3];
int test_sound_count;

static void log_sound(int op, int id, int v)
{
	if (test_sound_count < 64) {
		test_sound_log[test_sound_count][0] = op;
		test_sound_log[test_sound_count][1] = id;
		test_sound_log[test_sound_count][2] = v;
		test_sound_count++;
	}
}

void Sound_Play(int id) { log_sound(0, id, 0); }
void Sound_PlayLoop(int id) { log_sound(1, id, 0); }
void Sound_Stop(int id) { log_sound(2, id, 0); }
void Sound_SetPan(int id, int pan) { log_sound(3, id, pan); }
void Sound_SetFreq(int id, int hz) { log_sound(4, id, hz); }

/* only the file loaders reference these */
FILE *plat_open_data(const char *name, const char *mode) { (void)name; (void)mode; return NULL; }

/* pointer-based wrappers: ctypes is unreliable with by-value structs */
#include "../game/track.h"
void test_track_point(const TrackPos *p, Vec3 *out) { *out = Track_Point(&g_track, *p); }
void test_track_frame(const TrackPos *p, Mat3 *out) { *out = Track_Frame(&g_track, *p); }
