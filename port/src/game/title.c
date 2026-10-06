/* Title sequence, game state 900: Title_Update (0x419f80) flies the camera
 * along a scripted path while the "Ace" and "Speeder" logo pieces scroll
 * past, then assembles the logo; Title_Render is 0x41bbf0. The timeline is
 * g_phaseTime, which starts at -20 and grows 1.65 per tick. */
#include <math.h>

#include "../engine/rm.h"
#include "../platform/platform.h"
#include "doors.h"
#include "machine.h"
#include "race.h"
#include "title.h"

Title g_title;

static struct {
	RMFrame *scene, *camera, *sun;
	RMViewport *view;
	RMFrame *pro;   /* 0x452ddc the assembled logo */
	RMFrame *ace;   /* 0x452de0 tt.x */
	RMFrame *speed; /* 0x452de4 nn.x */
	RMFrame *ground;/* 0x452de8 */
} T;

static void set_pose(RMFrame *f, Vec3 p, Mat3 r)
{
	rm_frame_set_position(f, T.scene, p.x, p.y, p.z);
	rm_frame_set_orientation(f, T.scene, r.m[6], r.m[7], r.m[8], r.m[3], r.m[4], r.m[5]);
}

static RMFrame *load_object(const char *name, Vec3 pos, Vec3 scale, int with_material)
{
	RMFrame *f = rm_frame_create(T.scene);
	set_pose(f, pos, Mat3_Identity());
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

/* 0x419a20 */
static void title_setup(void)
{
	g_phaseTime = -20.0;
	rm_set_render_mode(0);
	T.scene = rm_frame_create(NULL);
	T.camera = rm_frame_create(T.scene);
	set_pose(T.camera, v3(0, 0, 0), Mat3_Identity());
	T.view = rm_viewport_create(T.camera, 0, 0, gfx_screen_w(), gfx_screen_h());
	RMLight *amb = rm_light_create_rgb(RM_LIGHT_AMBIENT, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(T.scene, amb);
	rm_release(amb);
	T.sun = rm_frame_create(T.scene);
	set_pose(T.sun, v3(-120.0f, 90.0f, 0.0f), Mat3_LookDir(v3(-120.0f, 90.0f, 0.0f), v3(0, 0, 0)));
	RMLight *sun = rm_light_create_rgb(RM_LIGHT_DIRECTIONAL, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(T.sun, sun);
	rm_release(sun);

	T.pro = load_object("pro.x", v3(0.0f, 100.0f, -100.0f), v3(300.0f, 180.0f, 180.0f), 0);
	T.ace = load_object("tt.x", v3(-120.0f, -100.0f, 0.0f), v3(100.0f, 100.0f, 100.0f), 1);
	T.speed = load_object("nn.x", v3(120.0f, -100.0f, 0.0f), v3(100.0f, 100.0f, 100.0f), 1);
	T.ground = load_object("gro.x", v3(120.0f, -100.0f, 0.0f), v3(10000.0f, 10000.0f, 10000.0f), 0);
	Doors_Init(T.camera);

	rm_frame_set_scene_background_rgb(T.scene, 0, 0, 0);
	rm_frame_set_scene_fog_enable(T.scene, 1);
	rm_frame_set_scene_fog_color(T.scene, 0xff000000u);
	rm_frame_set_scene_fog_params(T.scene, 1000.0f, 5000.0f, 1.0f);
	rm_viewport_set_back(T.view, 5000.0f);
	rm_viewport_set_field(T.view, 1.0f);
}

/* 0x4199a0 */
static void title_free(Title *t)
{
	t->state = 20;
	RM_RELEASE(T.pro);
	RM_RELEASE(T.ace);
	RM_RELEASE(T.speed);
	RM_RELEASE(T.ground);
	Doors_Free();
	RM_RELEASE(T.sun);
	RM_RELEASE(T.camera);
	RM_RELEASE(T.view);
	RM_RELEASE(T.scene);
}

static Mat3 look(Vec3 from, Vec3 to)
{
	return Mat3_LookDir(from, to);
}

/* The camera path: position t->cam and orientation t->rot for the current
 * time; returns the roll around the view axis, or NAN for none. */
static float camera_path(Title *t)
{
	double pt = g_phaseTime;
	Vec3 c;
	float roll = NAN;
	if (pt < 150.0) { /* swoop down from high above */
		float s = (float)(pt * (1.0 / 150.0));
		c = Vec3_Quad(v3(10000.0f, 4200.0f, 6000.0f), v3(3000.0f, 200.0f, 1000.0f), v3(-200.0f, 250.0f, -100.0f), s);
		t->rot = look(c, v3(c.x, 0.0f, (float)(c.z - 500.0)));
		roll = (float)(s * 6.28);
	}
	else if (pt < 200.0) {
		float s = (float)((pt - 150.0) * 0.02);
		c = Vec3_Lerp(v3(-200.0f, 250.0f, -100.0f), v3(-200.0f, 100.0f, -100.0f), s);
		t->rot = look(c, Vec3_Lerp(v3(-200.0f, 0.0f, -600.0f), v3(-250.0f, 100.0f, 0.0f), s));
	}
	else if (pt < 350.0) {
		float s = (float)((pt - 200.0) * (1.0 / 150.0));
		c = Vec3_Lerp(v3(-200.0f, 100.0f, -100.0f), v3(250.0f, 150.0f, -120.0f), s);
		t->rot = look(c, Vec3_Lerp(v3(-250.0f, 100.0f, 0.0f), v3(200.0f, 120.0f, 0.0f), s));
	}
	else if (pt < 450.0) {
		float s = (float)((pt - 350.0) * 0.01);
		c = Vec3_Lerp(v3(250.0f, 150.0f, -120.0f), v3(20.0f, 80.0f, -230.0f), s);
		t->rot = look(c, Vec3_Lerp(v3(200.0f, 120.0f, 0.0f), v3(-50.0f, 80.0f, 0.0f), s));
	}
	else if (pt < 500.0) {
		float s = (float)((pt - 450.0) * 0.02);
		c = Vec3_Lerp(v3(20.0f, 80.0f, -230.0f), v3(-80.0f, 80.0f, 250.0f), s);
		t->rot = look(c, Vec3_Lerp(v3(-50.0f, 80.0f, 0.0f), v3(0.0f, 120.0f, 100.0f), s));
	}
	else if (pt < 550.0) {
		float s = (float)((pt - 500.0) * 0.02);
		c = Vec3_Lerp(v3(-80.0f, 80.0f, 250.0f), v3(500.0f, 100.0f, 0.0f), s);
		t->rot = look(c, Vec3_Lerp(v3(0.0f, 120.0f, 100.0f), v3(0.0f, 100.0f, 0.0f), s));
	}
	else if (pt < 700.0) { /* rising spiral */
		double s = (pt - 550.0) * (1.0 / 150.0);
		float r = (float)((s + 1.0) * 500.0);
		double a = s * -6.28;
		c = v3((float)(cos(a) * r), (float)(s * 700.0 + 100.0), (float)(sin(a) * r));
		t->rot = look(c, v3(0.0f, 100.0f, 0.0f));
		roll = (float)a;
	}
	else if (pt < 750.0) {
		double s = (pt - 700.0) * 0.02;
		double r = 1000.0 - s * 800.0, a = s * -1.57;
		c = v3((float)(r * cos(a)), (float)(800.0 - s * 700.0), (float)(r * sin(a)));
		t->rot = look(c, v3(0.0f, 100.0f, 0.0f));
	}
	else if (pt < 850.0) { /* half circle, banking */
		double u = (pt - 750.0) * 0.01 * 3.14, a = -1.57 - u;
		c = v3((float)(cos(a) * 200.0), 100.0f, (float)(sin(a) * 200.0));
		t->rot = look(c, v3(0.0f, 100.0f, 0.0f));
		roll = (float)(sin(u) * -1.2);
	}
	else if (pt < 1050.0) { /* full circle, then away */
		float s = (float)((pt - 850.0) * 0.005);
		double r = s * 250.0 + 200.0;
		if (s >= 0.5f) {
			r += (s - 0.5) * 4000.0;
		}
		double a = -4.71 - s * 6.28;
		c = v3((float)(cos(a) * r), s > 0.5f ? 300.0f : (float)(s * 400.0 + 100.0), (float)(sin(a) * r));
		t->rot = look(c, v3(0.0f, 100.0f, 0.0f));
	}
	else if (pt < 1150.0) { /* climb over the logo */
		float s = (float)((pt - 1050.0) * 0.01);
		c = Vec3_Quad(v3(0.0f, 300.0f, 2450.0f), v3(0.0f, -400.0f, -300.0f), v3(0.0f, 1000.0f, -500.0f), s);
		t->rot = look(c, Vec3_Quad(v3(0.0f, 100.0f, 0.0f), v3(0.0f, -400.0f, -300.0f), v3(0.0f, 1200.0f, -550.0f),
		                           s + 0.01f));
		roll = (float)(sin(s * 5.0) * 0.5);
	}
	else {
		return NAN; /* the logo shot below sets the camera itself */
	}
	t->cam = c;
	return roll;
}

void Title_Update(Title *t)
{
	if (t->state == 20) {
		title_setup();
		t->state = 10;
	}
	if (g_input[4] != 0 && g_phaseTime > 60.0 && g_phaseTime < 1150.0) {
		g_phaseTime = 1150.0; /* skip to the logo */
	}
	double pt = g_phaseTime;
	/* the logo pieces stream past along a slanted line */
	Vec3 drift = v3((float)(sin(0.25) * pt * -80.0), 0.0f, (float)(cos(0.25) * pt * -80.0));
	Mat3 r = Mat3_Rotate(3.39f, 1);
	set_pose(T.ace, Vec3_Add(Mat3_MulVec(v3(120.0f, 100.0f, 0.0f), r), drift), r);
	Mat3 rs = r;
	for (int i = 6; i < 9; i++) {
		rs.m[i] = -rs.m[i];
	}
	set_pose(T.speed, Vec3_Add(Mat3_MulVec(v3(-120.0f, 100.0f, 0.0f), r), drift), rs);

	if (pt < 1150.0) {
		float roll = camera_path(t);
		if (!isnan(roll)) {
			t->rot = Mat3_Mul(Mat3_Rotate(roll, 2), t->rot);
		}
		/* the whole path is turned a little around the vertical */
		Mat3 y = Mat3_Rotate(0.25f, 1);
		t->cam = Vec3_Add(Mat3_MulVec(t->cam, y), drift);
		t->rot = Mat3_Mul(t->rot, y);
		set_pose(T.camera, t->cam, t->rot);
	}
	if (pt > 1150.0) {
		/* the two halves of the logo turn into place */
		if (pt < 1200.0) {
			float s = (float)((pt - 1150.0) * 0.02);
			double u = 1.0 - s;
			set_pose(T.ace, v3(-120.0f, -100.0f, 0.0f), Mat3_Rotate((float)(u * 2.5), 0));
			set_pose(T.speed, Vec3_Lerp(v3(120.0f, -100.0f, -200.0f), v3(120.0f, -100.0f, 0.0f), s),
			         Mat3_Rotate((float)(u * -2.5), 0));
			if (s > 1.0f) {
				s = 1.0f;
			}
			float d = s * 600.0f + 100.0f;
			double a = (1.0 - s) * -0.77;
			t->cam = v3((float)(sin(a) * d), 0.0f, (float)(cos(a) * -d));
		}
		else {
			set_pose(T.ace, v3(-120.0f, -100.0f, 0.0f), Mat3_Identity());
			set_pose(T.speed, v3(120.0f, -100.0f, 0.0f), Mat3_Identity());
			t->cam = v3(0.0f, 0.0f, -700.0f);
		}
		t->rot = Mat3_LookDir(t->cam, v3(0, 0, 0));
		set_pose(T.camera, t->cam, t->rot);
		if (pt > 1200.0) { /* the AceSpeeder! board slides in */
			float s = (float)((pt - 1200.0) * 0.02);
			if (s > 1.0f) {
				s = 1.0f;
			}
			double u = 1.0 - s;
			set_pose(T.pro, Vec3_Lerp(v3(1200.0f, 100.0f, 0.0f), v3(0.0f, 100.0f, 0.0f), (float)(1.0 - u * u * u)),
			         Mat3_Identity());
		}
		/* the sun sweeps over the logo */
		if (pt < 1200.0) {
			set_pose(T.sun, rm_frame_get_position(T.sun, T.scene), Mat3_LookDir(v3(-60.0f, 60.0f, -150.0f), v3(0, 0, 0)));
		}
		else if (pt < 1280.0) {
			double v = 1.0 - (pt - 1200.0) * 0.0125;
			double w = 1.0 - v * v;
			Vec3 to = v3((float)(sin(w * 6.28) * 300.0), (float)(sin(w * 3.28) * 150.0), 0.0f);
			set_pose(T.sun, rm_frame_get_position(T.sun, T.scene), Mat3_LookDir(v3(-60.0f, 60.0f, -150.0f), to));
		}
	}
	/* the ground plane follows the camera in 2500-unit steps */
	rm_frame_set_position(T.ground, T.scene, (float)((int)(t->cam.x * 0.0004f) * 2500), 0.0f,
	                      (float)((int)(t->cam.z * 0.0004f) * 2500));
	if (pt > 1360.0) {
		title_free(t);
		g_gameState = GS_MENU;
	}
	g_phaseTime = x87_add(g_phaseTime, 1.65);
}

void Title_Render(Title *t)
{
	double pt = g_phaseTime;
	rm_viewport_clear(T.view);
	if (pt >= 150.0) {
		rm_viewport_render(T.view, T.ace);
		rm_viewport_render(T.view, T.speed);
	}
	if (pt <= 1150.0) {
		rm_viewport_render(T.view, T.ground);
	}
	if (pt > 1210.0) {
		rm_viewport_render(T.view, T.pro);
	}
	if (pt < 100.0) {
		Doors_Open(pt >= 0.0 ? (float)(pt * 1.65) : 0.0f);
		Doors_Render(T.view, T.scene, T.sun, t->rot);
	}
	if (pt > 1280.0) {
		Doors_Close((float)((pt - 1280.0) * 1.65));
		Doors_Render(T.view, T.scene, T.sun, t->rot);
	}
}

void Title_Free(Title *t)
{
	if (T.scene) {
		title_free(t);
	}
}
