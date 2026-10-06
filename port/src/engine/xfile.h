/* Loader for the text DirectX .x files shipped with the game (one Mesh each,
 * with MeshNormals, MeshTextureCoords, MeshMaterialList and MeshFaceWraps). */
#ifndef XFILE_H
#define XFILE_H

#include "rmath.h"

typedef struct {
	float r, g, b, a;
	float power;
	float spec[3];
	float emis[3];
	char texture[64]; /* "" if none */
} XMaterial;

typedef struct {
	int nverts;
	Vec3 *verts;
	float (*uv)[2];   /* per vertex, NULL if absent */
	int nnormals;
	Vec3 *normals;
	int nfaces;
	int *face_start;  /* index into idx[] for each face */
	int *face_count;  /* vertices per face */
	int *idx;         /* vertex indices, concatenated */
	int *nidx;        /* normal indices, parallel to idx[], NULL if absent */
	int nmaterials;
	XMaterial *materials;
	int *face_material; /* per face, NULL if absent */
} XMesh;

/* Returns 0 on success. Data comes from plat_open_data(name). */
int xmesh_load(XMesh *m, const char *name);
void xmesh_free(XMesh *m);

#endif
