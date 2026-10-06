/* Linux backend: SDL2 window/input/audio + OpenGL 1.x fixed function. */
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../mixer.h"
#include "../platform.h"

#define VIRT_W 640
#define VIRT_H 480

static SDL_Window *g_win;
static SDL_GLContext g_gl;
static SDL_AudioDeviceID g_audio;
static SDL_GameController *g_pad;
static int g_quit;
static char g_data_dir[512];
static char g_save_dir[512];
static int g_win_w = VIRT_W, g_win_h = VIRT_H;

/* ------------------------------------------------------------------ files */

static int dir_has(const char *dir, const char *file)
{
	char p[1024];
	snprintf(p, sizeof(p), "%s/%s", dir, file);
	struct stat st;
	return stat(p, &st) == 0;
}

#define MAX_DATA_DIRS 8
static char g_data_dirs[MAX_DATA_DIRS][512];
static int g_ndata;

static void add_data_dir(const char *d)
{
	if (g_ndata < MAX_DATA_DIRS && d && *d) {
		snprintf(g_data_dirs[g_ndata++], sizeof(g_data_dirs[0]), "%s", d);
	}
}

/* Data comes from two places: the original game folder (meshes, bitmaps, sounds)
 * and the files generated from it by re/scripts (track_*.bin, music .ogg).
 * ACESPEEDER_DATA may list several directories separated by ':'. */
static void find_dirs(void)
{
	const char *env = getenv("ACESPEEDER_DATA");
	if (env) {
		char tmp[2048];
		snprintf(tmp, sizeof(tmp), "%s", env);
		for (char *tok = strtok(tmp, ":"); tok; tok = strtok(NULL, ":")) {
			add_data_dir(tok);
		}
	}
	const char *defaults[] = {"data", "../data", "../game", "../../game", "game"};
	for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
		struct stat st;
		if (stat(defaults[i], &st) == 0 && S_ISDIR(st.st_mode)) {
			add_data_dir(defaults[i]);
		}
	}
	add_data_dir(".");
	snprintf(g_data_dir, sizeof(g_data_dir), "%s", g_data_dirs[0]);
	(void)dir_has;
	env = getenv("ACESPEEDER_SAVE");
	if (env) {
		snprintf(g_save_dir, sizeof(g_save_dir), "%s", env);
	}
	else {
		const char *home = getenv("HOME");
		snprintf(g_save_dir, sizeof(g_save_dir), "%s/.local/share/acespeeder", home ? home : ".");
	}
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s", g_save_dir);
	for (char *p = tmp + 1; *p; p++) { /* mkdir -p */
		if (*p == '/') {
			*p = 0;
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

/* Case-insensitive lookup: the game refers to e.g. "BlackJackMayol.mid" and "Mapping1.bmp". */
static FILE *open_in(const char *dir, const char *name, const char *mode)
{
	char p[1024];
	snprintf(p, sizeof(p), "%s/%s", dir, name);
	FILE *f = fopen(p, mode);
	if (f || mode[0] != 'r') {
		return f;
	}
	char lower[256];
	size_t i;
	for (i = 0; name[i] && i < sizeof(lower) - 1; i++) {
		lower[i] = (char)tolower((unsigned char)name[i]);
	}
	lower[i] = 0;
	snprintf(p, sizeof(p), "%s/%s", dir, lower);
	return fopen(p, mode);
}

FILE *plat_open_data(const char *name, const char *mode)
{
	for (int i = 0; i < g_ndata; i++) {
		FILE *f = open_in(g_data_dirs[i], name, mode);
		if (f) {
			return f;
		}
	}
	return NULL;
}

FILE *plat_open_save(const char *name, const char *mode)
{
	FILE *f = open_in(g_save_dir, name, mode);
	if (!f && mode[0] == 'r') {
		f = plat_open_data(name, mode); /* shipped defaults (replays, gamedata.dat) */
	}
	return f;
}

/* -------------------------------------------------------------- lifecycle */

static void audio_cb(void *ud, Uint8 *stream, int len)
{
	(void)ud;
	mixer_render((int16_t *)stream, len / 4);
}

void mixer_lock(void)
{
	if (g_audio) {
		SDL_LockAudioDevice(g_audio);
	}
}

void mixer_unlock(void)
{
	if (g_audio) {
		SDL_UnlockAudioDevice(g_audio);
	}
}

int plat_init(void)
{
	find_dirs();
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return -1;
	}
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	int scale = getenv("ACESPEEDER_SCALE") ? atoi(getenv("ACESPEEDER_SCALE")) : 1;
	if (scale < 1) {
		scale = 1;
	}
	g_win = SDL_CreateWindow("AceSpeeder!", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                         VIRT_W * scale, VIRT_H * scale, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!g_win) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return -1;
	}
	g_gl = SDL_GL_CreateContext(g_win);
	SDL_GL_SetSwapInterval(1);
	SDL_GL_GetDrawableSize(g_win, &g_win_w, &g_win_h);

	mixer_init();
	SDL_AudioSpec want = {0}, have;
	want.freq = MIXER_RATE;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = audio_cb;
	g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (g_audio) {
		SDL_PauseAudioDevice(g_audio, 0);
	}
	else {
		fprintf(stderr, "audio disabled: %s\n", SDL_GetError());
	}
	for (int i = 0; i < SDL_NumJoysticks(); i++) {
		if (SDL_IsGameController(i)) {
			g_pad = SDL_GameControllerOpen(i);
			break;
		}
	}
	glEnable(GL_NORMALIZE);
	glFrontFace(GL_CW);
	glCullFace(GL_BACK);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
	glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_FALSE);
	glAlphaFunc(GL_GREATER, 0.5f);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glFogi(GL_FOG_MODE, GL_LINEAR);
	return 0;
}

void plat_shutdown(void)
{
	if (g_audio) {
		SDL_CloseAudioDevice(g_audio);
		g_audio = 0;
	}
	if (g_pad) {
		SDL_GameControllerClose(g_pad);
	}
	SDL_GL_DeleteContext(g_gl);
	SDL_DestroyWindow(g_win);
	SDL_Quit();
}

uint32_t plat_ticks_ms(void)
{
	return SDL_GetTicks();
}

static void pump(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) {
			g_quit = 1;
		}
		else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
			SDL_GL_GetDrawableSize(g_win, &g_win_w, &g_win_h);
		}
		else if (e.type == SDL_CONTROLLERDEVICEADDED && !g_pad) {
			g_pad = SDL_GameControllerOpen(e.cdevice.which);
		}
	}
}

int plat_should_quit(void)
{
	pump();
	return g_quit;
}

/* ------------------------------------------------------------------ input */

void plat_read_input(int32_t keys[IN_COUNT])
{
	pump();
	const Uint8 *k = SDL_GetKeyboardState(NULL);
	/* Same keys as the original's DirectInput mapping. */
	if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_KP_8]) keys[IN_UP] = 1;
	if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_KP_2]) keys[IN_DOWN] = 1;
	if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_KP_6]) keys[IN_RIGHT] = 1;
	if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_KP_4]) keys[IN_LEFT] = 1;
	if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]) keys[IN_A] = 1;
	if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) keys[IN_B] = 1;
	if (k[SDL_SCANCODE_ESCAPE]) keys[IN_ESC] = 1;
	if (k[SDL_SCANCODE_F1]) keys[IN_F1] = 1;
	if (k[SDL_SCANCODE_F2]) keys[IN_F2] = 1;
	if (k[SDL_SCANCODE_F3]) keys[IN_F3] = 1;
	if (k[SDL_SCANCODE_F4]) keys[IN_F4] = 1;
	if (g_pad) {
		/* Original joystick: axis beyond +-50 of +-1000, even buttons = A, odd = B. */
		int ax = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX);
		int ay = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY);
		const int dz = 32767 / 20;
		if (ay < -dz || SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) keys[IN_UP] = 1;
		if (ay > dz || SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) keys[IN_DOWN] = 1;
		if (ax > dz || SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) keys[IN_RIGHT] = 1;
		if (ax < -dz || SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) keys[IN_LEFT] = 1;
		if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A)) keys[IN_A] = 1;
		if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_B)) keys[IN_B] = 1;
		if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_BACK)) keys[IN_ESC] = 1;
	}
}

/* --------------------------------------------------------------- graphics */

#define MAX_TEX 512
static GLuint g_tex[MAX_TEX];
static int g_tex_w[MAX_TEX], g_tex_h[MAX_TEX];

int gfx_screen_w(void) { return VIRT_W; }
int gfx_screen_h(void) { return VIRT_H; }
float gfx_pixel_aspect(void) { return 1.0f; } /* letterboxed to 4:3 */

/* The game renders in a 640x480 coordinate space; letterbox it into the window. */
static void virt_to_window(int x, int y, int w, int h, int *ox, int *oy, int *ow, int *oh)
{
	float s = (float)g_win_w / VIRT_W;
	if ((float)g_win_h / VIRT_H < s) {
		s = (float)g_win_h / VIRT_H;
	}
	int bx = (int)((g_win_w - VIRT_W * s) / 2), by = (int)((g_win_h - VIRT_H * s) / 2);
	*ox = bx + (int)(x * s);
	*oy = by + (int)((VIRT_H - y - h) * s); /* GL origin is bottom-left */
	*ow = (int)(w * s);
	*oh = (int)(h * s);
}

void gfx_begin_frame(void)
{
	glViewport(0, 0, g_win_w, g_win_h);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void plat_screenshot(const char *path)
{
	unsigned char *px = malloc((size_t)g_win_w * g_win_h * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, g_win_w, g_win_h, GL_RGB, GL_UNSIGNED_BYTE, px);
	FILE *f = fopen(path, "wb");
	if (f) {
		fprintf(f, "P6\n%d %d\n255\n", g_win_w, g_win_h);
		for (int y = g_win_h - 1; y >= 0; y--) {
			fwrite(px + (size_t)y * g_win_w * 3, 1, (size_t)g_win_w * 3, f);
		}
		fclose(f);
	}
	free(px);
}

void gfx_end_frame(void)
{
	SDL_GL_SwapWindow(g_win);
}

void gfx_set_viewport(int x, int y, int w, int h)
{
	int ox, oy, ow, oh;
	virt_to_window(x, y, w, h, &ox, &oy, &ow, &oh);
	glViewport(ox, oy, ow, oh);
	glScissor(ox, oy, ow, oh);
	glEnable(GL_SCISSOR_TEST);
}

void gfx_clear(uint32_t rgb, int color, int depth)
{
	glClearColor((rgb >> 16 & 0xff) / 255.0f, (rgb >> 8 & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, 1.0f);
	glDepthMask(GL_TRUE);
	glClear((color ? GL_COLOR_BUFFER_BIT : 0) | (depth ? GL_DEPTH_BUFFER_BIT : 0));
}

void gfx_set_projection(float fx, float fy, float n, float f)
{
	/* Left-handed view space (+z forward) straight to GL clip space. */
	float m[16] = {0};
	m[0] = 1.0f / fx;
	m[5] = 1.0f / fy;
	m[10] = (f + n) / (f - n);
	m[11] = 1.0f;
	m[14] = -2.0f * f * n / (f - n);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(m);
	glMatrixMode(GL_MODELVIEW);
}

void gfx_set_modelview(const float m[4][4])
{
	/* A row-major row-vector matrix has the same memory layout as GL's
	 * column-major column-vector matrix. */
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(&m[0][0]);
}

void gfx_set_lighting(int on)
{
	if (on) {
		glEnable(GL_LIGHTING);
		glEnable(GL_COLOR_MATERIAL);
	}
	else {
		glDisable(GL_LIGHTING);
		glDisable(GL_COLOR_MATERIAL);
	}
}

void gfx_set_lights(GfxColor ambient, const GfxLight *lights, int count)
{
	float amb[4] = {ambient.r, ambient.g, ambient.b, 1.0f};
	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	for (int i = 0; i < GFX_MAX_LIGHTS; i++) {
		GLenum L = GL_LIGHT0 + i;
		if (i >= count) {
			glDisable(L);
			continue;
		}
		const GfxLight *l = &lights[i];
		float zero[4] = {0, 0, 0, 1};
		float col[4] = {l->color.r, l->color.g, l->color.b, 1.0f};
		glLightfv(L, GL_AMBIENT, zero);
		glLightfv(L, GL_DIFFUSE, col);
		glLightfv(L, GL_SPECULAR, col);
		if (l->type == GFX_LIGHT_DIRECTIONAL) {
			float p[4] = {-l->dir[0], -l->dir[1], -l->dir[2], 0.0f};
			glLightfv(L, GL_POSITION, p);
		}
		else {
			float p[4] = {l->pos[0], l->pos[1], l->pos[2], 1.0f};
			glLightfv(L, GL_POSITION, p);
		}
		glLightf(L, GL_CONSTANT_ATTENUATION, l->att0 > 0 ? l->att0 : 1.0f);
		glLightf(L, GL_LINEAR_ATTENUATION, l->att1);
		glLightf(L, GL_QUADRATIC_ATTENUATION, l->att2);
		glEnable(L);
	}
	glPopMatrix();
}

void gfx_set_material(GfxColor diffuse, GfxColor emissive, GfxColor specular, float power)
{
	(void)diffuse; /* ambient/diffuse come from vertex colours (face colours) */
	float em[4] = {emissive.r, emissive.g, emissive.b, 1.0f};
	float sp[4] = {specular.r, specular.g, specular.b, 1.0f};
	glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, em);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, sp);
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, power > 128.0f ? 128.0f : power);
}

void gfx_set_fog(int on, uint32_t rgb, float start, float end)
{
	if (!on) {
		glDisable(GL_FOG);
		return;
	}
	/* rgb is D3DCOLOR order 0x00RRGGBB */
	float c[4] = {(rgb >> 16 & 0xff) / 255.0f, (rgb >> 8 & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, 1.0f};
	glFogfv(GL_FOG_COLOR, c);
	glFogf(GL_FOG_START, start);
	glFogf(GL_FOG_END, end);
	glEnable(GL_FOG);
}

void gfx_set_depth(int test, int write)
{
	if (test) {
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
	}
	else {
		glDisable(GL_DEPTH_TEST);
	}
	glDepthMask(write ? GL_TRUE : GL_FALSE);
}

void gfx_set_cull(int on)
{
	if (on) {
		glEnable(GL_CULL_FACE);
	}
	else {
		glDisable(GL_CULL_FACE);
	}
}

void gfx_set_blend(int on)
{
	if (on) {
		glEnable(GL_BLEND);
		if (on == GFX_BLEND_ADD) {
			glBlendFunc(GL_ONE, GL_ONE);
		}
		else {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
	}
	else {
		glDisable(GL_BLEND);
	}
}

void gfx_set_alpha_test(int on)
{
	if (on) {
		glEnable(GL_ALPHA_TEST);
	}
	else {
		glDisable(GL_ALPHA_TEST);
	}
}

int gfx_texture_create(int w, int h, const uint32_t *rgba, int filter_linear)
{
	int id = -1;
	for (int i = 0; i < MAX_TEX; i++) {
		if (!g_tex[i]) {
			id = i;
			break;
		}
	}
	if (id < 0) {
		return -1;
	}
	glGenTextures(1, &g_tex[id]);
	glBindTexture(GL_TEXTURE_2D, g_tex[id]);
	GLint f = filter_linear ? GL_LINEAR : GL_NEAREST;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	g_tex_w[id] = w;
	g_tex_h[id] = h;
	return id;
}

void gfx_texture_destroy(int tex)
{
	if (tex >= 0 && tex < MAX_TEX && g_tex[tex]) {
		glDeleteTextures(1, &g_tex[tex]);
		g_tex[tex] = 0;
	}
}

void gfx_texture_bind(int tex)
{
	if (tex >= 0 && tex < MAX_TEX && g_tex[tex]) {
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, g_tex[tex]);
	}
	else {
		glDisable(GL_TEXTURE_2D);
	}
}

static void set_pointers(const GfxVertex *v, int color, int normal)
{
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, sizeof(GfxVertex), &v->x);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, sizeof(GfxVertex), &v->u);
	if (color) {
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GfxVertex), &v->color);
	}
	else {
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(1, 1, 1, 1);
	}
	if (normal) {
		glEnableClientState(GL_NORMAL_ARRAY);
		glNormalPointer(GL_FLOAT, sizeof(GfxVertex), &v->nx);
	}
	else {
		glDisableClientState(GL_NORMAL_ARRAY);
	}
}

void gfx_draw_triangles(const GfxVertex *v, int count, int use_vertex_color)
{
	set_pointers(v, use_vertex_color, 1);
	glDrawArrays(GL_TRIANGLES, 0, count);
}

void gfx_draw_lines(const GfxVertex *v, int count)
{
	set_pointers(v, 1, 0);
	glDrawArrays(GL_LINES, 0, count);
}

void gfx_begin_2d(void)
{
	gfx_set_viewport(0, 0, VIRT_W, VIRT_H);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, VIRT_W, VIRT_H, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glDisable(GL_LIGHTING);
	glDisable(GL_COLOR_MATERIAL);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_FOG);
	glEnable(GL_ALPHA_TEST);
	glEnable(GL_BLEND);
}

void gfx_draw_quad_2d(int tex, float sx, float sy, float sw, float sh,
                      float dx, float dy, float dw, float dh, uint32_t color)
{
	if (tex < 0 || tex >= MAX_TEX || !g_tex[tex]) {
		return;
	}
	float tw = (float)g_tex_w[tex], th = (float)g_tex_h[tex];
	float u0 = sx / tw, v0 = sy / th, u1 = (sx + sw) / tw, v1 = (sy + sh) / th;
	GfxVertex q[6];
	float xs[4] = {dx, dx + dw, dx + dw, dx}, ys[4] = {dy, dy, dy + dh, dy + dh};
	float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
	int order[6] = {0, 1, 2, 0, 2, 3};
	for (int i = 0; i < 6; i++) {
		int k = order[i];
		q[i] = (GfxVertex){us[k], vs[k], color, 0, 0, 1, xs[k], ys[k], 0};
	}
	gfx_texture_bind(tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	set_pointers(q, 1, 0);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void gfx_fill_rect_2d(float x, float y, float w, float h, uint32_t color)
{
	GfxVertex q[6];
	float xs[4] = {x, x + w, x + w, x}, ys[4] = {y, y, y + h, y + h};
	int order[6] = {0, 1, 2, 0, 2, 3};
	for (int i = 0; i < 6; i++) {
		int k = order[i];
		q[i] = (GfxVertex){0, 0, color, 0, 0, 1, xs[k], ys[k], 0};
	}
	glDisable(GL_TEXTURE_2D);
	set_pointers(q, 1, 0);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

void gfx_end_2d(void)
{
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}
