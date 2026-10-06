/* Entry point and MainFrame (0x41be50): logic runs in fixed 33 ms ticks (at
 * most five per frame, then the clock is resynchronised), and one frame is
 * drawn after each batch of ticks.
 *
 * It starts with the title, or straight into a race: main [stage course machine mode],
 * 1-based numbers, mode 0 grand prix / 1 time attack / 2 replay (also read from
 * a file start.txt in the data folder, for the PSP).
 *
 * Debugging: ACESPEEDER_SHOTS="t1,t2,..." (or the same list in a file shots.txt
 * in the data folder, for the PSP) runs one logic tick per frame without waiting,
 * saves shot_<tick>.ppm at those ticks and quits after the last. ACESPEEDER_KEYS=
 * "t:k,t1-t2:k,..." presses key k (u d l r a b) during tick t or ticks t1..t2. ACESPEEDER_RATE (or a file
 * rate.txt) logs ticks and frames per second to stderr or rate.log. ACESPEEDER_STATE
 * starts in another game state, e.g. 700 for the staff roll. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/font.h"
#include "engine/rm.h"
#include "game/machine.h"
#include "game/race.h"
#include "game/save.h"
#include "game/ending.h"
#include "game/menu.h"
#include "game/title.h"
#include "platform/platform.h"

void Sound_LoadAll(void);

static void start_race(void)
{
	g_gameState = GS_RACE;
	g_race.state = 20;
}

static const char *g_keys; /* scripted key presses for debugging */
static int g_tick;

static void scripted_keys(void)
{
	static const char names[] = "udrlab";
	for (const char *p = g_keys; p && *p;) {
		int t = atoi(p), t2 = t;
		const char *c = strchr(p, ':'), *dash = strchr(p, '-');
		if (dash && c && dash < c) {
			t2 = atoi(dash + 1);
		}
		if (c && g_tick >= t && g_tick <= t2 && strchr(names, c[1])) {
			g_input[strchr(names, c[1]) - names] = 1;
		}
		p = strchr(p, ',');
		p = p ? p + 1 : NULL;
	}
}

static void tick(void)
{
	memcpy(g_inputPrev, g_input, sizeof(g_input));
	memset(g_input, 0, sizeof(g_input));
	plat_read_input(g_input);
	g_tick++;
	scripted_keys();
	switch (g_gameState) {
	case GS_RACE:
		Race_Update(&g_race);
		break;
	case GS_TITLE:
		Title_Update(&g_title);
		break;
	case GS_MENU:
		Menu_Update(&g_menu);
		break;
	case GS_ENDING:
		Ending_Update(&g_ending);
		break;
	case GS_DEMO:
		Race_UpdateDemo(&g_race);
		break;
	}
}

/* Without the game data every screen would stay black: say what is missing. */
static int check_data(void)
{
	static const char *const required[] = {"tt.x", "title.x", "machine1.x", "course1.bmp", "engine.wav",
	                                       "track_1_1.bin"};
	const char *missing[8];
	int n = 0;
	for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
		FILE *f = plat_open_data(required[i], "rb");
		if (f) {
			fclose(f);
		}
		else {
			missing[n++] = required[i];
		}
	}
	if (n == 0) {
		return 1;
	}
	while (!plat_should_quit()) {
		gfx_begin_frame();
		gfx_clear(0x000000, 1, 1);
		gfx_begin_2d();
		gfx_set_blend(GFX_BLEND_ALPHA);
		font_draw(320, 60, 14, 0xffff60, FONT_CENTER, "AceSpeeder!: game files not found");
		for (int i = 0; i < n; i++) {
			font_draw(320, 120 + 34 * i, 12, 0xffffff, FONT_CENTER, missing[i]);
		}
		font_draw(320, 360, 10, 0xc0c0ff, FONT_CENTER, "Copy the whole folder made by tools/make_dist.sh");
		font_draw(320, 390, 10, 0xc0c0ff, FONT_CENTER, "(dist/PSP/GAME/AceSpeeder), not only EBOOT.PBP.");
		gfx_end_2d();
		if (getenv("ACESPEEDER_SHOTS")) {
			plat_screenshot("shot_missing.ppm");
			gfx_end_frame();
			break;
		}
		gfx_end_frame();
		plat_sleep_ms(30);
	}
	plat_shutdown();
	return 0;
}

static void render(void)
{
	if (g_gameState == GS_RACE && g_race.state == 10) {
		Race_Render(&g_race);
	}
	else if (g_gameState == GS_TITLE && g_title.state == 10) {
		Title_Render(&g_title);
	}
	else if (g_gameState == GS_MENU && g_menu.state == 10) {
		Menu_Render(&g_menu);
	}
	else if (g_gameState == GS_DEMO && g_race.state == 10) {
		Race_RenderDemo(&g_race);
	}
	else if (g_gameState == GS_ENDING && g_ending.state == 10) {
		Ending_Render(&g_ending);
	}
}

int main(int argc, char **argv)
{
	if (plat_init() != 0) {
		return 1;
	}
	rm_init();
	font_init();
	if (!check_data()) {
		return 1;
	}
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
	/* MainFrame's state 100 */
	g_soundOn = g_save.opt2[1];
	g_musicOn = g_save.opt2[0];
	g_title.state = 20;
	g_menu.state = 20;
	g_ending.state = 20;
	g_race.state = 20;
	if (start[0] > 0) {
		start_race();
	}
	else {
		g_gameState = GS_TITLE;
	}
	if (getenv("ACESPEEDER_STATE")) { /* debugging: start in another game state */
		g_gameState = atoi(getenv("ACESPEEDER_STATE"));
	}

	g_keys = getenv("ACESPEEDER_KEYS");
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
			render();
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
	uint32_t rate_start = last;
	int rate_ticks = 0, rate_frames = 0;
	FILE *rate_file = plat_open_data("rate.txt", "r"); /* the PSP has no environment */
	int show_rate = getenv("ACESPEEDER_RATE") != NULL || rate_file != NULL;
	if (rate_file) {
		fclose(rate_file);
	}
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
			rate_ticks++;
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
		render();
		gfx_end_frame();
		rate_frames++;
		if (show_rate && plat_ticks_ms() - rate_start >= 5000) {
			FILE *out = plat_open_save("rate.log", "a");
			fprintf(out ? out : stderr, "%.1f ticks/s, %.1f frames/s\n", rate_ticks / 5.0, rate_frames / 5.0);
			if (out) {
				fclose(out);
			}
			rate_start = plat_ticks_ms();
			rate_ticks = rate_frames = 0;
		}
	}
	Title_Free(&g_title);
	Menu_Free(&g_menu);
	Ending_Free(&g_ending);
	if (g_race.state == 10) {
		Race_Cleanup(&g_race);
	}
	plat_shutdown();
	return 0;
}
