/* Settings/progress (0x418730..0x418d60). gamedata.dat is a text file of
 * printf("%lf\n") / printf("%d\n") values, written and read in a fixed order. */
#include "save.h"

#include <stddef.h>
#include <string.h>

#include "../platform/platform.h"
#include "gmath.h"
#include "replay.h"

_Static_assert(offsetof(SaveData, best_time) == 0x30, "SaveData layout");
_Static_assert(offsetof(SaveData, stage_rank) == 0x270, "SaveData layout");
_Static_assert(offsetof(SaveData, has_replay) == 0x27c, "SaveData layout");
_Static_assert(offsetof(SaveData, opt) == 0x2c4, "SaveData layout");
_Static_assert(offsetof(SaveData, code) == 0x2d4, "SaveData layout");
_Static_assert(offsetof(SaveData, extra) == 0x2e0, "SaveData layout");

SaveData g_save;

static const int32_t k_unlock[3] = {0x0, 0x0, 0x0};

void Save_Defaults(SaveData *s)
{
	s->mode = s->machine = s->stage = s->course = 0;
	s->lives = 2;
	s->power = 100.0f;
	for (int i = 0; i < 6; i++) {
		s->course_time[i] = 999.99f;
	}
	s->rank = 6;
	for (int i = 0; i < 6; i++) {
		for (int j = 0; j < 6; j++) {
			s->best_time[i][j] = 999.99;
			s->best_lap[i][j] = 999.99;
		}
		s->stage_rank[i] = 5;
	}
	s->opt[0] = s->opt[1] = s->opt[2] = s->opt[3] = 0;
	s->extra = 0;
	s->opt2[0] = s->opt2[1] = s->opt2[2] = 1;
	s->code[0] = 0x45dfd0f;
	s->code[1] = 0x28b44f8;
	s->code[2] = 0x12a36cc;
}

void Save_Reset(SaveData *s)
{
	s->mode = s->machine = s->stage = s->course = 0;
	s->lives = 2;
	s->power = 100.0f;
	for (int i = 0; i < 6; i++) {
		s->course_time[i] = 999.99f;
	}
	for (int i = 0; i < 6; i++) {
		for (int j = 0; j < 6; j++) {
			s->has_replay[i][j] = (int16_t)Replay_Exists(i, j);
		}
	}
}

/* The original fscanf("%d") into 16-bit fields: the 32-bit store spills into
 * the following field. Reproduce it on our identical layout. */
static void read_int_into(FILE *f, SaveData *s, size_t off)
{
	int v = 0;
	if (fscanf(f, "%d", &v) == 1) {
		memcpy((char *)s + off, &v, 4);
	}
}

void Save_Load(SaveData *s)
{
	FILE *f = plat_open_save("gamedata.dat", "r");
	if (!f) {
		Save_Defaults(s);
		Save_Write(s);
		return;
	}
	for (int i = 0; i < 6; i++) {
		for (int j = 0; j < 6; j++) {
			if (fscanf(f, "%lf", &s->best_time[i][j]) != 1 || fscanf(f, "%lf", &s->best_lap[i][j]) != 1) {
				break;
			}
		}
	}
	for (int i = 0; i < 6; i++) {
		read_int_into(f, s, offsetof(SaveData, stage_rank) + 2 * i);
	}
	static const size_t opts[] = {0x2c6, 0x2c4, 0x2ca, 0x2cc, 0x2ce, 0x2d0, 0x2d4, 0x2d8, 0x2dc};
	for (size_t i = 0; i < sizeof(opts) / sizeof(opts[0]); i++) {
		read_int_into(f, s, opts[i]);
	}
	s->extra = (s->code[0] == k_unlock[0] && s->code[1] == k_unlock[1] && s->code[2] == k_unlock[2]) ? 1 : 0;
	fclose(f);
}

void Save_Write(const SaveData *s)
{
	FILE *f = plat_open_save("gamedata.dat", "w");
	if (!f) {
		return;
	}
	for (int i = 0; i < 6; i++) {
		for (int j = 0; j < 6; j++) {
			fprintf(f, "%lf\n", s->best_time[i][j]);
			fprintf(f, "%lf\n", s->best_lap[i][j]);
		}
	}
	for (int i = 0; i < 6; i++) {
		fprintf(f, "%d\n", s->stage_rank[i]);
	}
	fprintf(f, "%d\n%d\n%d\n", s->opt[1], s->opt[0], s->opt[3]);
	fprintf(f, "%d\n%d\n%d\n", s->opt2[0], s->opt2[1], s->opt2[2]);
	fprintf(f, "%d\n%d\n%d\n", (int)s->code[0], (int)s->code[1], (int)s->code[2]);
	fclose(f);
}

void Save_CheckCode(SaveData *s, const int16_t c[8])
{
	if (c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 0 && c[4] == 0 && c[5] == 0 && c[6] == 0 && c[7] == 0) {
		s->extra = 1;
		memcpy(s->code, k_unlock, sizeof(k_unlock));
	}
}

int Save_IsRecord(const SaveData *s, double value, int kind)
{
	if (kind == 10) {
		return value < s->best_time[s->stage][s->course];
	}
	if (kind == 20) {
		return value < s->best_lap[s->stage][s->course];
	}
	if (kind == 30) {
		return (double)s->stage_rank[s->stage] < value;
	}
	return 2;
}

void Save_SetRecord(SaveData *s, double value, int kind)
{
	if (kind == 10) {
		if (value < s->best_time[s->stage][s->course]) {
			s->best_time[s->stage][s->course] = value;
		}
	}
	else if (kind == 20) {
		if (value < s->best_lap[s->stage][s->course]) {
			s->best_lap[s->stage][s->course] = value;
		}
	}
	else if (kind == 30) {
		if ((double)s->stage_rank[s->stage] < value) {
			s->stage_rank[s->stage] = (int16_t)ftol(value);
		}
	}
}

void Save_MarkCleared(SaveData *s)
{
	s->has_replay[s->stage][s->course] = 1;
}

/* Stage rank from the summed course times against par, adjusted by lives and
 * power left: 0 = S .. 5 = E. */
void Save_UpdateRank(SaveData *s)
{
	static const float par[6] = {464.0f, 483.0f, 492.0f, 537.0f, 552.0f, 564.0f};
	float sum = 0.0f;
	for (int i = 0; i < 6; i++) {
		sum = sum + s->course_time[i];
	}
	for (int16_t r = 0; r < 6; r++) {
		if (sum / par[s->stage] < x87_mul((float)r, 0.08333333333333333) + 1.0f) {
			s->rank = r;
			break;
		}
	}
	double left = (double)s->lives * 100.0 + (double)s->power;
	if (left > 400.0) {
		s->rank--;
	}
	if (left < 200.0) {
		s->rank++;
	}
	if (s->rank < 0) {
		s->rank = 0;
	}
	if (s->rank > 5) {
		s->rank = 5;
	}
	if (s->rank < s->stage_rank[s->stage]) {
		s->stage_rank[s->stage] = s->rank;
	}
	Save_Write(s);
}
