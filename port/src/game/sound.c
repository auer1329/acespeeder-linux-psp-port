/* Sound effect wrappers (Sound_* at 0x41ebe0.. in the original): the game refers to
 * the 12 WAVs by index, see re/NOTES.md. */
#include "../platform/platform.h"
#include "machine.h"

static const char *const k_wavs[12] = {
	"null.wav", "jump.wav", "explos.wav", "crash.wav", "select.wav", "cansel.wav",
	"raptim.wav", "decide.wav", "engine.wav", "warn_tim.wav", "warn_pow.wav", "set.wav",
};
static int g_handles[12];
static int g_loaded;

void Sound_LoadAll(void)
{
	for (int i = 0; i < 12; i++) {
		g_handles[i] = snd_load_wav(k_wavs[i]);
	}
	g_loaded = 1;
}

void Sound_Play(int id) { if (g_loaded) snd_play(g_handles[id], 0); }
void Sound_PlayLoop(int id) { if (g_loaded) snd_play(g_handles[id], 1); }
void Sound_Stop(int id) { if (g_loaded) snd_stop(g_handles[id]); }
void Sound_SetPan(int id, int pan) { if (g_loaded) snd_set_pan(g_handles[id], pan); }
void Sound_SetFreq(int id, int hz) { if (g_loaded) snd_set_freq(g_handles[id], hz); }
