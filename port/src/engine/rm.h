/* A small re-implementation of the parts of Direct3D Retained Mode (IDirect3DRM2)
 * that Ace Speeder uses. Function names mirror the COM methods so ported code
 * reads like the decompilation: Frame2::SetPosition -> rm_frame_set_position.
 *
 * Objects are reference counted like COM: create returns a reference, a parent
 * frame holds references to its children, visuals and lights. */
#ifndef RM_H
#define RM_H

#include <stdint.h>

#include "rmath.h"

typedef struct RMFrame RMFrame;
typedef struct RMMeshBuilder RMMeshBuilder;
typedef struct RMTexture RMTexture;
typedef struct RMMaterial RMMaterial;
typedef struct RMLight RMLight;
typedef struct RMViewport RMViewport;
typedef struct RMUserVisual RMUserVisual;
typedef struct RMFace RMFace;

/* D3DRMRENDERQUALITY bits */
#define RM_SHADE_FLAT 0x00
#define RM_SHADE_GOURAUD 0x01
#define RM_LIGHT_ON 0x08
#define RM_FILL_WIREFRAME 0x40
#define RM_FILL_SOLID 0x80

enum { RM_LIGHT_AMBIENT, RM_LIGHT_POINT, RM_LIGHT_SPOT, RM_LIGHT_DIRECTIONAL, RM_LIGHT_PARALLELPOINT };

/* D3DCOLOR-style packed colour: 0xAARRGGBB */
#define RM_RGBA(r, g, b, a) ((uint32_t)(a) << 24 | (uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))

void rm_init(void);
void rm_set_render_mode(uint32_t mode); /* Device2::SetRenderMode: 1 = blended transparency */

/* ---- generic ---- */
void rm_addref(void *obj);
void rm_release(void *obj); /* any RM object; NULL is ignored */
#define RM_RELEASE(p) do { rm_release(p); (p) = NULL; } while (0)

/* ---- frames ---- */
RMFrame *rm_frame_create(RMFrame *parent);
void rm_frame_add_child(RMFrame *f, RMFrame *child);
void rm_frame_delete_child(RMFrame *f, RMFrame *child);
void rm_frame_add_visual(RMFrame *f, void *visual); /* RMMeshBuilder or RMUserVisual */
void rm_frame_delete_visual(RMFrame *f, void *visual);
void rm_frame_add_light(RMFrame *f, RMLight *light);
void rm_frame_set_position(RMFrame *f, RMFrame *ref, float x, float y, float z);
void rm_frame_set_orientation(RMFrame *f, RMFrame *ref, float dx, float dy, float dz, float ux, float uy, float uz);
Vec3 rm_frame_get_position(RMFrame *f, RMFrame *ref);
void rm_frame_get_orientation(RMFrame *f, RMFrame *ref, Vec3 *dir, Vec3 *up);
void rm_frame_world(RMFrame *f, Mat4 *out); /* local -> world */
RMFrame *rm_frame_scene(RMFrame *f);
void rm_frame_set_scene_background_rgb(RMFrame *f, float r, float g, float b);
void rm_frame_set_scene_fog_enable(RMFrame *f, int on);
void rm_frame_set_scene_fog_color(RMFrame *f, uint32_t argb);
void rm_frame_set_scene_fog_params(RMFrame *f, float start, float end, float density);
void rm_frame_set_material_mode(RMFrame *f, int mode);

/* ---- textures / materials / lights ---- */
RMTexture *rm_texture_load(const char *name); /* cached by name, like RM2::LoadTexture */
void rm_texture_set_decal_transparency(RMTexture *t, int on);
void rm_texture_set_decal_transparent_color(RMTexture *t, uint32_t argb);

RMMaterial *rm_material_create(float power);
void rm_material_set_power(RMMaterial *m, float power);
void rm_material_set_specular(RMMaterial *m, float r, float g, float b);
void rm_material_set_emissive(RMMaterial *m, float r, float g, float b);

RMLight *rm_light_create_rgb(int type, float r, float g, float b);
void rm_light_set_color_rgb(RMLight *l, float r, float g, float b);

/* ---- mesh builders ---- */
RMMeshBuilder *rm_mb_create(void);
int rm_mb_load(RMMeshBuilder *mb, const char *name);
void rm_mb_scale(RMMeshBuilder *mb, float sx, float sy, float sz);
void rm_mb_translate(RMMeshBuilder *mb, float tx, float ty, float tz);
void rm_mb_set_quality(RMMeshBuilder *mb, uint32_t quality);
void rm_mb_set_perspective(RMMeshBuilder *mb, int on);
void rm_mb_set_material(RMMeshBuilder *mb, RMMaterial *mat);
void rm_mb_set_color_rgb(RMMeshBuilder *mb, float r, float g, float b);
void rm_mb_set_color(RMMeshBuilder *mb, uint32_t argb);
void rm_mb_set_texture(RMMeshBuilder *mb, RMTexture *tex);
RMTexture *rm_mb_get_texture(RMMeshBuilder *mb); /* texture of the first face */
int rm_mb_add_vertex(RMMeshBuilder *mb, float x, float y, float z);
int rm_mb_add_normal(RMMeshBuilder *mb, float x, float y, float z);
void rm_mb_set_texture_coordinates(RMMeshBuilder *mb, int index, float u, float v);
RMFace *rm_mb_create_face(RMMeshBuilder *mb);
int rm_mb_face_count(RMMeshBuilder *mb);
int rm_mb_vertex_count(RMMeshBuilder *mb);
void rm_mb_get_box(RMMeshBuilder *mb, Vec3 *min, Vec3 *max);

void rm_face_add_vertex_and_normal_indexed(RMFace *face, int vertex, int normal);
void rm_face_add_vertex_indexed(RMFace *face, int vertex);
void rm_face_set_color_rgb(RMFace *face, float r, float g, float b);
void rm_face_set_color(RMFace *face, uint32_t argb);
void rm_face_set_texture(RMFace *face, RMTexture *tex);
void rm_face_set_material(RMFace *face, RMMaterial *mat);

/* ---- user visual: callback invoked during render with the viewport ---- */
typedef void (*RMUserVisualCallback)(RMUserVisual *uv, void *arg, RMViewport *view);
RMUserVisual *rm_user_visual_create(RMUserVisualCallback cb, void *arg);

/* ---- viewport ---- */
RMViewport *rm_viewport_create(RMFrame *camera, int x, int y, int w, int h);
void rm_viewport_set_camera(RMViewport *v, RMFrame *camera);
void rm_viewport_set_front(RMViewport *v, float front);
void rm_viewport_set_back(RMViewport *v, float back);
void rm_viewport_set_field(RMViewport *v, float field);
void rm_viewport_clear(RMViewport *v);
void rm_viewport_render(RMViewport *v, RMFrame *frame);
/* Project a world point to viewport pixels; returns 0 if behind the camera. */
int rm_viewport_transform(RMViewport *v, Vec3 world, float *sx, float *sy);

#endif
