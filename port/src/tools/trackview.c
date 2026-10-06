/* Course smoke test: loads track_<stage>_<course>.bin and flies the camera
 * along the racing line with the original's scene setup.
 * Usage: trackview [stage] [course] [frames] [screenshot.ppm] */
#include <stdlib.h>
#include <string.h>

#include "../engine/rm.h"
#include "../game/track.h"
#include "../platform/platform.h"

static RMFrame *load_object(RMFrame *parent, const char *name, float sx, float sy, float sz, uint32_t quality)
{
	RMFrame *f = rm_frame_create(parent);
	RMMeshBuilder *mb = rm_mb_create();
	rm_mb_load(mb, name);
	rm_mb_scale(mb, sx, sy, sz);
	rm_mb_set_quality(mb, quality);
	rm_frame_add_visual(f, mb);
	rm_release(mb);
	return f;
}

int main(int argc, char **argv)
{
#ifdef __PSP__
	int stage = 1, course = 1, frames = 120;
	const char *shot = "save/trackview.ppm";
	(void)argc, (void)argv;
#else
	int stage = argc > 1 ? atoi(argv[1]) : 1;
	int course = argc > 2 ? atoi(argv[2]) : 1;
	int frames = argc > 3 ? atoi(argv[3]) : 0;
	const char *shot = argc > 4 ? argv[4] : NULL;
#endif
	if (plat_init() != 0) {
		return 1;
	}
	rm_init();
	rm_set_render_mode(1);

	RMFrame *scene = rm_frame_create(NULL);
	RMFrame *camera = rm_frame_create(scene);
	RMViewport *view = rm_viewport_create(camera, 0, 0, gfx_screen_w(), gfx_screen_h());
	rm_viewport_set_field(view, 1.0f);
	rm_viewport_set_back(view, 20000.0f);

	RMLight *amb = rm_light_create_rgb(RM_LIGHT_AMBIENT, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(scene, amb);
	rm_release(amb);

	char name[32];
	TrackMeshes meshes;
	if (Track_Load(stage, course, &meshes) != 0) {
		plat_shutdown();
		return 1;
	}
	printf("course %d-%d: %d segments, laps %d, start %.2f, env %d\n", stage, course, meshes.nseg, g_track.laps,
	       g_track.f15d0, g_track.env);

	/* Directional light frame placed like the original (at the start, along the track). */
	RMFrame *sun = rm_frame_create(scene);
	TrackPos p0 = TrackPos_Make(0.0f, 3.5f, 0.0f);
	Vec3 sp = Track_Point(&g_track, p0);
	rm_frame_set_position(sun, scene, sp.x, sp.y, sp.z);
	Mat3 look = Mat3_Mul(Mat3_LookDir(v3(100, 100, 0), v3(0, 0, 0)), Track_Frame(&g_track, p0));
	rm_frame_set_orientation(sun, scene, look.m[6], look.m[7], look.m[8], look.m[3], look.m[4], look.m[5]);
	RMLight *dl = rm_light_create_rgb(RM_LIGHT_DIRECTIONAL, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(sun, dl);
	rm_release(dl);

	RMFrame *segs[TRACK_MAX_POINTS];
	for (int i = 0; i < meshes.nseg; i++) {
		segs[i] = rm_frame_create(scene);
		rm_frame_set_position(segs[i], scene, meshes.seg[i].pos.x, meshes.seg[i].pos.y, meshes.seg[i].pos.z);
		rm_frame_add_visual(segs[i], meshes.seg[i].mb);
		rm_frame_set_scene_fog_enable(segs[i], 1);
		rm_frame_set_scene_fog_color(segs[i], 0xff0c0019);
		rm_frame_set_scene_fog_params(segs[i], 6000.0f, 8000.0f, 1.0f);
	}
	snprintf(name, sizeof(name), "ground%d.x", g_track.env + 1);
	RMFrame *ground = load_object(scene, name, 32000, 32000, 32000, 0x80);
	snprintf(name, sizeof(name), "sky%d.x", g_track.env + 1);
	RMFrame *sky = load_object(scene, name, 16200, 10125, 16200, 0x80);

	float s = g_track.f15d0;
	for (int n = 0; !plat_should_quit(); n++) {
		int32_t keys[IN_COUNT] = {0};
		plat_read_input(keys);
		if (keys[IN_ESC]) {
			break;
		}
		s += keys[IN_UP] ? 0.05f : keys[IN_DOWN] ? -0.05f : 0.01f;
		if (s >= g_track.n + 1) {
			s -= g_track.n + 1;
		}
		if (s < 0) {
			s += g_track.n + 1;
		}
		TrackPos cp = TrackPos_Make(s, 3.5f, 200.0f);
		Vec3 cpos = Track_Point(&g_track, cp);
		Mat3 cf = Track_Frame(&g_track, cp);
		rm_frame_set_position(camera, scene, cpos.x, cpos.y, cpos.z);
		rm_frame_set_orientation(camera, scene, cf.m[6], cf.m[7] - 0.15f, cf.m[8], cf.m[3], cf.m[4], cf.m[5]);
		/* Sky and ground follow the camera horizontally, like in the race. */
		rm_frame_set_position(sky, scene, cpos.x, 0, cpos.z);
		rm_frame_set_position(ground, scene, cpos.x, 0, cpos.z);

		gfx_begin_frame();
		rm_viewport_clear(view);
		rm_viewport_render(view, sky);
		rm_viewport_render(view, ground);
		for (int i = 0; i < meshes.nseg; i++) {
			rm_viewport_render(view, segs[i]);
		}
		if (frames && n + 1 >= frames) {
			if (shot) {
				plat_screenshot(shot);
			}
			gfx_end_frame();
			break;
		}
		gfx_end_frame();
	}
	Track_FreeMeshes(&meshes);
	rm_release(scene);
	plat_shutdown();
	return 0;
}
