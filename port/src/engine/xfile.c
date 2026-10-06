#include "xfile.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/platform.h"

/* The text format is parsed generically into a tree of objects, each holding
 * the numbers and strings that appear directly inside it, in order.
 * ';' and ',' are only separators, so they are skipped. */

typedef struct XNode {
	char type[32];
	float *num;
	int nnum, capnum;
	char str[64]; /* first string literal (TextureFilename) */
	struct XNode **child;
	int nchild;
} XNode;

enum { T_EOF, T_LBRACE, T_RBRACE, T_IDENT, T_NUMBER, T_STRING };

typedef struct {
	int kind;
	char text[64];
	float value;
} Token;

typedef struct {
	const char *p, *end;
	Token pushed; /* one token of lookahead */
	int has_pushed;
} Lexer;

static void skip_space(Lexer *lx)
{
	while (lx->p < lx->end) {
		char c = *lx->p;
		if (isspace((unsigned char)c) || c == ';' || c == ',') {
			lx->p++;
		}
		else if (c == '<') { /* GUID */
			while (lx->p < lx->end && *lx->p != '>') {
				lx->p++;
			}
			lx->p++;
		}
		else if (c == '/' && lx->p + 1 < lx->end && lx->p[1] == '/') {
			while (lx->p < lx->end && *lx->p != '\n') {
				lx->p++;
			}
		}
		else if (c == '#') {
			while (lx->p < lx->end && *lx->p != '\n') {
				lx->p++;
			}
		}
		else {
			break;
		}
	}
}

static Token next(Lexer *lx)
{
	Token t = {T_EOF, "", 0.0f};
	if (lx->has_pushed) {
		lx->has_pushed = 0;
		return lx->pushed;
	}
	skip_space(lx);
	if (lx->p >= lx->end) {
		return t;
	}
	char c = *lx->p;
	if (c == '{') {
		lx->p++;
		t.kind = T_LBRACE;
	}
	else if (c == '}') {
		lx->p++;
		t.kind = T_RBRACE;
	}
	else if (c == '"') {
		lx->p++;
		int n = 0;
		while (lx->p < lx->end && *lx->p != '"') {
			if (n < (int)sizeof(t.text) - 1) {
				t.text[n++] = *lx->p;
			}
			lx->p++;
		}
		lx->p++;
		t.text[n] = 0;
		t.kind = T_STRING;
	}
	else if (isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.') {
		char *e;
		t.value = strtof(lx->p, &e);
		lx->p = e > lx->p ? e : lx->p + 1;
		t.kind = T_NUMBER;
	}
	else {
		int n = 0;
		while (lx->p < lx->end && (isalnum((unsigned char)*lx->p) || *lx->p == '_')) {
			if (n < (int)sizeof(t.text) - 1) {
				t.text[n++] = *lx->p;
			}
			lx->p++;
		}
		if (n == 0) { /* unknown character: skip it */
			lx->p++;
			return next(lx);
		}
		t.text[n] = 0;
		t.kind = T_IDENT;
	}
	return t;
}

static void unget(Lexer *lx, Token t)
{
	lx->pushed = t;
	lx->has_pushed = 1;
}

static void node_free(XNode *n)
{
	if (!n) {
		return;
	}
	for (int i = 0; i < n->nchild; i++) {
		node_free(n->child[i]);
	}
	free(n->child);
	free(n->num);
	free(n);
}

static void push_num(XNode *n, float v)
{
	if (n->nnum == n->capnum) {
		n->capnum = n->capnum ? n->capnum * 2 : 256;
		n->num = realloc(n->num, sizeof(float) * n->capnum);
	}
	n->num[n->nnum++] = v;
}

/* Parses the body of an object after its '{'. */
static XNode *parse_body(Lexer *lx, const char *type)
{
	XNode *n = calloc(1, sizeof(XNode));
	snprintf(n->type, sizeof(n->type), "%s", type);
	for (;;) {
		Token t = next(lx);
		if (t.kind == T_EOF || t.kind == T_RBRACE) {
			return n;
		}
		if (t.kind == T_NUMBER) {
			push_num(n, t.value);
		}
		else if (t.kind == T_STRING) {
			if (!n->str[0]) {
				snprintf(n->str, sizeof(n->str), "%s", t.text);
			}
		}
		else if (t.kind == T_LBRACE) { /* reference {name} or anonymous block: skip */
			XNode *skip = parse_body(lx, "");
			node_free(skip);
		}
		else if (t.kind == T_IDENT) {
			/* Type [name] { ... } -- anything else (template members) is skipped */
			Token u = next(lx);
			if (u.kind == T_IDENT) {
				Token w = next(lx);
				if (w.kind != T_LBRACE) {
					unget(lx, w);
					continue;
				}
				u = w;
			}
			if (u.kind != T_LBRACE) {
				unget(lx, u);
				continue;
			}
			XNode *c = parse_body(lx, t.text);
			n->child = realloc(n->child, sizeof(XNode *) * (n->nchild + 1));
			n->child[n->nchild++] = c;
		}
	}
}

static XNode *find_child(XNode *n, const char *type)
{
	for (int i = 0; i < n->nchild; i++) {
		if (!strcmp(n->child[i]->type, type)) {
			return n->child[i];
		}
	}
	return NULL;
}

/* Reads "n; indices..." face lists. Returns number of numbers consumed. */
static int read_faces(const float *num, int nnum, int pos, int nfaces, int **start, int **count, int **idx)
{
	int total = 0, p = pos;
	for (int f = 0; f < nfaces && p < nnum; f++) {
		int c = (int)num[p];
		total += c;
		p += 1 + c;
	}
	*start = malloc(sizeof(int) * nfaces);
	*count = malloc(sizeof(int) * nfaces);
	*idx = malloc(sizeof(int) * (total ? total : 1));
	p = pos;
	int k = 0;
	for (int f = 0; f < nfaces; f++) {
		int c = p < nnum ? (int)num[p++] : 0;
		(*start)[f] = k;
		(*count)[f] = c;
		for (int i = 0; i < c; i++) {
			(*idx)[k++] = p < nnum ? (int)num[p++] : 0;
		}
	}
	return p - pos;
}

static int build_mesh(XMesh *m, XNode *mesh)
{
	const float *num = mesh->num;
	int nnum = mesh->nnum, p = 0;
	if (nnum < 1) {
		return -1;
	}
	m->nverts = (int)num[p++];
	m->verts = malloc(sizeof(Vec3) * (m->nverts ? m->nverts : 1));
	for (int i = 0; i < m->nverts; i++, p += 3) {
		m->verts[i] = v3(num[p], num[p + 1], num[p + 2]);
	}
	m->nfaces = p < nnum ? (int)num[p++] : 0;
	read_faces(num, nnum, p, m->nfaces, &m->face_start, &m->face_count, &m->idx);

	XNode *nr = find_child(mesh, "MeshNormals");
	if (nr && nr->nnum > 0) {
		int q = 0;
		m->nnormals = (int)nr->num[q++];
		m->normals = malloc(sizeof(Vec3) * (m->nnormals ? m->nnormals : 1));
		for (int i = 0; i < m->nnormals; i++, q += 3) {
			m->normals[i] = v3_norm(v3(nr->num[q], nr->num[q + 1], nr->num[q + 2]));
		}
		int nf = (int)nr->num[q++];
		int *s, *c, *ni;
		read_faces(nr->num, nr->nnum, q, nf, &s, &c, &ni);
		/* Normal faces mirror the mesh faces one to one. */
		int total = m->nfaces ? m->face_start[m->nfaces - 1] + m->face_count[m->nfaces - 1] : 0;
		m->nidx = calloc(total ? total : 1, sizeof(int));
		for (int f = 0; f < m->nfaces && f < nf; f++) {
			for (int i = 0; i < m->face_count[f] && i < c[f]; i++) {
				m->nidx[m->face_start[f] + i] = ni[s[f] + i];
			}
		}
		free(s);
		free(c);
		free(ni);
	}

	XNode *tc = find_child(mesh, "MeshTextureCoords");
	if (tc && tc->nnum > 0) {
		int n = (int)tc->num[0];
		m->uv = calloc(m->nverts ? m->nverts : 1, sizeof(float[2]));
		for (int i = 0; i < n && i < m->nverts && 2 + i * 2 < tc->nnum; i++) {
			m->uv[i][0] = tc->num[1 + i * 2];
			m->uv[i][1] = tc->num[2 + i * 2];
		}
	}

	XNode *ml = find_child(mesh, "MeshMaterialList");
	if (ml && ml->nnum >= 2) {
		int nmat = (int)ml->num[0];
		int nfi = (int)ml->num[1];
		m->face_material = calloc(m->nfaces ? m->nfaces : 1, sizeof(int));
		for (int f = 0; f < m->nfaces; f++) {
			/* If fewer indices than faces are given, the last one repeats. */
			int i = f < nfi ? f : nfi - 1;
			m->face_material[f] = (i >= 0 && 2 + i < ml->nnum) ? (int)ml->num[2 + i] : 0;
		}
		m->materials = calloc(nmat ? nmat : 1, sizeof(XMaterial));
		int k = 0;
		for (int c = 0; c < ml->nchild && k < nmat; c++) {
			XNode *mat = ml->child[c];
			if (strcmp(mat->type, "Material")) {
				continue;
			}
			XMaterial *xm = &m->materials[k++];
			const float *v = mat->num;
			xm->r = mat->nnum > 0 ? v[0] : 1;
			xm->g = mat->nnum > 1 ? v[1] : 1;
			xm->b = mat->nnum > 2 ? v[2] : 1;
			xm->a = mat->nnum > 3 ? v[3] : 1;
			xm->power = mat->nnum > 4 ? v[4] : 0;
			for (int j = 0; j < 3; j++) {
				xm->spec[j] = mat->nnum > 5 + j ? v[5 + j] : 0;
				xm->emis[j] = mat->nnum > 8 + j ? v[8 + j] : 0;
			}
			XNode *tf = find_child(mat, "TextureFilename");
			if (tf) {
				snprintf(xm->texture, sizeof(xm->texture), "%s", tf->str);
			}
		}
		m->nmaterials = k;
	}
	return 0;
}

int xmesh_load(XMesh *m, const char *name)
{
	memset(m, 0, sizeof(*m));
	FILE *f = plat_open_data(name, "rb");
	if (!f) {
		return -1;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc(size + 1);
	size_t got = fread(buf, 1, size, f);
	fclose(f);
	buf[got] = 0;
	if (got < 16 || strncmp(buf, "xof ", 4) || strncmp(buf + 8, "txt", 3)) {
		free(buf);
		return -1; /* only the text format is used by the game */
	}
	Lexer lx = {buf + 16, buf + got, {0}, 0};
	XNode *root = calloc(1, sizeof(XNode));
	for (;;) {
		Token t = next(&lx);
		if (t.kind == T_EOF) {
			break;
		}
		if (t.kind != T_IDENT) {
			continue;
		}
		if (!strcmp(t.text, "template")) {
			next(&lx); /* name */
			Token b = next(&lx);
			if (b.kind == T_LBRACE) {
				node_free(parse_body(&lx, ""));
			}
			continue;
		}
		Token u = next(&lx);
		if (u.kind == T_IDENT) {
			Token w = next(&lx);
			if (w.kind != T_LBRACE) {
				unget(&lx, w);
				continue;
			}
			u = w;
		}
		else if (u.kind != T_LBRACE) {
			unget(&lx, u);
			continue;
		}
		if (u.kind == T_LBRACE) {
			XNode *c = parse_body(&lx, t.text);
			root->child = realloc(root->child, sizeof(XNode *) * (root->nchild + 1));
			root->child[root->nchild++] = c;
		}
	}
	free(buf);
	XNode *mesh = find_child(root, "Mesh");
	int rc = mesh ? build_mesh(m, mesh) : -1;
	node_free(root);
	return rc;
}

void xmesh_free(XMesh *m)
{
	free(m->verts);
	free(m->uv);
	free(m->normals);
	free(m->face_start);
	free(m->face_count);
	free(m->idx);
	free(m->nidx);
	free(m->materials);
	free(m->face_material);
	memset(m, 0, sizeof(*m));
}
