#include "replay.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "../platform/platform.h"
#include "gmath.h"

_Static_assert(offsetof(Replay, machine) == 0x4e28, "Replay layout");
_Static_assert(offsetof(Replay, time) == 0x4e30, "Replay layout");

Replay g_replay;

void Replay_Clear(Replay *r)
{
	for (int i = 0; i < REPLAY_MAX; i++) {
		r->data[i] = 9;
	}
	r->count = 0;
	r->pos = 0;
}

void Replay_Record(Replay *r, int a, int right, int left)
{
	if (r->pos < REPLAY_MAX) { /* the original has no bound check */
		r->data[r->pos] = (int16_t)(left + (right + a * 2) * 2);
	}
	r->pos++;
}

void Replay_Play(Replay *r, int32_t *a, int32_t *right, int32_t *left)
{
	int v = r->pos < REPLAY_MAX ? r->data[r->pos] : 9;
	*a = ftol(floor(v * 0.25));
	*right = ftol(floor((double)(v % 4) * 0.5));
	*left = ftol(floor((double)(v % 2)));
	r->pos++;
}

void Input_Zero3(int32_t *a, int32_t *b, int32_t *c)
{
	*a = 0;
	*b = 0;
	*c = 0;
}

int Replay_AtEnd(const Replay *r)
{
	return r->count < r->pos;
}

static void replay_name(char *buf, size_t n, int stage, int course)
{
	snprintf(buf, n, "replay_%d_%d.dat", stage + 1, course + 1);
}

/* Run-length encode data[0..count) in place (0x41df30). */
static void compress(Replay *r)
{
	if (r->count < 2) {
		if (r->count == 1) {
			r->data[0] = (int16_t)(r->data[0] + 10);
		}
		return;
	}
	int16_t v = r->data[0];
	int16_t run = 1;
	int out = 0;
	for (int i = 1; i < r->count; i++) {
		if (r->data[i] == v) {
			run++;
		}
		else {
			r->data[out++] = (int16_t)(v + run * 10);
			run = 1;
			v = r->data[i];
		}
	}
	r->data[out] = (int16_t)(v + run * 10);
	r->count = out + 1;
}

/* Inverse of compress (0x41df90). */
static void expand(Replay *r)
{
	static int16_t runs[REPLAY_MAX];
	int n = r->count < REPLAY_MAX ? r->count : REPLAY_MAX;
	memcpy(runs, r->data, sizeof(int16_t) * n);
	int out = 0;
	for (int i = 0; i < n; i++) {
		int len = ftol(floor(runs[i] * 0.1));
		int16_t code = (int16_t)(runs[i] % 10);
		for (int k = 0; k < len && out < REPLAY_MAX; k++) {
			r->data[out++] = code;
		}
	}
	r->count = out;
}

void Replay_Load(Replay *r, int stage, int course)
{
	char name[48];
	Replay_Clear(r);
	replay_name(name, sizeof(name), stage, course);
	FILE *f = plat_open_save(name, "r");
	if (!f) {
		return;
	}
	int machine = 0, count = 0;
	if (fscanf(f, "%d", &machine) != 1 || fscanf(f, "%lf", &r->time) != 1 || fscanf(f, "%d", &count) != 1) {
		fclose(f);
		return;
	}
	r->machine = (int16_t)machine;
	r->count = count < 0 ? 0 : count > REPLAY_MAX ? REPLAY_MAX : count;
	for (int i = 0; i < r->count; i++) {
		int v = 0;
		if (fscanf(f, "%d", &v) != 1) {
			r->count = i;
			break;
		}
		r->data[i] = (int16_t)v;
	}
	fclose(f);
	expand(r);
}

void Replay_Save(Replay *r, int stage, int course)
{
	char name[48];
	r->count = r->pos < REPLAY_MAX ? r->pos : REPLAY_MAX;
	compress(r);
	replay_name(name, sizeof(name), stage, course);
	FILE *f = plat_open_save(name, "w");
	if (!f) {
		return;
	}
	fprintf(f, "%d\n", r->machine);
	fprintf(f, "%lf\n", r->time);
	fprintf(f, "%d\n", r->count);
	for (int i = 0; i < r->count; i++) {
		fprintf(f, "%d\n", r->data[i]);
	}
	fclose(f);
}

int Replay_Exists(int stage, int course)
{
	char name[48];
	replay_name(name, sizeof(name), stage, course);
	FILE *f = plat_open_save(name, "r");
	if (!f) {
		return 0;
	}
	fclose(f);
	return 1;
}
