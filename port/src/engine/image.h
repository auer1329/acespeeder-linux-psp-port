#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

/* RGBA8888 image, top row first; pixel = 0xAABBGGRR (little-endian R,G,B,A bytes). */
typedef struct {
	int w, h;
	uint32_t *px;
} Image;

/* Loads an uncompressed 8/24/32-bit BMP from the game data. Alpha is 255. */
int image_load_bmp(Image *img, const char *name);
/* Makes every pixel equal to `key` (RGB part) fully transparent. */
void image_color_key(Image *img, uint32_t key);
void image_free(Image *img);

#endif
