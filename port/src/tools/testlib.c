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

#include "../game/camera.h"
void test_camera_intro0(Camera *c) { Camera_Intro(c, 0); }
void test_camera_intro1(Camera *c) { Camera_Intro(c, 1); }
void test_camera_replay(Camera *c, const TrackPos *p, int lap) { Camera_Replay(c, *p, lap); }
void test_camera_shot(Camera *c, const TrackPos *p, int lap, int dir, int shot) { Camera_ReplayShot(c, *p, lap, dir, shot); }

#include "../game/effects.h"
void test_explosion_add(const Vec3 *p) { Explosion_Add(*p); }

/* scene and music hooks of the race logic, logged for the differential tests */
#include "../game/race.h"
int test_hook_log[256][2];
float test_hook_arg[256];
int test_hook_count;
static void log_hook(int op, int a, float f)
{
	if (test_hook_count < 256) {
		test_hook_log[test_hook_count][0] = op;
		test_hook_log[test_hook_count][1] = a;
		test_hook_arg[test_hook_count] = f;
		test_hook_count++;
	}
}
void Race_Setup(Race *r) { (void)r; log_hook(1, 0, 0); }
void Race_Cleanup(Race *r) { (void)r; log_hook(2, 0, 0); }
void Doors_Open(float t) { log_hook(3, 0, t); }
void Doors_Close(float t) { log_hook(4, 0, t); }
void Music_Play(int id) { log_hook(5, id, 0); }
void Music_Stop(void) { log_hook(6, 0, 0); }
FILE *plat_open_save(const char *name, const char *mode) { (void)name; (void)mode; return NULL; }
