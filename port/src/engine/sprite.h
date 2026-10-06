/* DirectDraw-style sprites: a BMP whose black pixels are transparent (the
 * original's colour-keyed off-screen surfaces), blitted in 640x480 screen
 * coordinates. Draw calls go between gfx_begin_2d and gfx_end_2d. */
#ifndef SPRITE_H
#define SPRITE_H

typedef struct {
	int tex; /* -1 when not loaded */
	int w, h;
} Sprite;

int sprite_load(Sprite *s, const char *name);
void sprite_free(Sprite *s);
/* Copies the source rectangle to (dx, dy) at the same size. */
void sprite_blit(const Sprite *s, int sx, int sy, int sw, int sh, int dx, int dy);
/* Stretches the source rectangle over the destination rectangle. */
void sprite_stretch(const Sprite *s, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh);

#endif
