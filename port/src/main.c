/* Entry point and MainFrame (0x41be50): logic runs in fixed 33 ms ticks (at
 * most five per frame, then the clock is resynchronised), and one frame is
 * drawn after each batch of ticks.
 *
 * For now the game starts straight into a race: main [stage course machine mode],
 * 1-based numbers, mode 0 grand prix / 1 time attack / 2 replay (also read from
 * a file start.txt in the data folder, for the PSP).
 *
 * Debugging: ACESPEEDER_SHOTS="t1,t2,..." (or the same list in a file shots.txt
 * in the data folder, for the PSP) runs one logic tick per frame without waiting,
 * saves shot_<tick>.ppm at those ticks and quits after the last. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/font.h"
#include "engine/rm.h"
#include "game/machine.h"
#include "game/race.h"
#include "game/save.h"
#include "platform/platform.h"

void Sound_LoadAll(void);

static void start_race(void)
{
	g_gameState = GS_RACE;
	g_race.state = 20;
}

static void tick(void)
{
	memcpy(g_inputPrev, g_input, sizeof(g_input));
	memset(g_input, 0, sizeof(g_input));
	plat_read_input(g_input);
	switch (g_gameState) {
	case GS_RACE:
		Race_Update(&g_race);
		break;
	case GS_MENU:
	case GS_ENDING:
		/* the menus are not ported yet: race the same course again */
		start_race();
		break;
	}
}

int main(int argc, char **argv)
{
	if (plat_init() != 0) {
		return 1;
	}
	rm_init();
	font_init();
	Sound_LoadAll();
	Save_Defaults(&g_save);
	Save_Load(&g_save);
	int start[4] = {0, 0, 0, -1};
	FILE *f = plat_open_data("start.txt", "r");
	if (f) {
		if (fscanf(f, "%d %d %d %d", &start[0], &start[1], &start[2], &start[3]) < 1) {
			start[0] = 0;
		}
		fclose(f);
	}
	for (int i = 1; i < argc && i <= 4; i++) {
		start[i - 1] = atoi(argv[i]);
	}
	if (start[0] > 0) {
		g_save.stage = (int16_t)(start[0] - 1);
	}
	if (start[1] > 0) {
		g_save.course = (int16_t)(start[1] - 1);
	}
	if (start[2] > 0) {
		g_save.machine = (int16_t)(start[2] - 1);
	}
	if (start[3] >= 0) {
		g_save.mode = (int16_t)start[3];
	}
	start_race();

	char list[256] = "";
	const char *shots = getenv("ACESPEEDER_SHOTS");
	FILE *sf = shots ? NULL : plat_open_data("shots.txt", "r");
	if (sf) {
		shots = fgets(list, sizeof(list), sf) ? list : NULL;
		fclose(sf);
	}
	if (shots) {
		FILE *log = plat_open_save("ticks.log", "w");
		for (int t = 1; *shots && !plat_should_quit(); t++) {
			tick();
			if (log && t % 50 == 0) {
				fprintf(log, "%d: state %d race %d phase %d time %.3f sub %d %.3f lap %d s %.3f\n", t,
				        (int)g_gameState, g_race.state, (int)g_racePhase, g_phaseTime, g_race.sub, g_race.sub_time,
				        g_machines[0].lap, g_machines[0].tp.seg + g_machines[0].tp.t);
			}
			gfx_begin_frame();
			if (g_gameState == GS_RACE && g_race.state == 10) {
				Race_Render(&g_race);
			}
			if (atoi(shots) == t) {
				char path[64];
#ifdef __PSP__
				snprintf(path, sizeof(path), "save/shot_%d.ppm", t);
#else
				snprintf(path, sizeof(path), "shot_%d.ppm", t);
#endif
				plat_screenshot(path);
				shots = strchr(shots, ',') ? strchr(shots, ',') + 1 : "";
			}
			gfx_end_frame();
		}
		if (log) {
			fclose(log);
		}
		g_gameState = GS_QUIT;
	}
	uint32_t last = plat_ticks_ms();
	while (!plat_should_quit() && g_gameState != GS_QUIT) {
		uint32_t now = plat_ticks_ms();
		if (now < last) {
			last = now;
		}
		uint32_t elapsed = now - last;
		if (elapsed <= 32) {
			plat_sleep_ms(1);
			continue;
		}
		for (int n = 1;; n++) {
			tick();
			last += 33;
			if (n > 4) {
				last = plat_ticks_ms();
				break;
			}
			elapsed -= 33;
			if (elapsed < 33) {
				break;
			}
		}
		gfx_begin_frame();
		if (g_gameState == GS_RACE && g_race.state == 10) {
			Race_Render(&g_race);
		}
		gfx_end_frame();
	}
	if (g_race.state == 10) {
		Race_Cleanup(&g_race);
	}
	plat_shutdown();
	return 0;
}
