/* Portable software mixer shared by both backends: WAV effects with
 * DirectSound-like frequency/pan control, plus one streamed OGG music track.
 * Backends call mixer_render() from their audio thread and lock around it
 * with mixer_lock()/mixer_unlock() implemented per platform. */
#ifndef MIXER_H
#define MIXER_H

#include <stdint.h>

#define MIXER_RATE 44100

void mixer_init(void);
void mixer_render(int16_t *out, int frames); /* interleaved stereo */

/* implemented by the backend */
void mixer_lock(void);
void mixer_unlock(void);

#endif
