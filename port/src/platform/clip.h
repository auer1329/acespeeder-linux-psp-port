/* Software clipping of triangle lists against the near and far planes and a
 * guard band around the screen.
 *
 * The PSP's GE does not clip against the screen: a triangle with a vertex
 * projected outside its 4096x4096 drawing space, or behind the camera, is
 * dropped whole, so big polygons near the camera vanish at the screen edges.
 * The PSP backend clips such draws here before handing them to the GE, which
 * still does the transform and lighting. */
#ifndef CLIP_H
#define CLIP_H

#include "platform.h"

/* Guard band half-size in screen half-sizes (default 7, well inside the GE's
 * range; the Linux backend can use a smaller one to test the clipping). */
void clip_set_guard(float guard);
/* modelview in the platform's row-vector layout, projection as gfx_set_projection */
void clip_set_view(const float mv[4][4], float fx, float fy, float n, float f);
/* whether every vertex is inside, tested on the corners of the bounding box */
int clip_all_inside(const GfxVertex *v, int count);
int clip_box_inside(const float box[6]); /* the same test on known bounds */
/* Clips a triangle list; returns the vertex count of the clipped list (valid
 * until the next call) through *out, or -1 when out of memory. */
int clip_triangles(const GfxVertex *v, int count, const GfxVertex **out);

#endif
