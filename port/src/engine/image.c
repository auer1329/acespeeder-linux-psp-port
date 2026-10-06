#include "image.h"

#include <stdlib.h>
#include <string.h>

#include "../platform/platform.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int image_load_bmp(Image *img, const char *name)
{
	memset(img, 0, sizeof(*img));
	FILE *f = plat_open_data(name, "rb");
	if (!f) {
		return -1;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = malloc(size);
	if (fread(buf, 1, size, f) != (size_t)size || size < 54 || buf[0] != 'B' || buf[1] != 'M') {
		fclose(f);
		free(buf);
		return -1;
	}
	fclose(f);
	uint32_t off = rd32(buf + 10);
	uint32_t hdr = rd32(buf + 14);
	int w = (int)rd32(buf + 18);
	int h = (int)rd32(buf + 22);
	int bpp = rd16(buf + 28);
	uint32_t comp = rd32(buf + 30);
	int bottom_up = h > 0;
	if (h < 0) {
		h = -h;
	}
	if (comp != 0 || (bpp != 8 && bpp != 24 && bpp != 32) || w <= 0 || h <= 0) {
		free(buf);
		return -1;
	}
	const uint8_t *pal = buf + 14 + hdr;
	int stride = ((w * bpp + 31) / 32) * 4;
	if (off + (uint32_t)stride * h > (uint32_t)size) {
		free(buf);
		return -1;
	}
	img->w = w;
	img->h = h;
	img->px = malloc(sizeof(uint32_t) * w * h);
	for (int y = 0; y < h; y++) {
		const uint8_t *row = buf + off + (size_t)stride * (bottom_up ? h - 1 - y : y);
		uint32_t *out = img->px + (size_t)y * w;
		for (int x = 0; x < w; x++) {
			uint8_t r, g, b;
			if (bpp == 8) {
				const uint8_t *c = pal + row[x] * 4;
				b = c[0], g = c[1], r = c[2];
			}
			else {
				const uint8_t *c = row + x * (bpp / 8);
				b = c[0], g = c[1], r = c[2];
			}
			out[x] = r | g << 8 | b << 16 | 0xff000000u;
		}
	}
	free(buf);
	return 0;
}

void image_color_key(Image *img, uint32_t key)
{
	key &= 0x00ffffff;
	for (int i = 0; i < img->w * img->h; i++) {
		if ((img->px[i] & 0x00ffffff) == key) {
			img->px[i] = 0;
		}
	}
}

void image_free(Image *img)
{
	free(img->px);
	memset(img, 0, sizeof(*img));
}
