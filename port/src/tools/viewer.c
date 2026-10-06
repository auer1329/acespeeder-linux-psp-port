/* Engine smoke test: spins a .x mesh under the game's lighting setup and
 * draws a 2D sprite on top. Usage: viewer [mesh.x] [frames] [screenshot.ppm] */
#include <stdlib.h>
#include <string.h>

#include "../engine/image.h"
#include "../engine/rm.h"
#include "../platform/platform.h"

int main(int argc, char **argv)
{
#ifdef __PSP__
	/* No command line on the PSP: render a fixed number of frames and dump a screenshot. */
	const char *mesh = "machine1.x";
	int frames = 90;
	const char *shot = "save/viewer.ppm";
	(void)argc, (void)argv;
#else
	const char *mesh = argc > 1 ? argv[1] : "machine1.x";
	int frames = argc > 2 ? atoi(argv[2]) : 0;
	const char *shot = argc > 3 ? argv[3] : NULL;
#endif
	if (plat_init() != 0) {
		return 1;
	}
	rm_init();

	RMFrame *scene = rm_frame_create(NULL);
	rm_frame_set_scene_background_rgb(scene, 0.1f, 0.1f, 0.3f);
	RMFrame *camera = rm_frame_create(scene);
	rm_frame_set_position(camera, scene, 0, 1.5f, -4.0f);
	rm_frame_set_orientation(camera, scene, 0, -0.3f, 1, 0, 1, 0);

	/* The game's lights: 0.5 ambient + 0.5 directional. */
	RMLight *amb = rm_light_create_rgb(RM_LIGHT_AMBIENT, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(scene, amb);
	rm_release(amb);
	RMFrame *sun = rm_frame_create(scene);
	rm_frame_set_orientation(sun, scene, 0.5f, -1.0f, 0.7f, 0, 1, 0);
	RMLight *dir = rm_light_create_rgb(RM_LIGHT_DIRECTIONAL, 0.5f, 0.5f, 0.5f);
	rm_frame_add_light(sun, dir);
	rm_release(dir);

	RMFrame *obj = rm_frame_create(scene);
	RMMeshBuilder *mb = rm_mb_create();
	if (rm_mb_load(mb, mesh) != 0) {
		fprintf(stderr, "failed to load %s\n", mesh);
	}
	Vec3 lo, hi;
	rm_mb_get_box(mb, &lo, &hi);
	float size = fmaxf(hi.x - lo.x, fmaxf(hi.y - lo.y, hi.z - lo.z));
	if (size > 0) {
		rm_mb_translate(mb, -(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -(lo.z + hi.z) / 2);
		rm_mb_scale(mb, 3.0f / size, 3.0f / size, 3.0f / size);
	}
	rm_mb_set_quality(mb, RM_SHADE_GOURAUD | RM_LIGHT_ON | RM_FILL_SOLID);
	rm_frame_add_visual(obj, mb);
	printf("%s: %d vertices, %d faces\n", mesh, rm_mb_vertex_count(mb), rm_mb_face_count(mb));
	rm_release(mb);

	RMViewport *view = rm_viewport_create(camera, 0, 0, gfx_screen_w(), gfx_screen_h());
	rm_viewport_set_back(view, 5000.0f);
	rm_viewport_set_field(view, 1.0f);

	Image logo;
	int logo_tex = -1;
	if (image_load_bmp(&logo, "ace.bmp") == 0) {
		image_color_key(&logo, 0);
		logo_tex = gfx_texture_create(logo.w, logo.h, logo.px, 0);
	}

	float angle = 0.0f;
	for (int n = 0; !plat_should_quit(); n++) {
		int32_t keys[IN_COUNT] = {0};
		plat_read_input(keys);
		if (keys[IN_ESC]) {
			break;
		}
		angle += 0.02f;
		rm_frame_set_orientation(obj, scene, sinf(angle), 0, cosf(angle), 0, 1, 0);
		gfx_begin_frame();
		rm_viewport_clear(view);
		rm_viewport_render(view, scene);
		if (logo_tex >= 0) {
			gfx_begin_2d();
			gfx_draw_quad_2d(logo_tex, 0, 0, logo.w, logo.h, 8, 8, logo.w, logo.h, 0xffffffff);
			gfx_end_2d();
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
	rm_release(obj);
	rm_release(scene);
	plat_shutdown();
	return 0;
}
