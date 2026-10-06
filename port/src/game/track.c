#include "track.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/platform.h"

_Static_assert(sizeof(TrackPos) == 0x24, "TrackPos layout");
_Static_assert(offsetof(Track, ax) == 0x3c4, "Track layout");
_Static_assert(offsetof(Track, ptype) == 0x12c4, "Track layout");
_Static_assert(offsetof(Track, seglen) == 0x14e0, "Track layout");
_Static_assert(offsetof(Track, f15d0) == 0x15d0, "Track layout");
_Static_assert(offsetof(Track, radius) == 0x1740, "Track layout");
_Static_assert(offsetof(Track, laps) == 0x1a14, "Track layout");
_Static_assert(offsetof(Track, reftime) == 0x1a18, "Track layout");
_Static_assert(sizeof(Track) == 0x1a28, "Track layout");
_Static_assert(offsetof(Pickups, type) == 0x144, "Pickups layout");
_Static_assert(offsetof(Pickups, dur) == 0x164, "Pickups layout");
_Static_assert(offsetof(Pickups, from) == 0x1a4, "Pickups layout");
_Static_assert(offsetof(Pickups, to) == 0x3e4, "Pickups layout");

Track g_track;

/* Segment frame of the track at p: forward from the Bezier tangent, right kept
 * horizontal, then rolled by the interpolated bank angle. */
static Mat3 segment_frame(const Track *trk, const TrackPos *p)
{
	int i = p->seg;
	float fx = (trk->px[i + 1] - trk->cx[i]) * p->t;
	float gx = (trk->px[i] - trk->cx[i]) * p->omt;
	fx = (fx + fx) - (gx + gx);
	float fy = (trk->py[i + 1] - trk->cy[i]) * p->t;
	float gy = (trk->py[i] - trk->cy[i]) * p->omt;
	fy = (fy + fy) - (gy + gy);
	float fz = (trk->pz[i + 1] - trk->cz[i]) * p->t;
	float gz = (trk->pz[i] - trk->cz[i]) * p->omt;
	fz = (fz + fz) - (gz + gz);
	Mat3 f = Mat3_Identity();
	float len = sqrtf(fz * fz + fy * fy + fx * fx); /* summation order of the original */
	float dx = 0, dy = 0, dz = 0;
	if (len != 0.0f) {
		dx = fx / len, dy = fy / len, dz = fz / len;
	}
	float h = sqrtf(dx * dx + dz * dz);
	f.m[0] = dz / h;
	f.m[1] = 0.0f;
	f.m[2] = -(dx / h);
	f.m[3] = f.m[2] * dy;
	f.m[4] = f.m[0] * dz - f.m[2] * dx;
	f.m[5] = -(f.m[0] * dy);
	f.m[6] = dx, f.m[7] = dy, f.m[8] = dz;
	float bank = (trk->bank[i] - trk->cbank[i]) * p->omt2 + (trk->bank[i + 1] - trk->cbank[i]) * p->t2 +
	             trk->cbank[i];
	Mat3 roll = Mat3_Identity();
	float c = (float)cos((double)bank), s = (float)sin((double)bank);
	roll.m[0] = c, roll.m[1] = s, roll.m[3] = -s, roll.m[4] = c;
	return Mat3_Mul(roll, f);
}

Vec3 Track_Point(const Track *trk, TrackPos p)
{
	TrackPos_Update(&p);
	int i = p.seg;
	Mat3 r = segment_frame(trk, &p);
	int k = p.slot + p.seg * 8;
	float lx = (p.omt * trk->ax[k] + p.t * trk->bx[k]) * p.omu + (p.omt * trk->ax[k + 1] + p.t * trk->bx[k + 1]) * p.u;
	float ly = (p.omt * trk->ay[k] + p.t * trk->by[k]) * p.omu + (p.omt * trk->ay[k + 1] + p.t * trk->by[k + 1]) * p.u +
	           p.h;
	/* base point on the Bezier first, then the cross-section offset (operation order
	 * of the original keeps results bit-identical) */
	float bx_ = ((trk->px[i] - trk->cx[i]) * p.omt2 + (trk->px[i + 1] - trk->cx[i]) * p.t2) + trk->cx[i];
	float by_ = ((trk->py[i] - trk->cy[i]) * p.omt2 + (trk->py[i + 1] - trk->cy[i]) * p.t2) + trk->cy[i];
	float bz_ = ((trk->pz[i] - trk->cz[i]) * p.omt2 + (trk->pz[i + 1] - trk->cz[i]) * p.t2) + trk->cz[i];
	Vec3 out;
	out.x = (ly * r.m[3] + lx * r.m[0]) + bx_;
	out.y = (ly * r.m[4] + lx * r.m[1]) + by_;
	out.z = (ly * r.m[5] + lx * r.m[2]) + bz_;
	return out;
}

Mat3 Track_Frame(const Track *trk, TrackPos p)
{
	TrackPos_Update(&p);
	return segment_frame(trk, &p);
}

Mat3 Track_FlatFrame(const Track *trk, TrackPos p)
{
	TrackPos_Update(&p);
	int i = p.seg;
	float fx = (trk->px[i + 1] - trk->cx[i]) * p.t;
	float gx = (trk->px[i] - trk->cx[i]) * p.omt;
	fx = (fx + fx) - (gx + gx);
	float fz = (trk->pz[i + 1] - trk->cz[i]) * p.t;
	float gz = (trk->pz[i] - trk->cz[i]) * p.omt;
	fz = (fz + fz) - (gz + gz);
	float len = sqrtf(fz * fz + fx * fx);
	Mat3 m = Mat3_Identity();
	if (len != 0.0f) {
		float dx = fx / len, dz = fz / len;
		m.m[6] = dx, m.m[7] = 0.0f, m.m[1] = 0.0f;
		m.m[8] = dz, m.m[0] = dz;
		m.m[2] = -dx;
	}
	return m;
}

/* ------------------------------------------------------------- data files */
#ifndef GAME_TESTLIB

static int rd(FILE *f, void *p, size_t n)
{
	return fread(p, 1, n, f) == n ? 0 : -1;
}

int Track_Load(int stage, int course, TrackMeshes *out)
{
	char name[64];
	snprintf(name, sizeof(name), "track_%d_%d.bin", stage, course);
	FILE *f = plat_open_data(name, "rb");
	if (!f) {
		fprintf(stderr, "track: missing %s (run re/scripts/extract_tracks.py)\n", name);
		return -1;
	}
	char magic[4];
	uint32_t version, nseg, len;
	int rc = -1;
	memset(out, 0, sizeof(*out));
	if (rd(f, magic, 4) || memcmp(magic, "ASTR", 4) || rd(f, &version, 4) || version != 2 || rd(f, &nseg, 4) ||
	    nseg > TRACK_MAX_POINTS) {
		goto done;
	}
	if (rd(f, &len, 4) || len != sizeof(Track) || rd(f, &g_track, sizeof(Track))) {
		goto done;
	}
	memset(&g_pickups, 0, sizeof(g_pickups));
	if (rd(f, &len, 4) || len > sizeof(Pickups) || rd(f, &g_pickups, len)) {
		goto done;
	}
	out->nseg = (int)nseg;
	snprintf(name, sizeof(name), "course%d.bmp", g_track.env + 1); /* as Track_BuildMeshes(1) */
	out->tex = rm_texture_load(name);
	for (uint32_t s = 0; s < nseg; s++) {
		TrackSegment *seg = &out->seg[s];
		uint32_t nfaces;
		float pos[3];
		if (rd(f, pos, 12) || rd(f, &seg->quality, 4) || rd(f, &nfaces, 4)) {
			goto done;
		}
		seg->pos = v3(pos[0], pos[1], pos[2]);
		seg->mb = rm_mb_create();
		rm_mb_set_quality(seg->mb, seg->quality);
		for (uint32_t fi = 0; fi < nfaces; fi++) {
			float rgb[3];
			uint32_t tex, ncorners;
			if (rd(f, rgb, 12) || rd(f, &tex, 4) || rd(f, &ncorners, 4) || ncorners > 16) {
				goto done;
			}
			RMFace *face = rm_mb_create_face(seg->mb);
			rm_face_set_color_rgb(face, rgb[0], rgb[1], rgb[2]);
			if (tex) {
				rm_face_set_texture(face, out->tex);
			}
			for (uint32_t c = 0; c < ncorners; c++) {
				float v[8];
				if (rd(f, v, sizeof(v))) {
					goto done;
				}
				/* Every corner has its own vertex in the generated meshes. */
				int vi = rm_mb_add_vertex(seg->mb, v[0], v[1], v[2]);
				int ni = rm_mb_add_normal(seg->mb, v[3], v[4], v[5]);
				rm_mb_set_texture_coordinates(seg->mb, vi, v[6], v[7]);
				rm_face_add_vertex_and_normal_indexed(face, vi, ni);
			}
		}
	}
	rc = 0;
done:
	fclose(f);
	if (rc != 0) {
		fprintf(stderr, "track: %s is corrupt or from another version\n", name);
		Track_FreeMeshes(out);
	}
	return rc;
}

void Track_FreeMeshes(TrackMeshes *m)
{
	for (int i = 0; i < m->nseg; i++) {
		RM_RELEASE(m->seg[i].mb);
	}
	RM_RELEASE(m->tex);
	m->nseg = 0;
}
#endif /* GAME_TESTLIB */
