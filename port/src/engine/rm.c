#include "rm.h"

#include <stdlib.h>
#include <string.h>

#include "../platform/platform.h"
#include "image.h"
#include "xfile.h"

enum { OBJ_FRAME = 1, OBJ_MESHBUILDER, OBJ_TEXTURE, OBJ_MATERIAL, OBJ_LIGHT, OBJ_VIEWPORT, OBJ_USERVISUAL };

typedef struct {
	int type;
	int refs;
} RMObject;

struct RMTexture {
	RMObject o;
	char name[64];
	Image img;
	int gfx;           /* backend texture, -1 until uploaded */
	int decal_transparency;
	uint32_t transparent_color;
	int has_alpha;
	RMTexture *next_cached;
};

struct RMMaterial {
	RMObject o;
	float power;
	float spec[3];
	float emis[3];
};

struct RMLight {
	RMObject o;
	int ltype;
	float color[3];
};

struct RMFace {
	int nv;
	int cap;
	int *vi;
	int *ni;
	uint32_t color; /* 0xAARRGGBB */
	RMTexture *tex;
	RMMaterial *mat;
	RMMeshBuilder *owner;
};

typedef struct {
	RMTexture *tex;
	RMMaterial *mat;
	int blended;
	GfxVertex *v;
	int n, cap;
} DrawGroup;

struct RMMeshBuilder {
	RMObject o;
	Vec3 *verts;
	float (*uv)[2];
	int nverts, capverts;
	Vec3 *normals;
	int nnormals, capnormals;
	RMFace **faces;
	int nfaces, capfaces;
	uint32_t quality;
	int perspective;
	/* compiled draw data */
	int dirty;
	DrawGroup *groups;
	int ngroups;
	GfxVertex *lines;
	int nlines;
};

struct RMUserVisual {
	RMObject o;
	RMUserVisualCallback cb;
	void *arg;
};

struct RMFrame {
	RMObject o;
	RMFrame *parent;
	RMFrame **children;
	int nchildren, capchildren;
	RMObject **visuals;
	int nvisuals, capvisuals;
	RMLight **lights;
	int nlights, caplights;
	Mat4 local;
	/* scene properties (meaningful on root frames) */
	float bg[3];
	int fog_on;
	uint32_t fog_color;
	float fog_start, fog_end, fog_density;
};

struct RMViewport {
	RMObject o;
	RMFrame *camera;
	int x, y, w, h;
	float front, back, field;
};

static uint32_t g_render_mode;
static RMTexture *g_tex_cache;

void rm_init(void)
{
	g_render_mode = 0;
}

void rm_set_render_mode(uint32_t mode)
{
	g_render_mode = mode;
}

/* ------------------------------------------------------------------ objects */

static void *obj_new(int type, size_t size)
{
	RMObject *o = calloc(1, size);
	o->type = type;
	o->refs = 1;
	return o;
}

void rm_addref(void *obj)
{
	if (obj) {
		((RMObject *)obj)->refs++;
	}
}

static void frame_free(RMFrame *f);
static void mb_free(RMMeshBuilder *mb);
static void texture_free(RMTexture *t);

void rm_release(void *obj)
{
	RMObject *o = obj;
	if (!o || --o->refs > 0) {
		return;
	}
	switch (o->type) {
	case OBJ_FRAME:
		frame_free((RMFrame *)o);
		break;
	case OBJ_MESHBUILDER:
		mb_free((RMMeshBuilder *)o);
		break;
	case OBJ_TEXTURE:
		texture_free((RMTexture *)o);
		break;
	default:
		free(o);
		break;
	}
}

#define GROW(arr, n, cap)                                          \
	do {                                                           \
		if ((n) == (cap)) {                                        \
			(cap) = (cap) ? (cap) * 2 : 8;                         \
			(arr) = realloc((arr), sizeof(*(arr)) * (size_t)(cap)); \
		}                                                          \
	} while (0)

/* ------------------------------------------------------------------- frames */

RMFrame *rm_frame_create(RMFrame *parent)
{
	RMFrame *f = obj_new(OBJ_FRAME, sizeof(RMFrame));
	m4_identity(&f->local);
	f->fog_end = 1.0f;
	f->fog_density = 1.0f;
	if (parent) {
		rm_frame_add_child(parent, f);
	}
	return f;
}

static void frame_free(RMFrame *f)
{
	for (int i = 0; i < f->nchildren; i++) {
		f->children[i]->parent = NULL;
		rm_release(f->children[i]);
	}
	for (int i = 0; i < f->nvisuals; i++) {
		rm_release(f->visuals[i]);
	}
	for (int i = 0; i < f->nlights; i++) {
		rm_release(f->lights[i]);
	}
	free(f->children);
	free(f->visuals);
	free(f->lights);
	free(f);
}

static void detach(RMFrame *child)
{
	RMFrame *p = child->parent;
	if (!p) {
		return;
	}
	for (int i = 0; i < p->nchildren; i++) {
		if (p->children[i] == child) {
			memmove(&p->children[i], &p->children[i + 1], sizeof(RMFrame *) * (p->nchildren - i - 1));
			p->nchildren--;
			break;
		}
	}
	child->parent = NULL;
}

void rm_frame_add_child(RMFrame *f, RMFrame *child)
{
	if (child->parent == f) {
		return;
	}
	rm_addref(child);
	if (child->parent) {
		RMFrame *old = child->parent;
		detach(child);
		(void)old;
		child->o.refs--; /* the old parent's reference moves to the new parent */
	}
	GROW(f->children, f->nchildren, f->capchildren);
	f->children[f->nchildren++] = child;
	child->parent = f;
}

void rm_frame_delete_child(RMFrame *f, RMFrame *child)
{
	if (child->parent != f) {
		return;
	}
	detach(child);
	rm_release(child);
}

void rm_frame_add_visual(RMFrame *f, void *visual)
{
	rm_addref(visual);
	GROW(f->visuals, f->nvisuals, f->capvisuals);
	f->visuals[f->nvisuals++] = visual;
}

void rm_frame_delete_visual(RMFrame *f, void *visual)
{
	for (int i = 0; i < f->nvisuals; i++) {
		if (f->visuals[i] == visual) {
			memmove(&f->visuals[i], &f->visuals[i + 1], sizeof(void *) * (f->nvisuals - i - 1));
			f->nvisuals--;
			rm_release(visual);
			return;
		}
	}
}

void rm_frame_add_light(RMFrame *f, RMLight *light)
{
	rm_addref(light);
	GROW(f->lights, f->nlights, f->caplights);
	f->lights[f->nlights++] = light;
}

void rm_frame_world(RMFrame *f, Mat4 *out)
{
	Mat4 m = f->local;
	for (RMFrame *p = f->parent; p; p = p->parent) {
		m4_mul(&m, &m, &p->local);
	}
	*out = m;
}

RMFrame *rm_frame_scene(RMFrame *f)
{
	while (f->parent) {
		f = f->parent;
	}
	return f;
}

void rm_frame_set_position(RMFrame *f, RMFrame *ref, float x, float y, float z)
{
	Vec3 p = v3(x, y, z);
	if (ref) {
		Mat4 w;
		rm_frame_world(ref, &w);
		p = m4_transform(&w, p);
	}
	if (f->parent) {
		Mat4 pw, inv;
		rm_frame_world(f->parent, &pw);
		m4_inverse(&inv, &pw);
		p = m4_transform(&inv, p);
	}
	f->local.m[3][0] = p.x;
	f->local.m[3][1] = p.y;
	f->local.m[3][2] = p.z;
}

void rm_frame_set_orientation(RMFrame *f, RMFrame *ref, float dx, float dy, float dz, float ux, float uy, float uz)
{
	Vec3 d = v3(dx, dy, dz), u = v3(ux, uy, uz);
	if (ref) {
		Mat4 w;
		rm_frame_world(ref, &w);
		d = m4_rotate(&w, d);
		u = m4_rotate(&w, u);
	}
	Vec3 zax = v3_norm(d);
	Vec3 xax = v3_norm(v3_cross(u, zax));
	Vec3 yax = v3_cross(zax, xax);
	Mat4 r;
	m4_identity(&r);
	r.m[0][0] = xax.x, r.m[0][1] = xax.y, r.m[0][2] = xax.z;
	r.m[1][0] = yax.x, r.m[1][1] = yax.y, r.m[1][2] = yax.z;
	r.m[2][0] = zax.x, r.m[2][1] = zax.y, r.m[2][2] = zax.z;
	if (f->parent) {
		Mat4 pw, inv;
		rm_frame_world(f->parent, &pw);
		pw.m[3][0] = pw.m[3][1] = pw.m[3][2] = 0.0f;
		m4_inverse(&inv, &pw);
		m4_mul(&r, &r, &inv);
	}
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			f->local.m[i][j] = r.m[i][j];
		}
	}
}

Vec3 rm_frame_get_position(RMFrame *f, RMFrame *ref)
{
	Mat4 w;
	rm_frame_world(f, &w);
	Vec3 p = v3(w.m[3][0], w.m[3][1], w.m[3][2]);
	if (ref) {
		Mat4 rw, inv;
		rm_frame_world(ref, &rw);
		m4_inverse(&inv, &rw);
		p = m4_transform(&inv, p);
	}
	return p;
}

void rm_frame_get_orientation(RMFrame *f, RMFrame *ref, Vec3 *dir, Vec3 *up)
{
	Mat4 w;
	rm_frame_world(f, &w);
	Vec3 d = v3(w.m[2][0], w.m[2][1], w.m[2][2]);
	Vec3 u = v3(w.m[1][0], w.m[1][1], w.m[1][2]);
	if (ref) {
		Mat4 rw, inv;
		rm_frame_world(ref, &rw);
		m4_inverse(&inv, &rw);
		d = m4_rotate(&inv, d);
		u = m4_rotate(&inv, u);
	}
	*dir = v3_norm(d);
	*up = v3_norm(u);
}

void rm_frame_set_scene_background_rgb(RMFrame *f, float r, float g, float b)
{
	f->bg[0] = r, f->bg[1] = g, f->bg[2] = b;
}

void rm_frame_set_scene_fog_enable(RMFrame *f, int on) { f->fog_on = on; }
void rm_frame_set_scene_fog_color(RMFrame *f, uint32_t argb) { f->fog_color = argb; }

void rm_frame_set_scene_fog_params(RMFrame *f, float start, float end, float density)
{
	f->fog_start = start;
	f->fog_end = end;
	f->fog_density = density;
}

void rm_frame_set_material_mode(RMFrame *f, int mode)
{
	(void)f;
	(void)mode; /* the game only uses the default (from mesh) */
}

/* ----------------------------------------------------- textures, materials */

RMTexture *rm_texture_load(const char *name)
{
	for (RMTexture *t = g_tex_cache; t; t = t->next_cached) {
		if (!strcmp(t->name, name)) {
			rm_addref(t);
			return t;
		}
	}
	RMTexture *t = obj_new(OBJ_TEXTURE, sizeof(RMTexture));
	snprintf(t->name, sizeof(t->name), "%s", name);
	t->gfx = -1;
	if (image_load_bmp(&t->img, name) != 0) {
		fprintf(stderr, "rm: cannot load texture %s\n", name);
		t->img.w = t->img.h = 1;
		t->img.px = malloc(4);
		t->img.px[0] = 0xffffffff;
	}
	/* Retained Mode puts the first stored row of the bottom-up DIB at v = 0, so
	 * mesh textures appear upside down compared with the picture (the game's
	 * bitmaps with text, such as pro.bmp and title.bmp, are stored flipped). */
	for (int y = 0; y < t->img.h / 2; y++) {
		uint32_t *a = t->img.px + (size_t)y * t->img.w, *b = t->img.px + (size_t)(t->img.h - 1 - y) * t->img.w;
		for (int x = 0; x < t->img.w; x++) {
			uint32_t c = a[x];
			a[x] = b[x];
			b[x] = c;
		}
	}
	t->next_cached = g_tex_cache;
	g_tex_cache = t;
	return t;
}

static void texture_free(RMTexture *t)
{
	for (RMTexture **pp = &g_tex_cache; *pp; pp = &(*pp)->next_cached) {
		if (*pp == t) {
			*pp = t->next_cached;
			break;
		}
	}
	if (t->gfx >= 0) {
		gfx_texture_destroy(t->gfx);
	}
	image_free(&t->img);
	free(t);
}

static void texture_invalidate(RMTexture *t)
{
	if (t->gfx >= 0) {
		gfx_texture_destroy(t->gfx);
		t->gfx = -1;
	}
}

void rm_texture_set_decal_transparency(RMTexture *t, int on)
{
	t->decal_transparency = on;
	texture_invalidate(t);
}

void rm_texture_set_decal_transparent_color(RMTexture *t, uint32_t argb)
{
	t->transparent_color = argb;
	texture_invalidate(t);
}

static int texture_gfx(RMTexture *t)
{
	if (t->gfx < 0) {
		Image img = t->img;
		uint32_t *copy = NULL;
		t->has_alpha = 0;
		if (t->decal_transparency) {
			copy = malloc(sizeof(uint32_t) * img.w * img.h);
			memcpy(copy, img.px, sizeof(uint32_t) * img.w * img.h);
			Image tmp = {img.w, img.h, copy};
			/* D3DCOLOR 0xAARRGGBB -> our 0xAABBGGRR */
			uint32_t c = t->transparent_color;
			uint32_t key = (c >> 16 & 0xff) | (c & 0xff00) | (c & 0xff) << 16;
			image_color_key(&tmp, key);
			img.px = copy;
			t->has_alpha = 1;
		}
		t->gfx = gfx_texture_create(img.w, img.h, img.px, 1);
		free(copy);
	}
	return t->gfx;
}

RMMaterial *rm_material_create(float power)
{
	RMMaterial *m = obj_new(OBJ_MATERIAL, sizeof(RMMaterial));
	m->power = power;
	return m;
}

void rm_material_set_power(RMMaterial *m, float power) { m->power = power; }

void rm_material_set_specular(RMMaterial *m, float r, float g, float b)
{
	m->spec[0] = r, m->spec[1] = g, m->spec[2] = b;
}

void rm_material_set_emissive(RMMaterial *m, float r, float g, float b)
{
	m->emis[0] = r, m->emis[1] = g, m->emis[2] = b;
}

RMLight *rm_light_create_rgb(int type, float r, float g, float b)
{
	RMLight *l = obj_new(OBJ_LIGHT, sizeof(RMLight));
	l->ltype = type;
	rm_light_set_color_rgb(l, r, g, b);
	return l;
}

void rm_light_set_color_rgb(RMLight *l, float r, float g, float b)
{
	l->color[0] = r, l->color[1] = g, l->color[2] = b;
}

/* ------------------------------------------------------------ mesh builders */

static uint32_t argb_from_rgb(float r, float g, float b, float a)
{
#define C8(x) ((uint32_t)((x) <= 0.0f ? 0 : (x) >= 1.0f ? 255 : (int)((x) * 255.0f + 0.5f)))
	return C8(a) << 24 | C8(r) << 16 | C8(g) << 8 | C8(b);
#undef C8
}

RMMeshBuilder *rm_mb_create(void)
{
	RMMeshBuilder *mb = obj_new(OBJ_MESHBUILDER, sizeof(RMMeshBuilder));
	mb->quality = RM_SHADE_GOURAUD | RM_LIGHT_ON | RM_FILL_SOLID;
	mb->dirty = 1;
	return mb;
}

static void mb_clear_compiled(RMMeshBuilder *mb)
{
	for (int i = 0; i < mb->ngroups; i++) {
		free(mb->groups[i].v);
	}
	free(mb->groups);
	free(mb->lines);
	mb->groups = NULL;
	mb->ngroups = 0;
	mb->lines = NULL;
	mb->nlines = 0;
}

static void face_free(RMFace *f)
{
	rm_release(f->tex);
	rm_release(f->mat);
	free(f->vi);
	free(f->ni);
	free(f);
}

static void mb_free(RMMeshBuilder *mb)
{
	mb_clear_compiled(mb);
	for (int i = 0; i < mb->nfaces; i++) {
		face_free(mb->faces[i]);
	}
	free(mb->faces);
	free(mb->verts);
	free(mb->uv);
	free(mb->normals);
	free(mb);
}

int rm_mb_add_vertex(RMMeshBuilder *mb, float x, float y, float z)
{
	if (mb->nverts == mb->capverts) {
		mb->capverts = mb->capverts ? mb->capverts * 2 : 64;
		mb->verts = realloc(mb->verts, sizeof(Vec3) * mb->capverts);
		mb->uv = realloc(mb->uv, sizeof(float[2]) * mb->capverts);
	}
	mb->verts[mb->nverts] = v3(x, y, z);
	mb->uv[mb->nverts][0] = mb->uv[mb->nverts][1] = 0.0f;
	mb->dirty = 1;
	return mb->nverts++;
}

int rm_mb_add_normal(RMMeshBuilder *mb, float x, float y, float z)
{
	GROW(mb->normals, mb->nnormals, mb->capnormals);
	mb->normals[mb->nnormals] = v3_norm(v3(x, y, z));
	mb->dirty = 1;
	return mb->nnormals++;
}

void rm_mb_set_texture_coordinates(RMMeshBuilder *mb, int index, float u, float v)
{
	if (index >= 0 && index < mb->nverts) {
		mb->uv[index][0] = u;
		mb->uv[index][1] = v;
		mb->dirty = 1;
	}
}

RMFace *rm_mb_create_face(RMMeshBuilder *mb)
{
	RMFace *f = calloc(1, sizeof(RMFace));
	f->color = 0xffffffff;
	f->owner = mb;
	GROW(mb->faces, mb->nfaces, mb->capfaces);
	mb->faces[mb->nfaces++] = f;
	mb->dirty = 1;
	return f;
}

void rm_face_add_vertex_and_normal_indexed(RMFace *face, int vertex, int normal)
{
	if (face->nv == face->cap) {
		face->cap = face->cap ? face->cap * 2 : 4;
		face->vi = realloc(face->vi, sizeof(int) * face->cap);
		face->ni = realloc(face->ni, sizeof(int) * face->cap);
	}
	face->vi[face->nv] = vertex;
	face->ni[face->nv] = normal;
	face->nv++;
	face->owner->dirty = 1;
}

void rm_face_add_vertex_indexed(RMFace *face, int vertex)
{
	rm_face_add_vertex_and_normal_indexed(face, vertex, -1);
}

void rm_face_set_color(RMFace *face, uint32_t argb)
{
	face->color = argb;
	face->owner->dirty = 1;
}

void rm_face_set_color_rgb(RMFace *face, float r, float g, float b)
{
	rm_face_set_color(face, argb_from_rgb(r, g, b, 1.0f));
}

void rm_face_set_texture(RMFace *face, RMTexture *tex)
{
	rm_addref(tex);
	rm_release(face->tex);
	face->tex = tex;
	face->owner->dirty = 1;
}

void rm_face_set_material(RMFace *face, RMMaterial *mat)
{
	rm_addref(mat);
	rm_release(face->mat);
	face->mat = mat;
	face->owner->dirty = 1;
}

int rm_mb_load(RMMeshBuilder *mb, const char *name)
{
	XMesh x;
	if (xmesh_load(&x, name) != 0) {
		fprintf(stderr, "rm: cannot load mesh %s\n", name);
		return -1;
	}
	int vbase = mb->nverts, nbase = mb->nnormals;
	for (int i = 0; i < x.nverts; i++) {
		int k = rm_mb_add_vertex(mb, x.verts[i].x, x.verts[i].y, x.verts[i].z);
		if (x.uv) {
			rm_mb_set_texture_coordinates(mb, k, x.uv[i][0], x.uv[i][1]);
		}
	}
	for (int i = 0; i < x.nnormals; i++) {
		rm_mb_add_normal(mb, x.normals[i].x, x.normals[i].y, x.normals[i].z);
	}
	RMTexture **tex = calloc(x.nmaterials + 1, sizeof(RMTexture *));
	RMMaterial **mat = calloc(x.nmaterials + 1, sizeof(RMMaterial *));
	for (int i = 0; i < x.nmaterials; i++) {
		XMaterial *xm = &x.materials[i];
		if (xm->texture[0]) {
			tex[i] = rm_texture_load(xm->texture);
		}
		mat[i] = rm_material_create(xm->power);
		rm_material_set_specular(mat[i], xm->spec[0], xm->spec[1], xm->spec[2]);
		rm_material_set_emissive(mat[i], xm->emis[0], xm->emis[1], xm->emis[2]);
	}
	for (int f = 0; f < x.nfaces; f++) {
		RMFace *face = rm_mb_create_face(mb);
		for (int i = 0; i < x.face_count[f]; i++) {
			int k = x.face_start[f] + i;
			rm_face_add_vertex_and_normal_indexed(face, vbase + x.idx[k], x.nidx ? nbase + x.nidx[k] : -1);
		}
		int m = x.face_material ? x.face_material[f] : -1;
		if (m >= 0 && m < x.nmaterials) {
			XMaterial *xm = &x.materials[m];
			face->color = argb_from_rgb(xm->r, xm->g, xm->b, xm->a);
			rm_face_set_texture(face, tex[m]);
			rm_face_set_material(face, mat[m]);
		}
	}
	for (int i = 0; i < x.nmaterials; i++) {
		rm_release(tex[i]);
		rm_release(mat[i]);
	}
	free(tex);
	free(mat);
	xmesh_free(&x);
	mb->dirty = 1;
	return 0;
}

void rm_mb_scale(RMMeshBuilder *mb, float sx, float sy, float sz)
{
	for (int i = 0; i < mb->nverts; i++) {
		mb->verts[i].x *= sx;
		mb->verts[i].y *= sy;
		mb->verts[i].z *= sz;
	}
	mb->dirty = 1;
}

void rm_mb_translate(RMMeshBuilder *mb, float tx, float ty, float tz)
{
	for (int i = 0; i < mb->nverts; i++) {
		mb->verts[i] = v3_add(mb->verts[i], v3(tx, ty, tz));
	}
	mb->dirty = 1;
}

void rm_mb_set_quality(RMMeshBuilder *mb, uint32_t quality)
{
	mb->quality = quality;
	mb->dirty = 1;
}

void rm_mb_set_perspective(RMMeshBuilder *mb, int on) { mb->perspective = on; }

void rm_mb_set_material(RMMeshBuilder *mb, RMMaterial *mat)
{
	for (int i = 0; i < mb->nfaces; i++) {
		rm_face_set_material(mb->faces[i], mat);
	}
}

void rm_mb_set_color(RMMeshBuilder *mb, uint32_t argb)
{
	for (int i = 0; i < mb->nfaces; i++) {
		mb->faces[i]->color = argb;
	}
	mb->dirty = 1;
}

void rm_mb_set_color_rgb(RMMeshBuilder *mb, float r, float g, float b)
{
	rm_mb_set_color(mb, argb_from_rgb(r, g, b, 1.0f));
}

void rm_mb_set_texture(RMMeshBuilder *mb, RMTexture *tex)
{
	for (int i = 0; i < mb->nfaces; i++) {
		rm_face_set_texture(mb->faces[i], tex);
	}
}

RMTexture *rm_mb_get_texture(RMMeshBuilder *mb)
{
	return mb->nfaces ? mb->faces[0]->tex : NULL;
}

int rm_mb_face_count(RMMeshBuilder *mb) { return mb->nfaces; }
int rm_mb_vertex_count(RMMeshBuilder *mb) { return mb->nverts; }

void rm_mb_get_box(RMMeshBuilder *mb, Vec3 *min, Vec3 *max)
{
	Vec3 lo = v3(0, 0, 0), hi = v3(0, 0, 0);
	for (int i = 0; i < mb->nverts; i++) {
		Vec3 p = mb->verts[i];
		if (i == 0) {
			lo = hi = p;
		}
		lo = v3(fminf(lo.x, p.x), fminf(lo.y, p.y), fminf(lo.z, p.z));
		hi = v3(fmaxf(hi.x, p.x), fmaxf(hi.y, p.y), fmaxf(hi.z, p.z));
	}
	*min = lo;
	*max = hi;
}

static DrawGroup *group_for(RMMeshBuilder *mb, RMTexture *tex, RMMaterial *mat, int blended)
{
	for (int i = 0; i < mb->ngroups; i++) {
		DrawGroup *g = &mb->groups[i];
		if (g->tex == tex && g->mat == mat && g->blended == blended) {
			return g;
		}
	}
	mb->groups = realloc(mb->groups, sizeof(DrawGroup) * (mb->ngroups + 1));
	DrawGroup *g = &mb->groups[mb->ngroups++];
	memset(g, 0, sizeof(*g));
	g->tex = tex;
	g->mat = mat;
	g->blended = blended;
	return g;
}

static void group_push(DrawGroup *g, const GfxVertex *v)
{
	GROW(g->v, g->n, g->cap);
	g->v[g->n++] = *v;
}

static GfxVertex make_vertex(RMMeshBuilder *mb, int vi, Vec3 n, uint32_t abgr)
{
	GfxVertex out;
	Vec3 p = mb->verts[vi];
	out.u = mb->uv[vi][0];
	out.v = mb->uv[vi][1];
	out.color = abgr;
	out.nx = n.x, out.ny = n.y, out.nz = n.z;
	out.x = p.x, out.y = p.y, out.z = p.z;
	return out;
}

static void mb_compile(RMMeshBuilder *mb)
{
	mb_clear_compiled(mb);
	mb->dirty = 0;
	int flat = (mb->quality & 7) == RM_SHADE_FLAT;
	int wire = (mb->quality & 0x1c0) == RM_FILL_WIREFRAME;
	int nlinecap = 0;
	for (int fi = 0; fi < mb->nfaces; fi++) {
		RMFace *f = mb->faces[fi];
		if (f->nv < 3) {
			continue;
		}
		uint32_t c = f->color;
		uint32_t abgr = (c & 0xff000000) | (c >> 16 & 0xff) | (c & 0xff00) | (c & 0xff) << 16;
		int blended = (c >> 24) < 255 && (g_render_mode & 1);
		/* Face normal from the first non-degenerate corner (left-handed, clockwise front). */
		Vec3 fn = v3(0, 1, 0);
		for (int i = 1; i + 1 < f->nv; i++) {
			Vec3 a = mb->verts[f->vi[0]], b = mb->verts[f->vi[i]], d = mb->verts[f->vi[i + 1]];
			Vec3 cr = v3_cross(v3_sub(b, a), v3_sub(d, a));
			if (v3_len(cr) > 1e-12f) {
				fn = v3_norm(cr);
				break;
			}
		}
		if (wire) {
			for (int i = 0; i < f->nv; i++) {
				if (mb->nlines + 2 > nlinecap) {
					nlinecap = nlinecap ? nlinecap * 2 : 64;
					mb->lines = realloc(mb->lines, sizeof(GfxVertex) * nlinecap);
				}
				mb->lines[mb->nlines++] = make_vertex(mb, f->vi[i], fn, abgr);
				mb->lines[mb->nlines++] = make_vertex(mb, f->vi[(i + 1) % f->nv], fn, abgr);
			}
			continue;
		}
		DrawGroup *g = group_for(mb, f->tex, f->mat, blended);
		for (int i = 1; i + 1 < f->nv; i++) {
			int corner[3] = {0, i, i + 1};
			for (int k = 0; k < 3; k++) {
				int idx = corner[k];
				Vec3 n = fn;
				if (!flat && f->ni[idx] >= 0 && f->ni[idx] < mb->nnormals) {
					n = mb->normals[f->ni[idx]];
				}
				GfxVertex v = make_vertex(mb, f->vi[idx], n, abgr);
				group_push(g, &v);
			}
		}
	}
}

/* ------------------------------------------------------------- user visual */

RMUserVisual *rm_user_visual_create(RMUserVisualCallback cb, void *arg)
{
	RMUserVisual *uv = obj_new(OBJ_USERVISUAL, sizeof(RMUserVisual));
	uv->cb = cb;
	uv->arg = arg;
	return uv;
}

/* ---------------------------------------------------------------- viewport */

RMViewport *rm_viewport_create(RMFrame *camera, int x, int y, int w, int h)
{
	RMViewport *v = obj_new(OBJ_VIEWPORT, sizeof(RMViewport));
	v->camera = camera;
	v->x = x, v->y = y, v->w = w, v->h = h;
	v->front = 1.0f;
	v->back = 100.0f;
	v->field = 0.5f;
	return v;
}

void rm_viewport_set_camera(RMViewport *v, RMFrame *camera) { v->camera = camera; }

void rm_viewport_set_front(RMViewport *v, float front)
{
	if (front > 0.0f) { /* RM rejects non-positive values (the game passes 0) */
		v->front = front;
	}
}

void rm_viewport_set_back(RMViewport *v, float back)
{
	if (back > v->front) {
		v->back = back;
	}
}

void rm_viewport_set_field(RMViewport *v, float field)
{
	if (field > 0.0f) {
		v->field = field;
	}
}

/* The original renders 640x480 with uniform scaling, so `field` is the horizontal
 * half-extent at distance 1 and the vertical one is field*3/4. We keep that vertical
 * extent and widen horizontally to fill wider screens (PSP 480x272). */
static void viewport_fields(RMViewport *v, float *fx, float *fy)
{
	*fy = v->field * 0.75f;
	*fx = *fy * (float)v->w / (float)v->h * gfx_pixel_aspect();
}

void rm_viewport_clear(RMViewport *v)
{
	gfx_set_viewport(v->x, v->y, v->w, v->h);
	uint32_t rgb = 0;
	if (v->camera) {
		RMFrame *scene = rm_frame_scene(v->camera);
		rgb = argb_from_rgb(scene->bg[0], scene->bg[1], scene->bg[2], 0.0f) & 0xffffff;
	}
	gfx_clear(rgb, 1, 1);
}

typedef struct {
	RMViewport *view;
	Mat4 view_inv;
	int lit_ready;
} RenderCtx;

static void collect_lights(RMFrame *f, const Mat4 *view_inv, GfxColor *ambient, GfxLight *lights, int *n)
{
	Mat4 w;
	for (int i = 0; i < f->nlights; i++) {
		RMLight *l = f->lights[i];
		if (l->ltype == RM_LIGHT_AMBIENT) {
			ambient->r += l->color[0];
			ambient->g += l->color[1];
			ambient->b += l->color[2];
			continue;
		}
		if (*n >= GFX_MAX_LIGHTS) {
			continue;
		}
		rm_frame_world(f, &w);
		Mat4 mv;
		m4_mul(&mv, &w, view_inv);
		GfxLight *g = &lights[(*n)++];
		memset(g, 0, sizeof(*g));
		g->type = l->ltype == RM_LIGHT_DIRECTIONAL ? GFX_LIGHT_DIRECTIONAL : GFX_LIGHT_POINT;
		g->color = (GfxColor){l->color[0], l->color[1], l->color[2], 1.0f};
		g->pos[0] = mv.m[3][0], g->pos[1] = mv.m[3][1], g->pos[2] = mv.m[3][2];
		Vec3 d = v3_norm(v3(mv.m[2][0], mv.m[2][1], mv.m[2][2]));
		g->dir[0] = d.x, g->dir[1] = d.y, g->dir[2] = d.z;
		g->att0 = 1.0f;
	}
	for (int i = 0; i < f->nchildren; i++) {
		collect_lights(f->children[i], view_inv, ambient, lights, n);
	}
}

static void draw_mb(RMMeshBuilder *mb, const Mat4 *mv, int pass_blended)
{
	if (mb->dirty) {
		mb_compile(mb);
	}
	gfx_set_modelview(mv->m);
	int lit = (mb->quality & RM_LIGHT_ON) != 0;
	if (mb->nlines) {
		if (!pass_blended) {
			gfx_set_lighting(0);
			gfx_texture_bind(-1);
			gfx_draw_lines(mb->lines, mb->nlines);
		}
		return;
	}
	for (int i = 0; i < mb->ngroups; i++) {
		DrawGroup *g = &mb->groups[i];
		if (g->blended != pass_blended || !g->n) {
			continue;
		}
		gfx_set_lighting(lit);
		if (g->mat) {
			GfxColor zero = {0, 0, 0, 1};
			GfxColor em = {g->mat->emis[0], g->mat->emis[1], g->mat->emis[2], 1};
			GfxColor sp = {g->mat->spec[0], g->mat->spec[1], g->mat->spec[2], 1};
			gfx_set_material(zero, em, sp, g->mat->power);
		}
		else {
			GfxColor zero = {0, 0, 0, 1};
			gfx_set_material(zero, zero, zero, 0.0f);
		}
		int alpha_test = 0;
		if (g->tex) {
			gfx_texture_bind(texture_gfx(g->tex));
			alpha_test = g->tex->has_alpha;
		}
		else {
			gfx_texture_bind(-1);
		}
		gfx_set_alpha_test(alpha_test);
		gfx_draw_triangles(g->v, g->n, 1);
	}
	gfx_set_alpha_test(0);
}

static void render_frame(RenderCtx *ctx, RMFrame *f, const Mat4 *parent_world, int pass_blended)
{
	Mat4 world, mv;
	m4_mul(&world, &f->local, parent_world);
	m4_mul(&mv, &world, &ctx->view_inv);
	for (int i = 0; i < f->nvisuals; i++) {
		RMObject *o = f->visuals[i];
		if (o->type == OBJ_MESHBUILDER) {
			draw_mb((RMMeshBuilder *)o, &mv, pass_blended);
		}
		else if (o->type == OBJ_USERVISUAL && !pass_blended) {
			RMUserVisual *uv = (RMUserVisual *)o;
			gfx_set_modelview(mv.m);
			uv->cb(uv, uv->arg, ctx->view);
		}
	}
	for (int i = 0; i < f->nchildren; i++) {
		render_frame(ctx, f->children[i], &world, pass_blended);
	}
}

void rm_viewport_render(RMViewport *v, RMFrame *frame)
{
	if (!v->camera || !frame) {
		return;
	}
	RenderCtx ctx;
	ctx.view = v;
	Mat4 cam;
	rm_frame_world(v->camera, &cam);
	m4_inverse_rigid(&ctx.view_inv, &cam);

	gfx_set_viewport(v->x, v->y, v->w, v->h);
	float fx, fy;
	viewport_fields(v, &fx, &fy);
	gfx_set_projection(fx, fy, v->front, v->back);

	RMFrame *scene = rm_frame_scene(frame);
	GfxColor ambient = {0, 0, 0, 1};
	GfxLight lights[GFX_MAX_LIGHTS];
	int nlights = 0;
	collect_lights(scene, &ctx.view_inv, &ambient, lights, &nlights);
	gfx_set_lights(ambient, lights, nlights);

	/* Scene properties come from the frame being rendered when it has them: the
	 * game sets fog on individual track segment frames and renders each one
	 * separately, while the sky and machines are drawn without fog. */
	RMFrame *fogsrc = frame->fog_on ? frame : scene;
	gfx_set_fog(fogsrc->fog_on, fogsrc->fog_color & 0xffffff, fogsrc->fog_start, fogsrc->fog_end);
	gfx_set_depth(1, 1);
	gfx_set_cull(1);
	gfx_set_blend(0);

	Mat4 parent;
	if (frame->parent) {
		rm_frame_world(frame->parent, &parent);
	}
	else {
		m4_identity(&parent);
	}
	render_frame(&ctx, frame, &parent, 0);
	if (g_render_mode & 1) {
		gfx_set_blend(1);
		gfx_set_depth(1, 0);
		render_frame(&ctx, frame, &parent, 1);
		gfx_set_blend(0);
		gfx_set_depth(1, 1);
	}
	gfx_set_fog(0, 0, 0, 0);
}

int rm_viewport_transform(RMViewport *v, Vec3 world, float *sx, float *sy)
{
	Mat4 cam, inv;
	rm_frame_world(v->camera, &cam);
	m4_inverse_rigid(&inv, &cam);
	Vec3 p = m4_transform(&inv, world);
	if (p.z <= v->front) {
		return 0;
	}
	float fx, fy;
	viewport_fields(v, &fx, &fy);
	*sx = v->x + v->w * 0.5f * (1.0f + p.x / (p.z * fx));
	*sy = v->y + v->h * 0.5f * (1.0f - p.y / (p.z * fy));
	return 1;
}
