/* The staff roll after the last grand prix stage, game state 700: Ending_Update
 * (0x40f310), Ending_Render (0x40f570), set up by 0x40ef10 and freed by
 * 0x410610. A wireframe model turns in the background while the credits fade
 * in and out, one card per stretch of the timeline; the card text is English
 * or Japanese at random. */
#include <stdlib.h>

#include "../engine/font.h"
#include "../engine/rm.h"
#include "../engine/sprite.h"
#include "../platform/platform.h"
#include "doors.h"
#include "ending.h"
#include "machine.h"
#include "race.h"
#include "save.h"
#include "title.h"

Ending g_ending;

/* 0x4474a8: the card in use is g_racePhase, which advances when g_phaseTime
 * passes its end; entry -1 (0) is the start of the first card. */
static const float k_card_end[13] = {0, 300, 600, 700, 1000, 1300, 1600, 1900, 2000, 2300, 2400, 2700, 10000};

static struct {
	RMFrame *scene, *camera, *sun, *model;
	RMViewport *view;
	Sprite null_blk;
	Mat3 cam_rot;
} E;

static void set_pose(RMFrame *f, Vec3 p, Mat3 r)
{
	rm_frame_set_position(f, E.scene, p.x, p.y, p.z);
	rm_frame_set_orientation(f, E.scene, r.m[6], r.m[7], r.m[8], r.m[3], r.m[4], r.m[5]);
}

/* 0x40ef10 */
static void ending_setup(Ending *e)
{
	e->state = 10;
	g_racePhase = 0;
	g_phaseTime = 0.0;
	e->variant = (int16_t)(crt_rand() % 2);
	E.scene = rm_frame_create(NULL);
	E.camera = rm_frame_create(E.scene);
	E.cam_rot = Mat3_LookDir(v3(0.0f, 400.0f, -1000.0f), v3(0, 0, 0));
	set_pose(E.camera, v3(0.0f, 300.0f, -1000.0f), E.cam_rot);
	E.view = rm_viewport_create(E.camera, 0, 0, gfx_screen_w(), gfx_screen_h());
	RMLight *amb = rm_light_create_rgb(RM_LIGHT_AMBIENT, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(E.scene, amb);
	rm_release(amb);
	E.sun = rm_frame_create(E.scene);
	set_pose(E.sun, v3(100.0f, 100.0f, 100.0f), Mat3_LookDir(v3(100.0f, 200.0f, 500.0f), v3(0, 0, 0)));
	RMLight *sun = rm_light_create_rgb(RM_LIGHT_DIRECTIONAL, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(E.sun, sun);
	rm_release(sun);
	/* Obj_LoadX_Quality40: drawn as wireframe */
	E.model = rm_frame_create(E.scene);
	set_pose(E.model, v3(1200.0f, 0.0f, 0.0f), Mat3_Identity());
	RMMeshBuilder *mb = rm_mb_create();
	rm_mb_load(mb, "endroll.x");
	rm_mb_scale(mb, 1600.0f, 1600.0f, 1600.0f);
	rm_mb_set_quality(mb, RM_FILL_WIREFRAME);
	rm_mb_set_perspective(mb, 1);
	rm_frame_add_visual(E.model, mb);
	rm_release(mb);
	Doors_Init(E.camera, 1.0f);
	rm_frame_set_scene_background_rgb(E.scene, 0, 0, 0);
	rm_viewport_set_back(E.view, 5000.0f);
	rm_viewport_set_field(E.view, 1.0f);
	sprite_load(&E.null_blk, "null_blk.bmp");
}

/* 0x410610 */
static void ending_free(Ending *e)
{
	e->state = 20;
	sprite_free(&E.null_blk);
	Doors_Free();
	RM_RELEASE(E.model);
	RM_RELEASE(E.sun);
	RM_RELEASE(E.camera);
	RM_RELEASE(E.view);
	RM_RELEASE(E.scene);
}

void Ending_Update(Ending *e)
{
	if (e->state == 20) {
		ending_setup(e);
	}
	if (k_card_end[g_racePhase] < g_phaseTime) {
		g_racePhase++;
	}
	if (g_racePhase == 0) {
		Music_Play(7);
	}
	if (g_phaseTime > 2100.0 && g_phaseTime < 2102.0) {
		Sound_Play(6);
	}
	Mat3 r = Mat3_Rotate((float)(g_phaseTime * -0.02), 1);
	rm_frame_set_orientation(E.model, E.scene, r.m[6], r.m[7], r.m[8], r.m[3], r.m[4], r.m[5]);
	if (g_phaseTime < 100.0) {
		Doors_Open((float)g_phaseTime);
	}
	if (g_phaseTime > 2600.0) {
		Doors_Close((float)(g_phaseTime - 2600.0));
	}
	if (g_phaseTime > 2700.0) {
		Music_Stop();
		ending_free(e);
		g_title.state = 20;
		g_gameState = GS_TITLE;
	}
	g_phaseTime += 1.65;
}

static void line(const char *s, int y, int size, float r, float g, float b)
{
	uint32_t rgb = (uint32_t)(int)(r * 255.0f) << 16 | (uint32_t)(int)(g * 255.0f) << 8 | (uint32_t)(int)(b * 255.0f);
	gfx_begin_2d();
	gfx_set_blend(GFX_BLEND_ALPHA);
	font_draw(320, y, size, rgb, FONT_CENTER, s);
	gfx_end_2d();
}

/* name above, person below */
static void card(int japanese, const char *en_role, const char *en_name, const char *ja_role, const char *ja_name,
                 float a)
{
	if (japanese) {
		line(ja_role, 195, 24, a, a, a);
		line(ja_name, 245, 24, a, a, a);
	}
	else {
		line(en_role, 200, 22, a, a, a);
		line(en_name, 240, 26, a, a, a);
	}
}

void Ending_Render(Ending *e)
{
	static const char *const ranks[6] = {"Rank A", "Rank B", "Rank C", "Rank D", "Rank E", "Rank F"};
	rm_viewport_clear(E.view);
	gfx_begin_2d();
	sprite_stretch(&E.null_blk, 0, 0, 160, 120, 0, 0, 640, 480);
	gfx_end_2d();
	rm_viewport_render(E.view, E.model);

	/* each card fades in over its first 30%, holds, and fades out by 90% */
	int c = g_racePhase;
	float s = c >= 1 && c < 13 ? (float)((g_phaseTime - k_card_end[c - 1]) / (k_card_end[c] - k_card_end[c - 1])) : 0.0f;
	float a = 0.0f;
	if (s >= 0.1f) {
		if (s < 0.3f) {
			a = (float)((s - 0.1) * 5.0);
		}
		else if (s < 0.7f) {
			a = 1.0f;
		}
		else if (s < 0.9f) {
			a = (float)(1.0 - (s - 0.7) * 5.0);
		}
	}
	int ja = e->variant == 0;
	if (a != 0.0f) {
		switch (c - 1) {
		case 0:
			if (ja) {
				line("中谷崇宏作品", 220, 24, a, a, a);
			}
			else {
				line("A", 185, 22, a, a, a);
				line("Takahiro Nakatani", 220, 22, a, a, a);
				line("Work", 255, 22, a, a, a);
			}
			break;
		case 1:
			line("AceSpeeder!", 220, 34, a, a * 0.5f, 0.0f);
			break;
		case 3:
			card(ja, "Directed by", "Takahiro Nakatani", "監 督", "中 谷 崇 宏", a);
			break;
		case 4:
			card(ja, "Graphic by", "Wataru Kawakami", "美 術", "川 上 亘", a);
			break;
		case 5:
			card(ja, "Sounds by", "Kazumasa Kohara", "音 響", "小 原 一 将", a);
			break;
		case 6:
			card(ja, "Composed by", "T.Phuta", "作 曲", "T.Phuta", a);
			break;
		case 8: {
			float b = a * 0.7f;
			line("Your Play is ...", 200, 22, b, b, a);
			int r = g_save.rank;
			line(r >= 0 && r <= 5 ? ranks[r] : "Rank -", 240, 32, b, b, a);
			break;
		}
		case 10:
			line("Production TN", 220, 26, a, a, 0.0f);
			break;
		}
	}
	if (g_phaseTime < 100.0 || g_phaseTime > 2600.0) {
		Doors_Render(E.view, E.scene, E.sun, E.cam_rot);
	}
}

void Ending_Free(Ending *e)
{
	if (E.scene) {
		ending_free(e);
	}
}
