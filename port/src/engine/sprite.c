#include "sprite.h"

#include "../platform/platform.h"
#include "image.h"

int sprite_load(Sprite *s, const char *name)
{
	Image img;
	s->tex = -1;
	s->w = s->h = 0;
	if (image_load_bmp(&img, name) != 0) {
		return -1;
	}
	image_color_key(&img, 0x000000);
	s->tex = gfx_texture_create(img.w, img.h, img.px, 0);
	s->w = img.w;
	s->h = img.h;
	image_free(&img);
	return s->tex >= 0 ? 0 : -1;
}

void sprite_free(Sprite *s)
{
	if (s->tex >= 0) {
		gfx_texture_destroy(s->tex);
	}
	s->tex = -1;
}

void sprite_blit(const Sprite *s, int sx, int sy, int sw, int sh, int dx, int dy)
{
	sprite_stretch(s, sx, sy, sw, sh, dx, dy, sw, sh);
}

void sprite_stretch(const Sprite *s, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh)
{
	if (s->tex < 0) {
		return;
	}
	gfx_draw_quad_2d(s->tex, (float)sx, (float)sy, (float)sw, (float)sh, (float)dx, (float)dy, (float)dw, (float)dh,
	                 0xffffffffu);
}
