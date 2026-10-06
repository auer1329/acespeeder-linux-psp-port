/* Particle effects and animated decorations: the update half of objects whose
 * rendering lives in the race renderer. */
#include <stddef.h>
#include <math.h>

#include "effects.h"
#include "machine.h"
#include "track.h"

_Static_assert(offsetof(Explosion, pos) == 0xcc, "Explosion layout");
_Static_assert(offsetof(Gates, speed) == 0x384, "Gates layout");
_Static_assert(offsetof(Scenery, spin) == 0x690, "Scenery layout");

Explosion g_explosion;
Gates g_gates;
Scenery g_scenery;

void Sparks_Update(void)
{
	Sparks *s = &g_sparks;
	for (int i = 0; i < 50; i++) {
		s->life[i] = DADD(s->life[i], 1.0);
		if (s->life[i] > 25.0f) {
			s->active[i] = 0;
		}
		Vec3 *v = &s->vel[i];
		v->x = DMUL(v->x, 0.98);
		v->y = DADD(DMUL(v->y, 0.98), -0.3);
		v->z = DADD(DMUL(v->z, 0.98), -0.2);
		s->pos[i].x = v->x + s->pos[i].x;
		s->pos[i].y = v->y + s->pos[i].y;
		s->pos[i].z = s->pos[i].z + v->z;
	}
}

void Explosion_Add(Vec3 pos)
{
	Explosion *e = &g_explosion;
	int n = e->next;
	e->active[n] = 1;
	e->life[n] = 0;
	e->pos[n] = pos;
	e->pos[n].z = x87_muld(cos((double)(n + n)), 10.0) + e->pos[n].z;
	e->pos[n].x = x87_muld(sin((double)(n + n)), 10.0) + e->pos[n].x;
	e->next = (int16_t)((n + 1) % 50);
}

void Explosion_Update(void)
{
	Explosion *e = &g_explosion;
	for (int i = 0; i < 50; i++) {
		e->life[i] = (int16_t)(e->life[i] + 1);
		if (e->life[i] > 40) {
			e->active[i] = 0;
		}
		e->pos[i].y = DADD(e->pos[i].y, 5.0);
		/* fadd st(0),st(0) rounds the doubled value to single precision too */
		float a = (float)(2.0 * sin((double)(crt_rand() % 10000)));
		e->pos[i].x = a + e->pos[i].x;
		float b = (float)(2.0 * cos((double)(crt_rand() % 10000)));
		e->pos[i].z = b + e->pos[i].z;
	}
}

void Scenery_Init(void)
{
	g_scenery.count = 0;
	g_scenery.color[0] = 0.3f;
	g_scenery.color[1] = 0.3f;
	g_scenery.color[2] = 0.35f;
}

/* An object `side` units to the side of the track at progress s. */
void Scenery_Add(float s, float side, float yaw)
{
	Scenery *sc = &g_scenery;
	if (sc->count >= 32) {
		return;
	}
	TrackPos p = TrackPos_Make(s, 3.5f, 0.0f);
	Vec3 base = Track_Point(&g_track, p);
	base.y = 0.0f;
	Vec3 o = Mat3_MulVec(v3(side, 0.0f, 0.0f), Track_FlatFrame(&g_track, p));
	sc->pos[sc->count] = Vec3_Add(base, o);
	sc->spin[sc->count] = DMUL(RandF(), 1000.0);
	sc->yaw[sc->count] = yaw;
	sc->count++;
}

void Scenery_Populate(int n)
{
	for (int i = 0; i < n; i++) {
		float s = RandF();
		int segs = g_track.n + 1;
		float side;
		if (RandF() >= 0.5f) {
			side = x87_add(-1500.0, -(double)DMUL(RandF(), 3500.0));
		}
		else {
			side = DADD(DMUL(RandF(), 3500.0), 1500.0);
		}
		float yaw = DMUL(RandF(), 6.28);
		Scenery_Add(s * (float)segs, side, yaw);
	}
}

void Scenery_Update(void)
{
	for (int i = 0; i < g_scenery.count; i++) {
		g_scenery.spin[i] = DADD(g_scenery.spin[i], 0.02);
	}
}

void Gates_Update(void)
{
	for (int i = 0; i < g_gates.count; i++) {
		g_gates.angle[i] = g_gates.speed[i] + g_gates.angle[i];
	}
}
