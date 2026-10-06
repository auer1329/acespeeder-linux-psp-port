#include "doors.h"
#include <stddef.h>


#include "../platform/platform.h"

static RMFrame *g_doors[2];
static RMFrame *g_camera;
static float g_sx = 1.0f, g_sy = 1.0f;

void Doors_Init(RMFrame *camera, float field)
{
	/* The shutter was made to fill a 4:3 view with a field of 1: stretch it
	 * to the screen's shape and the view's field so it covers the edges. */
	g_sx = field * gfx_pixel_aspect();
	g_sy = field;
	static const char *const names[2] = {"doora.x", "doorb.x"};
	g_camera = camera;
	for (int k = 0; k < 2; k++) {
		g_doors[k] = rm_frame_create(camera);
		RMMeshBuilder *mb = rm_mb_create();
		rm_mb_load(mb, names[k]);
		rm_mb_scale(mb, 60.0f * g_sx, 60.0f * g_sy, 100.0f);
		rm_mb_set_quality(mb, 0x88);
		rm_mb_set_perspective(mb, 1);
		RMMaterial *m = rm_material_create(50.0f);
		rm_material_set_power(m, 50.0f);
		rm_mb_set_material(mb, m);
		rm_release(m);
		rm_frame_add_visual(g_doors[k], mb);
		rm_release(mb);
	}
}

void Doors_Free(void)
{
	RM_RELEASE(g_doors[0]);
	RM_RELEASE(g_doors[1]);
	g_camera = NULL;
}

/* w = 0: closed over the view, 1: fully open. */
static void place(float w)
{
	if (!g_doors[0]) {
		return;
	}
	rm_frame_set_position(g_doors[0], g_camera, 0.0f, (w * 80.0f - 0.5f) * g_sy, 50.0f);
	rm_frame_set_position(g_doors[1], g_camera, 0.0f, (0.5f - w * 80.0f) * g_sy, 50.0f);
	for (int k = 0; k < 2; k++) {
		rm_frame_set_orientation(g_doors[k], g_camera, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);
	}
}

void Doors_Open(float t)
{
	place(Wave_EaseIn(t * 0.015f));
}

void Doors_Close(float t)
{
	place(Wave_Bounce(t * 0.008f));
}

void Doors_Render(RMViewport *view, RMFrame *scene, RMFrame *sun, Mat3 cam_rot)
{
	if (!g_doors[0]) {
		return;
	}
	Vec3 dir, up;
	rm_frame_get_orientation(sun, scene, &dir, &up);
	Mat3 m = Mat3_Mul(Mat3_Mul(Mat3_Rotate(2.14f, 0), Mat3_Rotate(3.84f, 1)), cam_rot);
	rm_frame_set_orientation(sun, scene, m.m[6], m.m[7], m.m[8], m.m[3], m.m[4], m.m[5]);
	rm_viewport_render(view, g_doors[0]);
	rm_viewport_render(view, g_doors[1]);
	rm_frame_set_orientation(sun, scene, dir.x, dir.y, dir.z, up.x, up.y, up.z);
}
