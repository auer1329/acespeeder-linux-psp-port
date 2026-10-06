/* The race scene: the 3D part of Race_Setup (0x417070), Race_Render (0x412f40)
 * with its HUD, the course gates and buildings,
 * and Race_Cleanup (0x414fe0). The game logic itself is in race.c. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../engine/font.h"
#include "../engine/rm.h"
#include "../engine/sprite.h"
#include "../platform/platform.h"
#include "camera.h"
#include "course_objects.h"
#include "doors.h"
#include "effects.h"
#include "fx.h"
#include "hud.h"
#include "machine.h"
#include "race.h"
#include "replay.h"
#include "save.h"
#include "track.h"

#define FOG_COLOR 0xff0c0019u
#define FOG_START 6000.0f
#define FOG_END 8000.0f

/* The HUD bitmaps loaded by 0x418fd0. */
typedef struct {
	Sprite power;    /* 0x451c48 power gauge, a window into a 420x32 strip */
	Sprite number2;  /* 0x451dd8 big digits, 32x32 */
	Sprite number3;  /* 0x451ea0 small digits, 20x20 */
	Sprite kigo;     /* 0x451928 "/" signs, 20x20 */
	Sprite left;     /* 0x4519f0 "left" (lives) label */
	Sprite inf;      /* 0x451ab8 time / rank / lap labels, 60x20 each */
	Sprite null_blu; /* 0x451d10 plain background for the low-detail options */
} HudSprites;

static struct {
	RMFrame *scene;     /* 0x451720 */
	RMFrame *camera;    /* 0x451724 */
	RMFrame *sun;       /* 0x451728 */
	RMViewport *view;
	TrackMeshes meshes;
	RMFrame *seg[TRACK_MAX_POINTS]; /* 0x45172c */
	RMFrame *ground;    /* 0x4517a4 */
	RMFrame *sky;       /* 0x4517a8 */
	RMFrame *title;     /* 0x4517ac */
	RMFrame *titbar;    /* 0x4517b0 */
	RMFrame *player;    /* 0x4517b4 */
	RMFrame *shadow;    /* 0x4517b8 */
	RMFrame *ai[NUM_MACHINES - 1]; /* 0x4517bc */
	RMFrame *fx;        /* 0x451858 effects bank moved to each effect, fogged */
	RMFrame *fx_world;  /* 0x45185c effects bank in world space */
	RMFrame *gate[16];  /* 0x44f048 */
	int nbuild;         /* 0x44eec0 */
	Vec3 build_pos[24];
	RMFrame *build[24];
	HudSprites hud;
} S;

static const char *const k_music[8] = {
	"BlackJackMayol.ogg", "PhutaMusicA.ogg", "DeadendCorner.ogg", "PhutaMusicB.ogg",
	"MagicalParty.ogg", "PhutaMusicC.ogg", "AquaGunShoot.ogg", "DeepBlueMayol.ogg",
};

/* ---- scene building helpers ---- */

static void set_pose(RMFrame *f, Vec3 p, Mat3 r)
{
	rm_frame_set_position(f, S.scene, p.x, p.y, p.z);
	rm_frame_set_orientation(f, S.scene, r.m[6], r.m[7], r.m[8], r.m[3], r.m[4], r.m[5]);
}

static void set_fog(RMFrame *f)
{
	rm_frame_set_scene_fog_enable(f, 1);
	rm_frame_set_scene_fog_color(f, FOG_COLOR);
	rm_frame_set_scene_fog_params(f, FOG_START, FOG_END, 1.0f);
}

/* Obj_LoadX_Quality80 (0x40b6d0) and Obj_LoadX_WithMaterial (0x40b7b0). */
static RMFrame *load_object(const char *name, Vec3 pos, Mat3 rot, Vec3 scale, int with_material)
{
	RMFrame *f = rm_frame_create(S.scene);
	set_pose(f, pos, rot);
	RMMeshBuilder *mb = rm_mb_create();
	rm_mb_load(mb, name);
	rm_mb_scale(mb, scale.x, scale.y, scale.z);
	rm_mb_set_quality(mb, with_material ? 0x89 : 0x80);
	rm_mb_set_perspective(mb, 1);
	if (with_material) {
		RMMaterial *m = rm_material_create(100.0f);
		rm_material_set_power(m, 7.5f);
		rm_material_set_specular(m, 1.8f, 1.8f, 1.8f);
		rm_material_set_emissive(m, 0.25f, 0.25f, 0.25f);
		rm_mb_set_material(mb, m);
		rm_release(m);
	}
	rm_frame_add_visual(f, mb);
	rm_release(mb);
	return f;
}

static RMFrame *create_fx_frame(void)
{
	RMFrame *f = rm_frame_create(S.scene);
	set_pose(f, v3(0, 0, 0), Mat3_Identity());
	RMUserVisual *uv = Fx_CreateVisual();
	rm_frame_add_visual(f, uv);
	rm_release(uv);
	return f;
}

/* Gates_Add (0x40e2a0): a gate over the track, spinning when speed != 0. */
static void gate_add(const CourseGate *g)
{
	if (g_gates.count > 15) {
		return;
	}
	char name[32];
	TrackPos p = TrackPos_Make(g->s, 3.5f, 0.0f);
	Vec3 pos = Track_Point(&g_track, p);
	Mat3 rot = g->mode == 1 ? Track_Frame(&g_track, p) : Track_FlatFrame(&g_track, p);
	int i = g_gates.count;
	g_gates.pos[i] = pos;
	g_gates.rot[i] = rot;
	snprintf(name, sizeof(name), "gate%d.x", g->kind + 1);
	S.gate[i] = load_object(name, pos, rot, v3(g->scale[0], g->scale[1], g->scale[2]), 0);
	set_fog(S.gate[i]);
	g_gates.angle[i] = 0.0f;
	g_gates.speed[i] = g->speed;
	g_gates.count++;
}

/* Buildings_Add (0x40de80): a building on the ground beside the track. */
static void building_add(const CourseBuilding *b)
{
	if (S.nbuild >= 24) {
		return;
	}
	char name[32];
	TrackPos p = TrackPos_Make(b->s, 3.5f, 0.0f);
	Vec3 at = Track_Point(&g_track, p);
	Mat3 flat = Track_FlatFrame(&g_track, p);
	Vec3 pos = Vec3_Add(v3(at.x, 0.0f, at.z), Mat3_MulVec(v3(b->side, 0.0f, 0.0f), flat));
	Mat3 rot = Mat3_Mul(Mat3_Rotate(b->yaw * 6.28f, 1), flat);
	S.build_pos[S.nbuild] = pos;
	snprintf(name, sizeof(name), "build%d.x", b->kind + 1);
	S.build[S.nbuild] = load_object(name, pos, rot, v3(b->scale[0], b->scale[1], b->scale[2]), 0);
	set_fog(S.build[S.nbuild]);
	S.nbuild++;
}

int g_musicOn = 1;

void Music_Play(int id)
{
	if (!g_musicOn) {
		return;
	}
	mus_stop();
	if (id >= 0 && id < 8) {
		mus_play(k_music[id], 1);
	}
}

void Music_Stop(void)
{
	if (!g_musicOn) {
		return;
	}
	mus_stop();
}

static void hud_load(HudSprites *h)
{
	sprite_load(&h->power, "power.bmp");
	sprite_load(&h->number2, "number2.bmp");
	sprite_load(&h->number3, "number3.bmp");
	sprite_load(&h->kigo, "kigo.bmp");
	sprite_load(&h->left, "left.bmp");
	sprite_load(&h->inf, "inf.bmp");
	sprite_load(&h->null_blu, "null_blu.bmp");
}

static void hud_free(HudSprites *h)
{
	sprite_free(&h->power);
	sprite_free(&h->number2);
	sprite_free(&h->number3);
	sprite_free(&h->kigo);
	sprite_free(&h->left);
	sprite_free(&h->inf);
	sprite_free(&h->null_blu);
}

void Race_Setup(Race *r)
{
	char name[32];
	Race_SetupStart(r);
	/* Course_BuildTrack, Track_MakeBands, Track_BuildMeshes and Track_ComputeRadius
	 * of the original, precomputed into the course data files */
	Track_Load(g_save.stage + 1, g_save.course + 1, &S.meshes);
	Race_SetupGrid();

	rm_set_render_mode(1);
	S.scene = rm_frame_create(NULL);
	S.camera = rm_frame_create(S.scene);
	set_pose(S.camera, v3(0, 0, 0), Mat3_Identity());
	S.view = rm_viewport_create(S.camera, 0, 0, gfx_screen_w(), gfx_screen_h());

	RMLight *amb = rm_light_create_rgb(RM_LIGHT_AMBIENT, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(S.scene, amb);
	rm_release(amb);
	S.sun = rm_frame_create(S.scene);
	TrackPos p0 = TrackPos_Make(0.0f, 3.5f, 0.0f);
	set_pose(S.sun, Track_Point(&g_track, p0),
	         Mat3_Mul(Mat3_LookDir(v3(100.0f, 100.0f, 0.0f), v3(0, 0, 0)), Track_Frame(&g_track, p0)));
	RMLight *sun = rm_light_create_rgb(RM_LIGHT_DIRECTIONAL, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(S.sun, sun);
	rm_release(sun);

	for (int i = 0; i < S.meshes.nseg; i++) {
		S.seg[i] = rm_frame_create(S.scene);
		rm_frame_set_position(S.seg[i], S.scene, S.meshes.seg[i].pos.x, S.meshes.seg[i].pos.y,
		                      S.meshes.seg[i].pos.z);
		rm_frame_add_visual(S.seg[i], S.meshes.seg[i].mb);
	}

	snprintf(name, sizeof(name), "machine%d.x", g_save.machine + 1);
	S.player = load_object(name, v3(0, 0, 0), Mat3_Identity(), v3(18.0f, 18.0f, 18.0f), 1);
	S.shadow = rm_frame_create(S.scene);
	set_pose(S.shadow, v3(0, 0, 0), Mat3_Identity());
	RMMeshBuilder *mb = rm_mb_create();
	snprintf(name, sizeof(name), "shadow%d.x", g_save.machine + 1);
	rm_mb_load(mb, name);
	rm_mb_scale(mb, 20.0f, 20.0f, 20.0f);
	rm_mb_set_perspective(mb, 0);
	rm_mb_set_quality(mb, 0x80);
	rm_mb_set_color(mb, 0xff000000u);
	rm_frame_add_visual(S.shadow, mb);
	rm_release(mb);
	for (int i = 0; i < NUM_MACHINES - 1; i++) {
		/* the opponents' looks are random, independent of their handling */
		snprintf(name, sizeof(name), "machine%d.x", crt_rand() % 8 + 1);
		S.ai[i] = load_object(name, v3(0, 0, 0), Mat3_Identity(), v3(18.0f, 18.0f, 18.0f), 1);
	}

	Fx_Init();
	S.fx = create_fx_frame();
	S.fx_world = create_fx_frame();
	Fx_PreparePickups();
	HudMsg_Init(&g_hudMsg);
	memset(&g_explosion, 0, sizeof(g_explosion));
	memset(&g_sparks, 0, sizeof(g_sparks));
	Scenery_Init();
	Scenery_Populate(g_track.n);

	snprintf(name, sizeof(name), "ground%d.x", g_track.env + 1);
	S.ground = load_object(name, v3(0, 0, 0), Mat3_Identity(), v3(32000.0f, 32000.0f, 32000.0f), 0);
	snprintf(name, sizeof(name), "sky%d.x", g_track.env + 1);
	S.sky = load_object(name, v3(0, 0, 0), Mat3_Identity(), v3(16200.0f, 10125.0f, 16200.0f), 0);

	/* the course title board over the start line and the bar behind it */
	Vec3 pos;
	Mat3 rot;
	Track_PointFrame(TrackPos_Make(0.0f, 3.5f, 180.0f), &pos, &rot);
	r->title_pos = pos;
	memcpy(r->u10, &rot, sizeof(rot));
	S.title = rm_frame_create(S.scene);
	set_pose(S.title, pos, rot);
	mb = rm_mb_create();
	rm_mb_load(mb, "title.x");
	rm_mb_scale(mb, 100.0f, 100.0f, 100.0f);
	rm_mb_set_quality(mb, 0x89);
	rm_mb_set_perspective(mb, 1);
	RMMaterial *mat = rm_material_create(100.0f);
	rm_material_set_power(mat, 2.0f);
	rm_material_set_specular(mat, 1.2f, 1.2f, 1.2f);
	rm_material_set_emissive(mat, 0.5f, 0.5f, 0.5f);
	rm_mb_set_material(mb, mat);
	rm_release(mat);
	rm_frame_add_visual(S.title, mb);
	rm_release(mb);
	Track_PointFrame(TrackPos_Make(0.0f, 3.5f, -90.0f), &pos, &rot);
	S.titbar = load_object("titbar.x", pos, rot, v3(500.0f, 500.0f, 500.0f), 1);

	Doors_Init(S.camera);

	/* course objects; the detail option drops the scenery and the objects */
	int opt = g_save.opt[3];
	g_gates.count = 0;
	S.nbuild = 0;
	if (opt != 2 && opt != 4 && opt != 6 && opt != 7) {
		const CourseObjects *co = &k_course_objects[g_save.stage][g_save.course];
		for (int i = 0; i < co->ngates; i++) {
			gate_add(&co->gates[i]);
		}
		for (int i = 0; i < co->nbuildings; i++) {
			building_add(&co->buildings[i]);
		}
	}
	if (opt == 1 || opt == 4 || opt == 5 || opt == 7) {
		g_scenery.count = 0;
	}

	for (int i = 0; i <= g_track.n && i < S.meshes.nseg; i++) {
		set_fog(S.seg[i]);
	}
	set_fog(S.fx);
	/* SetFront(0) is rejected by Direct3D RM, so the near plane stays at 1 */
	rm_viewport_set_back(S.view, 20000.0f);
	rm_viewport_set_field(S.view, 1.0f);
	hud_load(&S.hud);
}

void Race_Cleanup(Race *r)
{
	r->state = 20;
	Fx_Free();
	hud_free(&S.hud);
	Doors_Free();
	RMFrame **frames[] = {&S.title, &S.titbar, &S.player, &S.shadow, &S.ground,
	                      &S.sky, &S.fx, &S.fx_world, &S.sun, &S.camera};
	for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {
		RM_RELEASE(*frames[i]);
	}
	for (int i = 0; i < NUM_MACHINES - 1; i++) {
		RM_RELEASE(S.ai[i]);
	}
	for (int i = 0; i < TRACK_MAX_POINTS; i++) {
		RM_RELEASE(S.seg[i]);
	}
	for (int i = 0; i < 16; i++) {
		RM_RELEASE(S.gate[i]);
	}
	for (int i = 0; i < 24; i++) {
		RM_RELEASE(S.build[i]);
	}
	g_gates.count = 0;
	S.nbuild = 0;
	Track_FreeMeshes(&S.meshes);
	RM_RELEASE(S.view);
	RM_RELEASE(S.scene);
}

/* ---- rendering ---- */

static float dist2(Vec3 a, Vec3 b)
{
	float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
	return dx * dx + dy * dy + dz * dz;
}

static void render_fx(RMFrame *f, int mode, double param)
{
	Fx_SetModeParam(mode, param);
	rm_viewport_render(S.view, f);
}

/* The effect a machine shows while a pickup acts on it, at its pose. */
static void special_effect(const Machine *m, Vec3 pos, Mat3 rot, int is_player)
{
	float t = m->sp_timer;
	switch (m->sp_kind) {
	case 0:
		if (t < 24.0f) {
			set_pose(S.fx, pos, rot);
			render_fx(S.fx, FX_SPEED, t * (float)(1.0 / 24.0));
		}
		break;
	case 1:
		if (t < 24.0f) {
			set_pose(S.fx, pos, rot);
			render_fx(S.fx, FX_JUMPUP, t * (float)(1.0 / 24.0));
		}
		break;
	case 2:
		if (is_player ? t < g_pickups.dur[m->path_idx] : t < 50.0f) {
			set_pose(S.fx, pos, rot);
			render_fx(S.fx, FX_SPECIAL, is_player ? t / g_pickups.dur[m->path_idx] : t * 0.02f);
		}
		break;
	case 3:
		if (t < 50.0f) {
			/* the shock ring: on the machine when it ran out of power, else flat on the ground */
			if (g_racePhase == PH_NOPOWER) {
				set_pose(S.fx, pos, rot);
			}
			else {
				set_pose(S.fx, v3(pos.x, 5.0f, pos.z), Mat3_Identity());
			}
			render_fx(S.fx, FX_RING, t < 20.0f ? t * 0.05f : (t - 20.0f) * (float)(1.0 / 30.0));
		}
		break;
	}
}

static void render_scenery(void)
{
	for (int i = 0; i < g_scenery.count; i++) {
		if (dist2(g_machines[0].pos, g_scenery.pos[i]) < 81000000.0f) {
			Mat3 rot = Mat3_Mul(Mat3_Rotate((float)sin(g_scenery.spin[i]) * 0.4f, 0), Mat3_Rotate(g_scenery.yaw[i], 1));
			set_pose(S.fx, g_scenery.pos[i], rot);
			render_fx(S.fx, FX_SCENERY, 0.0);
		}
	}
}

/* 0x433100: gates (spinning ones turned around their axis) and buildings near the player. */
static void render_objects(void)
{
	for (int i = 0; i < g_gates.count; i++) {
		if (dist2(g_gates.pos[i], g_machines[0].pos) < 81000000.0f) {
			if (g_gates.speed[i] != 0.0f) {
				set_pose(S.gate[i], g_gates.pos[i], Mat3_Mul(Mat3_Rotate(g_gates.angle[i], 2), g_gates.rot[i]));
			}
			rm_viewport_render(S.view, S.gate[i]);
		}
	}
	for (int i = 0; i < S.nbuild; i++) {
		if (dist2(S.build_pos[i], g_machines[0].pos) < 81000000.0f) {
			rm_viewport_render(S.view, S.build[i]);
		}
	}
}

/* ---- HUD ---- */

static void frame_blit(const Sprite *s, int cell_w, int cell_h, int horizontal, int frame, int x, int y)
{
	if (horizontal) {
		sprite_blit(s, frame * cell_w, 0, cell_w, cell_h, x, y);
	}
	else {
		sprite_blit(s, 0, frame * cell_h, cell_w, cell_h, x, y);
	}
}

#define BIG(n, x, y) frame_blit(&S.hud.number2, 32, 32, 1, (n), (x), (y))
#define SMALL(n, x, y) frame_blit(&S.hud.number3, 20, 20, 1, (n), (x), (y))
#define KIGO(n, x, y) frame_blit(&S.hud.kigo, 20, 20, 1, (n), (x), (y))
#define INF(n, x, y) frame_blit(&S.hud.inf, 60, 20, 0, (n), (x), (y))

static uint32_t rgbf(float r, float g, float b)
{
	return (uint32_t)(int)(r * 255.0f) << 16 | (uint32_t)(int)(g * 255.0f) << 8 | (uint32_t)(int)(b * 255.0f);
}

/* GDI text over the frame (0x411440 and 0x410b20); the original's italic
 * variant is drawn upright. */
static void text(int x, int y, int size, uint32_t rgb, int align, const char *s)
{
	gfx_begin_2d();
	gfx_set_blend(GFX_BLEND_ALPHA);
	font_draw(x, y, size, rgb, align, s);
	gfx_end_2d();
}

/* seconds"hundredths */
static void format_time(char *buf, size_t n, const char *prefix, double t)
{
	int sec = (int)floor(t), hund = (int)(t * 100.0) % 100;
	snprintf(buf, n, hund < 10 ? "%s%d\"0%d" : "%s%d\"%2d", prefix, sec, hund);
}

static void render_info(void)
{
	Fx_SetMode(FX_INFO);
	rm_viewport_render(S.view, S.fx_world);
}

static void lap_message(const Machine *p)
{
	char buf[48];
	format_time(buf, sizeof(buf), "Lap ", p->lap_time[p->lap - 2]);
	text(470, 340, 12, rgbf(0.7f, 0.7f, 1.0f), FONT_CENTER, buf);
}

/* Two choices side by side; the selected one blinks between plain and 《bracketed》. */
static void menu_pair(const char *a, const char *a_sel, const char *b, const char *b_sel, int xa, int xb, int choice)
{
	int blink = (int)g_phaseTime % 20 < 10;
	text(xa, 230, 12, 0xffffff, FONT_CENTER, choice == 0 && blink ? a_sel : a);
	text(xb, 230, 12, 0xffffff, FONT_CENTER, choice != 0 && blink ? b_sel : b);
}

static void hud_racing(Race *r)
{
	const Machine *p = &g_machines[0];
	if (r->sub_time < 40.0) {
		render_info();
	}
	if (g_save.mode == MODE_REPLAY) {
		if ((int)g_phaseTime % 150 > 75) {
			text(600, 420, 20, rgbf(1.0f, 0.2f, 0.2f), FONT_RIGHT, "AceSpeeder!");
		}
		else {
			text(600, 420, 20, rgbf(0.2f, 1.0f, 0.2f), FONT_RIGHT, "REPLAY");
		}
	}
	gfx_begin_2d();
	gfx_set_blend(GFX_BLEND_ALPHA);
	if (g_save.mode != MODE_REPLAY) {
		float pw = p->power + p->power;
		int x0 = (int)(210.0f - pw), x1 = (int)(410.0f - pw);
		sprite_blit(&S.hud.power, x0, 0, x1 - x0, 32, 420, 20);
	}
	double t = g_track.reftime - p->time;
	if (t < 0.0) {
		t = 0.0;
	}
	INF(0, 20, 24);
	BIG((int)floor(t * 0.01) % 10, 20, 40);
	BIG((int)floor(t * 0.1) % 10, 50, 40);
	BIG((int)floor(t) % 10, 80, 40);
	SMALL((int)floor(t * 10.0) % 10, 112, 51);
	SMALL((int)floor(t * 100.0) % 10, 132, 51);
	INF(1, 20, 74);
	BIG(Race_PlayerRank() + 1, 20, 90);
	KIGO(0, 45, 101);
	SMALL(8, 60, 101);
	INF(2, 20, 124);
	BIG(p->lap, 20, 140);
	KIGO(0, 45, 151);
	SMALL(g_track.laps, 60, 151);
	if (g_save.mode == MODE_GRANDPRIX) {
		sprite_blit(&S.hud.left, 0, 0, 110, 20, 20, 450);
		SMALL(g_save.lives, 140, 450);
	}
	gfx_end_2d();
	if (p->lap >= 2 && p->lap_time[p->lap - 1] < 3.0 && (int)(g_phaseTime + g_phaseTime) % 100 < 60) {
		lap_message(p);
	}
}

static void hud_finished(Race *r)
{
	const Machine *p = &g_machines[0];
	char buf[48];
	switch (r->sub) {
	case 0:
	case 5:
		if (r->sub_time < (r->sub == 0 ? 200.0 : 500.0)) {
			render_info();
		}
		if (r->sub == 0 && g_phaseTime * 0.02 < 3.0 && (int)(g_phaseTime + g_phaseTime) % 100 < 60) {
			lap_message(p);
		}
		break;
	case 1: {
		text(320, 85, 36, 0xffffff, FONT_CENTER, "Result");
		uint32_t c = 0xc8c8ff;
		int replay = g_save.mode == MODE_REPLAY;
		text(150, 210, 16, c, FONT_LEFT, "Total Time");
		format_time(buf, sizeof(buf), Save_IsRecord(&g_save, p->time, 10) && !replay ? "new " : "", p->time);
		text(490, 210, 16, c, FONT_RIGHT, buf);
		text(150, 260, 16, c, FONT_LEFT, "Best Lap");
		format_time(buf, sizeof(buf), Save_IsRecord(&g_save, p->best_lap, 20) && !replay ? "new " : "", p->best_lap);
		text(490, 260, 16, c, FONT_RIGHT, buf);
		text(150, 310, 16, c, FONT_LEFT, "Position");
		snprintf(buf, sizeof(buf), "%d", p->rank);
		text(490, 310, 16, c, FONT_RIGHT, buf);
		break;
	}
	case 2:
		text(320, 160, 12, 0xffffff, FONT_CENTER, "リプレイデータを保存しますか？");
		menu_pair("保存する", "《保存する》", "保存しない", "《保存しない》", 220, 420, r->choice);
		break;
	case 3:
		text(320, 300, 25, 0xffffff, FONT_CENTER, "Try Next Stage!");
		break;
	case 6:
		text(320, 260, 12, 0xffffff, FONT_CENTER, "これから先のステージは");
		text(320, 300, 12, 0xffffff, FONT_CENTER, "パスワード入力をしてお楽しみください");
		break;
	}
}

static void hud(Race *r)
{
	char buf[48];
	switch (g_racePhase) {
	case PH_ROLLING: {
		if (r->sub_time > 100.0) {
			render_info();
		}
		snprintf(buf, sizeof(buf), "Stage %d-%d", g_save.stage + 1, g_save.course + 1);
		text(50, 50, 50, 0xffffff, FONT_LEFT, buf);
		format_time(buf, sizeof(buf), "Best Time ", g_save.best_time[g_save.stage][g_save.course]);
		text(590, 340, 12, 0xffffff, FONT_RIGHT, buf);
		format_time(buf, sizeof(buf), "Best Lap ", g_save.best_lap[g_save.stage][g_save.course]);
		text(590, 370, 12, 0xffffff, FONT_RIGHT, buf);
		break;
	}
	case PH_RACING:
		hud_racing(r);
		break;
	case PH_FINISHED:
		hud_finished(r);
		break;
	case PH_CRASHED:
	case PH_TIMEOVER:
	case PH_NOPOWER:
	case PH_FELL:
		if (r->sub == 0 || r->sub == 1) {
			render_info();
		}
		else if ((r->sub == 2 || r->sub == 3) && r->sub_time < 50.0) {
			render_info();
		}
		if (r->sub == 1 && g_save.mode == MODE_GRANDPRIX) {
			gfx_begin_2d();
			gfx_set_blend(GFX_BLEND_ALPHA);
			sprite_blit(&S.hud.left, 0, 0, 110, 20, 250, 380);
			SMALL(g_save.lives, 370, 380);
			gfx_end_2d();
		}
		break;
	case PH_PAUSED:
		text(320, 160, 12, 0xffffff, FONT_CENTER, "ＰＡＵＳＥ");
		if (g_save.mode == MODE_REPLAY) {
			menu_pair("リプレイを続ける", "《リプレイを続ける》", "メニューに戻る", "《メニューに戻る》", 210, 430, r->choice);
		}
		else {
			menu_pair("ゲームを続ける", "《ゲームを続ける》", "メニューに戻る", "《メニューに戻る》", 210, 430, r->choice);
		}
		break;
	case PH_GAMEOVER:
		if (r->sub == 0 || r->sub == 1 || (r->sub == 2 && r->sub_time < 50.0)) {
			render_info();
		}
		if (r->sub == 1) {
			text(320, 360, 24, rgbf(0.7f, 0.7f, 1.0f), FONT_CENTER, "Try Again!");
		}
		break;
	}
}

void Race_Render(Race *r)
{
	if (!S.scene) {
		return;
	}
	const Machine *p = &g_machines[0];
	Vec3 cam = g_camera.pos;
	set_pose(S.camera, g_cameraFramePos, g_cameraFrameRot);
	rm_viewport_clear(S.view);
	int opt = g_save.opt[3];
	if (opt > 2 && opt != 4) {
		gfx_begin_2d();
		sprite_stretch(&S.hud.null_blu, 0, 0, 160, 120, 0, 0, 640, 480);
		gfx_end_2d();
	}
	if (dist2(r->title_pos, cam) < 25000000.0f) {
		rm_viewport_render(S.view, S.title);
		rm_viewport_render(S.view, S.titbar);
	}
	rm_frame_set_position(S.ground, S.scene, (float)(floor(cam.x * 0.000125) * 8000.0), 0.0f,
	                      (float)(floor(cam.z * 0.000125) * 8000.0));
	rm_viewport_render(S.view, S.ground);
	if (opt < 3 || opt == 4) {
		rm_frame_set_position(S.sky, S.scene, cam.x, 0.0f, cam.z);
		rm_viewport_render(S.view, S.sky);
	}
	for (int i = 0; i <= g_track.n && i < S.meshes.nseg; i++) {
		if (sqrtf(dist2(g_track.center[i], cam)) < g_track.radius[i] + 5000.0f) {
			rm_viewport_render(S.view, S.seg[i]);
		}
	}
	render_objects();
	Fx_SetMode(FX_BOARDS);
	rm_viewport_render(S.view, S.fx_world);
	if (p->shadow_vis == 1) {
		set_pose(S.shadow, p->shadow_pos, p->shadow_rot);
		rm_viewport_render(S.view, S.shadow);
	}
	set_pose(S.player, g_playerFramePos, g_playerFrameRot);
	rm_viewport_render(S.view, S.player);
	special_effect(p, p->pos, p->rot, 1);
	set_pose(S.fx, p->dpos, p->drot);
	render_fx(S.fx, FX_BACKFIRE, p->throttle * 3.5f / k_max_throttle[p->model]);
	for (int i = 1; i < NUM_MACHINES; i++) {
		const Machine *m = &g_machines[i];
		if (sqrtf(dist2(m->pos, cam)) < 4000.0f) {
			set_pose(S.ai[i - 1], m->pos, m->rot);
			rm_viewport_render(S.view, S.ai[i - 1]);
			special_effect(m, m->pos, m->rot, 0);
			set_pose(S.fx, m->pos, m->rot);
			render_fx(S.fx, FX_BACKFIRE, 3.5);
		}
	}
	render_scenery();
	Fx_PrepareSparks(g_camera.rot);
	if (Fx_SparkCount() != 0) {
		Fx_SetMode(FX_SPARKS);
		rm_viewport_render(S.view, S.fx_world);
	}
	Fx_PrepareSmoke(g_camera.rot, g_camera.pos);
	if (Fx_SmokeCount() != 0) {
		Fx_SetMode(FX_SMOKE);
		rm_viewport_render(S.view, S.fx_world);
	}
	hud(r);
	switch (g_racePhase) {
	case PH_ROLLING:
	case PH_CRASHED:
	case PH_TIMEOVER:
	case PH_NOPOWER:
	case PH_FELL:
	case PH_NEXT:
	case PH_QUIT:
	case PH_GAMEOVER:
		Doors_Render(S.view, S.scene, S.sun, g_camera.rot);
		break;
	}
}

/* ---- attract-mode demo, game state 300 ----
 * Race_UpdateDemo (0x415280) and Race_RenderDemo (0x416570): after idling on the
 * title the menu sometimes shows the course instead of a replay. A camera flies
 * in over it with the credits, follows an unseen machine round the track, turns
 * around the title board and dives into a tunnel; A leaves early. */

static RMFrame *g_tunnel; /* 0x451854 */

/* 0x416420 */
static void demo_setup(void)
{
	g_tunnel = load_object("tunnel.x", v3(0, 0, 0), Mat3_Identity(), v3(1500.0f, 1500.0f, 48000.0f), 0);
	Machine *p = &g_machines[0];
	p->tp.seg = (int16_t)(g_track.n - 1);
	p->tp.t = 0.5f;
	TrackPos_Update(&p->tp);
	p->pos = Track_Point(&g_track, p->tp);
	p->rot = Track_Frame(&g_track, p->tp);
	p->model = 0;
}

static void demo_end(Race *r)
{
	Music_Stop();
	Sound_Stop(0);
	RM_RELEASE(g_tunnel); /* 0x416400 */
	Race_Cleanup(r);
	g_gameState = GS_MENU;
}

/* the chase view rocks around all three axes */
static void demo_sway(Race *r)
{
	Camera *c = &g_camera;
	r->demo_sway = (float)(r->demo_sway + 0.04);
	c->rot = Mat3_Mul(Mat3_Rotate((float)(sin(r->demo_sway) * 0.4), 0), c->rot);
	c->rot = Mat3_Mul(Mat3_Rotate((float)(sin(r->demo_sway * 1.2) * 0.4), 2), c->rot);
	c->rot = Mat3_Mul(Mat3_Rotate((float)(sin(r->demo_sway * 0.7) * 0.6), 1), c->rot);
}

static Mat3 title_rot(const Race *r)
{
	Mat3 m;
	memcpy(&m, r->u10, sizeof(m));
	return m;
}

/* the title board turns while the camera circles it from `local` */
static void demo_title_shot(Race *r, double s, Vec3 local)
{
	Mat3 tr = title_rot(r);
	set_pose(S.title, r->title_pos, Mat3_Mul(Mat3_Rotate((float)(s * 3.14), 1), tr));
	g_camera.pos = Vec3_Add(Mat3_MulVec(local, tr), r->title_pos);
	g_camera.rot = Mat3_Mul(Mat3_LookDir(local, v3(0, 0, 0)), tr);
}

void Race_UpdateDemo(Race *r)
{
	Camera *c = &g_camera;
	Machine *p = &g_machines[0];
	if (r->state == 20) {
		Race_Setup(r);
		demo_setup();
	}
	if (g_racePhase != 40 && g_racePhase != 50 && g_input[IN_A] && !g_inputPrev[IN_A]) {
		g_phaseTime = 0.0;
		g_racePhase = 50;
	}
	double pt = g_phaseTime;
	switch (g_racePhase) {
	case 0:
		Music_Play(g_track.s1a22 + 1);
		Sound_PlayLoop(0);
		g_phaseTime = pt = 0.0;
		g_racePhase = 10;
		/* fall through */
	case 10: /* fly in over the course */
		if (pt < 1500.0) {
			double s = pt * (1.0 / 1500.0);
			c->pos = Vec3_Quad(v3(50000.0f, 200.0f, 30000.0f), v3(30000.0f, 200.0f, 6000.0f),
			                   v3(15000.0f, 3200.0f, 3000.0f), (float)(s * s));
			c->rot = Mat3_LookDir(c->pos, v3((float)(c->pos.x - 600.0), 100.0f, (float)(c->pos.z - 360.0)));
			c->rot = Mat3_Mul(Mat3_Rotate((float)sin(s * -10.0), 2), c->rot);
			Doors_Open((float)pt);
		}
		else {
			g_phaseTime = 0.0;
			g_racePhase = 20;
			c->dist = 100.0f;
			Camera_StartRace(c);
		}
		c->pos.y = (float)(c->pos.y + 100.0);
		break;
	case 20: /* follow the machine round the track */
		if (pt >= 1200.0) {
			g_phaseTime = 0.0;
			g_racePhase = 30;
			break;
		}
		p->on_road = 1;
		p->grounded = 1;
		Machine_Update(p);
		Machine_UpdatePose(p);
		Camera_Chase(c);
		demo_sway(r);
		break;
	case 30: /* circle the title board */
		if (pt < 300.0) {
			double s = pt * (1.0 / 300.0), u = 1.0 - s, a = s * -6.28;
			float d = (float)(u * 2000.0 + 50.0);
			demo_title_shot(r, s, v3((float)(sin(a) * d), (float)(u * 800.0), (float)(cos(a) * d)));
		}
		else {
			g_phaseTime = 0.0;
			g_racePhase = 40;
		}
		break;
	case 40: /* into the tunnel */
		if (pt < 500.0) {
			double s = pt * (1.0 / 300.0), a = s * -6.28;
			float d = (float)(s * 500.0 + 50.0), y = (float)(s * 500.0);
			if (d > 200.0f) {
				d = 200.0f;
			}
			if (y > 100.0f) {
				y = 100.0f;
			}
			demo_title_shot(r, s, v3((float)(sin(a) * d), y, (float)(cos(a) * d)));
			Mat3 tr = title_rot(r);
			Vec3 q = Vec3_Lerp(v3(0, 0, 0), v3(0.0f, 0.0f, 10000.0f), (float)s);
			q.z = (float)((int)q.z % 8000);
			set_pose(g_tunnel, Vec3_Add(Mat3_MulVec(q, tr), r->title_pos), Mat3_Mul(Mat3_Rotate((float)(s * 6.28), 2), tr));
			if (pt > 300.0) {
				Doors_Close((float)(pt - 300.0));
				if (pt > 400.0) {
					demo_end(r);
					return;
				}
			}
		}
		break;
	case 50: /* A pressed: close the doors and leave */
		Machine_Update(p);
		Machine_UpdatePose(p);
		Camera_Chase(c);
		demo_sway(r);
		Doors_Close((float)pt);
		if (pt > 100.0) {
			demo_end(r);
			return;
		}
		break;
	}
	Gates_Update();
	Scenery_Update();
	g_phaseTime = x87_add(g_phaseTime, 1.65);
}

/* white text with a black drop shadow */
static void shadowed(int x, int y, int align, const char *s)
{
	text(x + 1, y + 1, 20, 0x000000, align, s);
	text(x, y, 20, 0xffffff, align, s);
}

void Race_RenderDemo(Race *r)
{
	if (!S.scene) {
		return;
	}
	char buf[48];
	int ph = g_racePhase, opt = g_save.opt[3];
	double pt = g_phaseTime;
	Vec3 cam = g_camera.pos;
	set_pose(S.camera, g_camera.pos, g_camera.rot);
	rm_viewport_clear(S.view);
	if (opt >= 3 && opt != 4) {
		gfx_begin_2d();
		sprite_stretch(&S.hud.null_blu, 0, 0, 160, 120, 0, 0, 640, 480);
		gfx_end_2d();
	}
	if (ph != 40) {
		rm_frame_set_position(S.ground, S.scene, (float)(floor(cam.x * 0.000125) * 8000.0), 0.0f,
		                      (float)(floor(cam.z * 0.000125) * 8000.0));
		rm_viewport_render(S.view, S.ground);
		if (opt < 3 || opt == 4) {
			rm_frame_set_position(S.sky, S.scene, cam.x, 0.0f, cam.z);
			rm_viewport_render(S.view, S.sky);
		}
	}
	int on_course = ph == 20 || ph == 30 || ph == 50;
	if (on_course) {
		for (int i = 0; i <= g_track.n && i < S.meshes.nseg; i++) {
			if (sqrtf(dist2(g_track.center[i], cam)) < g_track.radius[i] + 5000.0f) {
				rm_viewport_render(S.view, S.seg[i]);
			}
		}
		rm_viewport_render(S.view, S.title);
		rm_viewport_render(S.view, S.titbar);
	}
	if (ph == 40) {
		rm_viewport_render(S.view, S.title);
		rm_viewport_render(S.view, g_tunnel);
	}
	if (ph != 40 && ph != 10) {
		render_objects();
	}
	if (on_course) {
		render_scenery();
		Fx_SetMode(FX_BOARDS);
		rm_viewport_render(S.view, S.fx_world);
	}
	if (ph == 10) { /* credits over the fly-in */
		if (pt > 50.0 && pt < 280.0) {
			shadowed(40, 230, FONT_LEFT, "Directed by");
			shadowed(40, 260, FONT_LEFT, "Takahiro Nakatani");
		}
		else if (pt > 310.0 && pt < 540.0) {
			shadowed(130, 350, FONT_LEFT, "Graphic by");
			shadowed(130, 380, FONT_LEFT, "Wataru Kawakami");
		}
		else if (pt > 570.0 && pt < 800.0) {
			shadowed(600, 160, FONT_RIGHT, "Sounds by");
			shadowed(600, 190, FONT_RIGHT, "Kazumasa Kohara");
		}
		else if (pt > 830.0 && pt < 1060.0) {
			shadowed(540, 300, FONT_RIGHT, "Composed by");
			shadowed(540, 330, FONT_RIGHT, "T.Phuta");
		}
		else if (pt > 1100.0 && pt < 1450.0) {
			HudMsg_Logo(&g_hudMsg, (float)((pt - 1100.0) * (1.0 / 350.0)));
			Fx_SetMode(FX_ACE);
			rm_viewport_render(S.view, S.fx_world);
		}
	}
	else if (ph == 20) {
		text(20, 10, 20, rgbf(1.0f, 0.3f, 0.3f), FONT_LEFT, "AceSpeeder!");
		snprintf(buf, sizeof(buf), "Stage %d-%d", g_save.stage + 1, g_save.course + 1);
		text(610, 425, 20, rgbf(0.3f, 0.3f, 1.0f), FONT_RIGHT, buf);
	}
	if (ph == 10 || ph == 40 || ph == 50) {
		Doors_Render(S.view, S.scene, S.sun, g_camera.rot);
	}
}
