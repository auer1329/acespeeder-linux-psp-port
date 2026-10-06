/* Race_Update (0x411530): the per-tick logic of a race, a state machine over
 * g_racePhase with substates in r->sub. Every path not returning early ends
 * in the tail that updates the effects and advances the phase clocks. */
#include <math.h>

#include "camera.h"
#include "effects.h"
#include "hud.h"
#include "machine.h"
#include "race.h"
#include "replay.h"
#include "save.h"
#include "track.h"

Race g_race;
int32_t g_racePhase;
int32_t g_gameState = GS_INIT;

#define A_NOW (g_input[4] && !g_inputPrev[4])
#define B_NOW (g_input[5] && !g_inputPrev[5])
#define RIGHT_NOW (g_input[2] && !g_inputPrev[2])
#define LEFT_NOW (g_input[3] && !g_inputPrev[3])

static void restart_sub(Race *r)
{
	r->sub_time = 0.0;
}

static void set_phase(Race *r, int phase)
{
	g_racePhase = phase;
	g_phaseTime = 0.0;
	r->sub = 0;
	r->sub_time = 0.0;
}

static void update_machines(void)
{
	for (int i = 0; i < NUM_MACHINES; i++) {
		Machine_Update(&g_machines[i]);
		Machine_UpdatePose(&g_machines[i]);
	}
}

static void stop_engine(void)
{
	Sound_Stop(10);
	Sound_PlayLoop(0);
	Sound_Stop(8);
}

static void toggle_choice(Race *r)
{
	Sound_Play(4);
	r->choice = (int16_t)((r->choice + 1) % 2);
}

/* The machine is lost: one life less in a grand prix, retry while lives remain. */
static void leave_race(Race *r, int lost_life)
{
	Music_Stop();
	if (!lost_life) {
		Sound_Stop(0);
	}
	Race_Cleanup(r);
	if (g_save.mode == MODE_GRANDPRIX && lost_life) {
		g_save.lives--;
		g_save.power = 100.0f;
		g_gameState = g_save.lives >= 0 ? GS_RACE : GS_MENU;
	}
	else if (g_save.mode == MODE_GRANDPRIX) {
		g_save.power = g_machines[0].power;
		g_save.course_time[g_save.course] = (float)g_machines[0].time;
		g_save.course++;
		if (g_save.extra == 1) {
			if (g_save.course > 5) {
				Save_UpdateRank(&g_save);
				g_gameState = GS_ENDING;
			}
			else {
				g_gameState = GS_RACE;
			}
		}
		else {
			g_gameState = g_save.course <= 2 ? GS_RACE : GS_MENU;
		}
	}
	else if (g_save.mode == MODE_TIMEATTACK || g_save.mode == MODE_REPLAY) {
		g_gameState = GS_MENU;
	}
}

/* The failure phases share their flow: message in, wait for a key, then retry
 * or game over. Returns 1 when the race was left (skip the tail). */
static int fail_phase(Race *r, int msg, void (*cam)(Camera *))
{
	switch (r->sub) {
	case 0:
		cam(&g_camera);
		HudMsg_Fail(&g_hudMsg, msg, x87_muld(r->sub_time, 0.01));
		if (r->sub_time > 100.0) {
			r->sub = 1;
			restart_sub(r);
		}
		break;
	case 1:
		cam(&g_camera);
		HudMsg_Fail(&g_hudMsg, msg, 1.0f);
		if (A_NOW || r->sub_time > 300.0) {
			if (g_save.lives > 0) {
				r->sub = 2;
				if (msg == 3) { /* the power-out camera restarts its orbit */
					g_phaseTime = 0.0;
				}
			}
			else {
				r->sub = 3;
			}
			restart_sub(r);
		}
		break;
	case 2:
		cam(&g_camera);
		HudMsg_Fail(&g_hudMsg, msg, x87_add(x87_muld(r->sub_time, 0.02), 1.0));
		if (r->sub_time > 50.0) {
			Doors_Close(x87_add(r->sub_time, -50.0));
		}
		if (r->sub_time > 150.0) {
			leave_race(r, 1);
			return 1;
		}
		break;
	case 3:
		cam(&g_camera);
		HudMsg_Fail(&g_hudMsg, msg, x87_add(x87_muld(r->sub_time, 0.02), 1.0));
		if (r->sub_time > 50.0) {
			set_phase(r, PH_GAMEOVER);
		}
		break;
	}
	g_machines[0].sp_timer = DADD(g_machines[0].sp_timer, 1.0);
	return 0;
}

static void crash_camera(Camera *c)
{
	if (g_machines[0].pos.y > 500.0f) {
		Camera_CrashHigh(c);
	}
	else {
		Camera_CrashLow(c);
	}
}

static void explode(void)
{
	Explosion_Add(g_machines[0].pos);
	Explosion_Update();
}

/* Finish sequence substates (phase 30). */
static void finished(Race *r)
{
	double t = r->sub_time;
	switch (r->sub) {
	case 0: /* "goal" message */
		HudMsg_Goal(&g_hudMsg, 4,
		            t < 80.0 ? x87_muld(t, 0.0125) : t < 150.0 ? 1.0f : x87_add(x87_muld(x87_add(t, -150.0), 0.02), 1.0));
		if (t > 250.0) {
			r->sub = 1;
			restart_sub(r);
		}
		break;
	case 1:
		if (t > 10.0 && (A_NOW || t > 500.0)) {
			if (g_save.mode == MODE_GRANDPRIX || g_save.mode == MODE_TIMEATTACK) {
				r->sub = 2;
				restart_sub(r);
			}
			else if (g_save.mode == MODE_REPLAY) {
				r->sub = 4;
				restart_sub(r);
			}
		}
		break;
	case 2: /* save the replay? */
		if (LEFT_NOW) {
			toggle_choice(r);
		}
		if (RIGHT_NOW) {
			toggle_choice(r);
		}
		if (t > 10.0 && (A_NOW || t > 500.0)) {
			Sound_Play(7);
			if (r->choice == 0) {
				g_replay.machine = g_save.machine;
				g_replay.time = g_machines[0].time;
				Replay_Save(&g_replay, g_save.stage, g_save.course);
				Save_MarkCleared(&g_save);
			}
			if (g_save.mode == MODE_GRANDPRIX) {
				if (g_save.extra == 1) {
					r->sub = g_save.course == 5 ? 5 : 3;
				}
				else {
					r->sub = g_save.course >= 2 ? 6 : 3;
				}
				restart_sub(r);
			}
			else if (g_save.mode == MODE_TIMEATTACK) {
				r->sub = 4;
				restart_sub(r);
			}
		}
		break;
	case 3:
		if (t > 10.0 && (A_NOW || t > 500.0)) {
			r->sub = 4;
			restart_sub(r);
		}
		break;
	case 4: /* store the records and move on */
		if (g_save.mode == MODE_GRANDPRIX || g_save.mode == MODE_TIMEATTACK) {
			Save_SetRecord(&g_save, g_machines[0].time, 10);
			Save_SetRecord(&g_save, g_machines[0].best_lap, 20);
			Save_Write(&g_save);
		}
		g_save.power = g_machines[0].power;
		g_phaseTime = 0.0;
		g_racePhase = PH_NEXT;
		break;
	case 5: /* all extra courses cleared */
		HudMsg_Wide(&g_hudMsg, 6,
		            t < 300.0 ? x87_muld(t, 0.0033333333333333335)
		                      : t < 450.0 ? 1.0f : x87_add(x87_muld(x87_add(t, -450.0), 0.02), 1.0));
		if (t > 550.0) {
			r->sub = 4;
			restart_sub(r);
		}
		break;
	case 6:
		if (t > 500.0 && (A_NOW || t > 1500.0)) {
			r->sub = 4;
			restart_sub(r);
		}
		break;
	}
}

/* Returns 1 when the tail must be skipped. */
static int racing(Race *r)
{
	Machine *p = &g_machines[0];
	if (r->sub_time < 40.0) {
		HudMsg_Start(&g_hudMsg, 0, x87_add(x87_muld(r->sub_time, 0.025), 1.0));
	}
	if (B_NOW || (g_save.mode == MODE_REPLAY && r->a_now && !r->a_prev)) {
		Sound_Play(7);
		stop_engine();
		g_racePhase = PH_PAUSED;
		return 0;
	}
	if (g_save.mode == MODE_GRANDPRIX || g_save.mode == MODE_TIMEATTACK) {
		Replay_Record(&g_replay, g_input[4], g_input[2], g_input[3]);
	}
	else if (g_save.mode == MODE_REPLAY) {
		Replay_Play(&g_replay, &g_input[4], &g_input[2], &g_input[3]);
	}
	update_machines();
	Race_Collide();
	if (g_save.mode == MODE_GRANDPRIX || g_save.mode == MODE_TIMEATTACK) {
		Camera_Chase(&g_camera);
	}
	else if (g_save.mode == MODE_REPLAY) {
		Camera_Replay(&g_camera, p->tp, p->lap);
	}
	if (p->power < 10.0f) {
		Sound_PlayLoop(10);
	}
	int sec = ftol(floor(g_track.reftime - p->time));
	if (sec >= 0 && sec <= 15 && sec != r->last_sec) {
		Sound_Play(9);
	}
	r->last_sec = sec;
	if (p->alive == 0) {
		set_phase(r, PH_CRASHED);
		stop_engine();
	}
	if (g_track.reftime - p->time < -2.0) {
		set_phase(r, PH_TIMEOVER);
		stop_engine();
	}
	if (p->power < 0.0f) {
		set_phase(r, PH_NOPOWER);
		p->sp_kind = 3;
		p->sp_timer = 0.0f;
		Sound_Stop(10);
		Sound_Play(2);
		Sound_PlayLoop(0);
		Sound_Stop(8);
	}
	if (p->pos.y < 0.0f) {
		set_phase(r, PH_FELL);
		p->sp_kind = 3;
		p->sp_timer = 0.0f;
		Sound_Stop(10);
		Sound_Play(2);
		Sound_PlayLoop(0);
		Sound_Stop(8);
	}
	if (p->lap > g_track.laps || (g_save.mode == MODE_REPLAY && Replay_AtEnd(&g_replay) == 1)) {
		Machine_BestLap(p);
		p->autopilot = 1;
		p->model = 0;
		set_phase(r, PH_FINISHED);
		r->choice = 0;
		p->rank = (int16_t)(Race_PlayerRank() + 1);
		stop_engine();
		return 0;
	}
	/* the x87 runs in single precision, so these double sums round to float */
	p->time = x87_add(p->time, 0.033);
	if (p->lap != 0) {
		p->lap_time[p->lap - 1] = x87_add(p->lap_time[p->lap - 1], 0.033);
	}
	return 0;
}

void Race_SetupStart(Race *r)
{
	r->state = 10;
	g_racePhase = PH_SETUP;
	g_phaseTime = 0.0;
	r->sub_time = 0.0;
	r->sub = 0;
	r->choice = 0;
	if (g_save.mode == MODE_GRANDPRIX || g_save.mode == MODE_TIMEATTACK) {
		Replay_Clear(&g_replay);
	}
	else if (g_save.mode == MODE_REPLAY) {
		Replay_Load(&g_replay, g_save.stage, g_save.course);
		g_save.machine = g_replay.machine;
	}
}

/* Place the opponents on a grid between just behind the player's start and a
 * point ahead that depends on how tight the time limit is. */
void Race_SetupGrid(void)
{
	Track *t = &g_track;
	float q = x87_add(1.0, -(double)x87_div(t->reftime, t->f15d4));
	float front = x87_add(DMUL(x87_mul(x87_mul(q, (double)(t->n + 1)), (double)t->laps), 0.75), t->f15d0);
	float back = DADD(t->f15d0, 0.5);
	t->reftime = x87_muld(t->reftime, 1.5);
	Machine *p = &g_machines[0];
	Machine_Init(p);
	p->is_ai = 0;
	p->tp.seg = (int16_t)ftol(floor((double)t->f15d0));
	p->tp.t = (float)((double)t->f15d0 - floor((double)t->f15d0));
	TrackPos_Update(&p->tp);
	p->pos = Track_Point(t, p->tp);
	p->rot = Track_Frame(t, p->tp);
	p->power = g_save.power;
	for (int k = 0; k < NUM_MACHINES - 1; k++) {
		Machine *m = &g_machines[k + 1];
		Machine_Init(m);
		m->is_ai = 1;
		m->lap = 0;
		float a = x87_muld((double)k, 0.16666666666666666);
		float s = x87_add(x87_mul(x87_add(1.0, -(double)a), back), x87_mul(a, front));
		m->tp.seg = (int16_t)ftol(floor((double)s));
		m->tp.t = (float)((double)s - floor((double)s));
		while (m->tp.seg > t->n) {
			m->tp.seg = (int16_t)(m->tp.seg - t->n - 1);
			m->lap++;
		}
		/* step out of type 6 segments; the original wraps with the lap count
		 * where it means the segment count */
		while (t->ptype[m->tp.seg] == 6) {
			m->tp.t = DADD(m->tp.t, 0.2);
			if (m->tp.t > 1.0f) {
				m->tp.seg++;
				m->tp.t = DADD(m->tp.t, -1.0);
				if (m->tp.seg > t->n) {
					m->lap++;
					m->tp.seg = (int16_t)(m->tp.seg + (-1 - t->laps));
				}
			}
		}
		float d = (float)m->tp.seg + m->tp.t - ((float)p->tp.seg + p->tp.t);
		if (d < 0.05f && d > -0.05f) {
			m->tp.t = DADD(m->tp.t, -0.1);
		}
		TrackPos_Update(&m->tp);
		m->pos = Track_Point(t, m->tp);
		m->rot = Track_Frame(t, m->tp);
	}
	Camera_Init(&g_camera);
}

void Race_Update(Race *r)
{
	if (r->state == 20) {
		Race_Setup(r);
	}
	r->a_prev = r->a_now;
	r->a_now = (int16_t)g_input[4];
	Machine *p = &g_machines[0];
	switch (g_racePhase) {
	case PH_SETUP:
		Music_Play(g_track.s1a22 + 1);
		Sound_PlayLoop(0);
		g_phaseTime = 0.0;
		g_racePhase = PH_ROLLING;
		r->sub_time = 0.0;
		r->sub = 0;
		Camera_ReplayReset(&g_camera);
		/* fall through */
	case PH_ROLLING:
		if (r->sub_time > 100.0) {
			HudMsg_Start(&g_hudMsg, 0, r->sub_time < 300.0 ? x87_muld(x87_add(r->sub_time, -100.0), 0.005) : 1.0f);
		}
		/* the machines drive themselves over the line */
		Input_Zero3(&g_input[4], &g_input[2], &g_input[3]);
		Input_Zero3(&g_inputPrev[4], &g_inputPrev[2], &g_inputPrev[3]);
		update_machines();
		Camera_Chase(&g_camera);
		if (g_track.s1a24 == 0) {
			Camera_Intro(&g_camera, 0);
		}
		else if (g_track.s1a24 == 1) {
			Camera_Intro(&g_camera, 1);
		}
		Doors_Open((float)g_phaseTime);
		if (p->lap == 1) {
			Sound_SetFreq(8, 63000);
			Sound_SetPan(8, 0);
			Sound_PlayLoop(8);
			Sound_Stop(0);
			Machine_Launch(p);
			p->autopilot = 0;
			p->model = g_save.machine;
			Camera_StartRace(&g_camera);
			Camera_ReplayShot(&g_camera, p->tp, p->lap, 0, 99);
			g_phaseTime = 0.0;
			g_racePhase = PH_RACING;
			r->sub = 0;
			restart_sub(r);
		}
		break;
	case PH_RACING:
		racing(r);
		break;
	case PH_FINISHED:
		update_machines();
		Race_Collide();
		Camera_Finish(&g_camera);
		finished(r);
		break;
	case PH_CRASHED:
		Machine_Crash(p);
		if (p->pos.y < 10.0f) {
			explode();
		}
		if (fail_phase(r, 1, r->sub <= 1 ? crash_camera : Camera_CrashLow)) {
			return;
		}
		break;
	case PH_TIMEOVER:
		if (fail_phase(r, 2, Camera_TimeOver)) {
			return;
		}
		break;
	case PH_NOPOWER:
		explode();
		if (fail_phase(r, 3, Camera_NoPower)) {
			return;
		}
		break;
	case PH_FELL:
		Machine_Crash(p);
		if (p->pos.y < 10.0f) {
			explode();
		}
		if (fail_phase(r, 1, Camera_CrashLow)) {
			return;
		}
		break;
	case PH_NEXT:
		update_machines();
		Camera_Finish(&g_camera);
		Doors_Close((float)g_phaseTime);
		if (g_phaseTime > 100.0) {
			leave_race(r, 0);
			return;
		}
		p->sp_timer = DADD(p->sp_timer, 1.0);
		break;
	case PH_PAUSED:
		if ((r->a_now && !r->a_prev) || B_NOW) {
			if (r->choice == 0) {
				Sound_Play(7);
				Sound_PlayLoop(8);
				Sound_Stop(0);
				g_racePhase = PH_RACING;
			}
			else if (r->choice == 1) {
				Sound_Play(7);
				g_racePhase = PH_QUIT;
				g_phaseTime = 0.0;
			}
		}
		r->a_prev = (int16_t)g_input[4];
		if (RIGHT_NOW) {
			toggle_choice(r);
		}
		if (LEFT_NOW) {
			toggle_choice(r);
		}
		break;
	case PH_QUIT:
		Camera_NoPower(&g_camera);
		if (g_phaseTime > 100.0) {
			Doors_Close(x87_add(g_phaseTime, -100.0));
		}
		if (g_phaseTime > 200.0) {
			Music_Stop();
			Sound_Stop(0);
			Race_Cleanup(r);
			g_gameState = GS_MENU;
			return;
		}
		break;
	case PH_GAMEOVER:
		switch (r->sub) {
		case 0:
			Camera_GameOver(&g_camera);
			HudMsg_GameOver(&g_hudMsg, 5, x87_muld(r->sub_time, 0.01));
			if (r->sub_time > 100.0) {
				r->sub = 1;
				restart_sub(r);
			}
			break;
		case 1:
			Camera_GameOver(&g_camera);
			HudMsg_GameOver(&g_hudMsg, 5, 1.0f);
			/* the original tests for the button held, not newly pressed */
			if ((g_input[4] && g_inputPrev[4]) || r->sub_time > 300.0) {
				r->sub = 2;
				restart_sub(r);
			}
			break;
		case 2:
			Camera_GameOver(&g_camera);
			HudMsg_GameOver(&g_hudMsg, 5, x87_add(x87_muld(r->sub_time, 0.02), 1.0));
			if (r->sub_time > 50.0) {
				Doors_Close(x87_add(r->sub_time, -50.0));
			}
			if (r->sub_time > 150.0) {
				Music_Stop();
				Race_Cleanup(r);
				g_gameState = GS_MENU;
				return;
			}
			break;
		}
		break;
	}
	Sparks_Update();
	Scenery_Update();
	if (g_save.stage >= 0 && g_save.stage <= 5 && g_save.course >= 0 && g_save.course <= 5) {
		Gates_Update();
	}
	g_phaseTime = x87_add(g_phaseTime, 1.65);
	r->sub_time = x87_add(r->sub_time, 1.65);
}
