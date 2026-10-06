/* Data types recovered from AceSpeeder.exe, parsed into Ghidra by scripts/ApplySigs.java.
 * Offsets in comments are relative to the struct start; global instances noted. */

typedef struct Vec3 {
	float x;
	float y;
	float z;
} Vec3;

/* row-major 3x3 rotation, rows = local X, Y, Z axes */
typedef struct Mat3 {
	float m[9];
} Mat3;

/* A position on the track (passed by value, 36 bytes). */
typedef struct TrackPos {
	short seg;     /* 0x00 segment index */
	short pad0;
	float t;       /* 0x04 0..1 along the segment */
	short slot;    /* 0x08 cross-section edge 0..7 */
	short pad1;
	float u;       /* 0x0c 0..1 across the edge */
	float h;       /* 0x10 height above the surface */
	float t2;      /* 0x14 t*t */
	float omt;     /* 0x18 1-t */
	float omt2;    /* 0x1c (1-t)^2 */
	float omu;     /* 0x20 1-u */
} TrackPos;

/* The course: global instance at 0x44c230. */
typedef struct Track {
	short n;              /* 0x0000 last control point index */
	short pad0;
	float px[30];         /* 0x0004 segment end points (mid points of controls) */
	float py[30];         /* 0x007c */
	float pz[30];         /* 0x00f4 */
	float cx[30];         /* 0x016c control points */
	float cy[30];         /* 0x01e4 */
	float cz[30];         /* 0x025c */
	float bank[30];       /* 0x02d4 roll at segment ends */
	float cbank[30];      /* 0x034c roll at control points */
	float ax[240];        /* 0x03c4 cross-section at segment start, x [seg*8+k] */
	float bx[240];        /* 0x0784 cross-section at segment end, x */
	float ay[240];        /* 0x0b44 */
	float by[240];        /* 0x0f04 */
	short ptype[30];      /* 0x12c4 cross-section profile id */
	short edge[240];      /* 0x1300 surface type per edge */
	float seglen[30];     /* 0x14e0 */
	short chg_start[30];  /* 0x1558 profile differs from previous segment */
	short chg_end[30];    /* 0x1594 profile differs from next segment */
	float f15d0;          /* 0x15d0 */
	float f15d4;          /* 0x15d4 */
	Vec3 center[30];      /* 0x15d8 segment centres (culling) */
	float radius[30];     /* 0x1740 segment bounding radius */
	short nscenery;       /* 0x17b8 */
	short pad2;
	float scen[0x96];     /* 0x17bc random scenery data (layout TBD) */
	short laps;           /* 0x1a14 (0x44dc44) */
	short pad3;
	double reftime;       /* 0x1a18 per-course reference value (68.0, 99.0, ...) */
	short env;            /* 0x1a20 scenery set: course/sky/ground<env+1> */
	short s1a22;          /* 0x1a22 */
	short s1a24;          /* 0x1a24 */
} Track;

/* A racing machine; array of 7 at 0x44b670 (index 0 = player). Size 0x178. */
typedef struct Machine {
	short model;          /* 0x000 machine type 0..7 */
	short pad0;
	TrackPos tp;          /* 0x004 position on the track */
	Vec3 pos;             /* 0x028 world position */
	Mat3 rot;             /* 0x034 world orientation */
	char u58[0x50];       /* 0x058 */
	short alive;          /* 0x0a8 */
	short u_aa;           /* 0x0aa */
	short u_ac;           /* 0x0ac */
	short lap;            /* 0x0ae */
	float power;          /* 0x0b0 */
	char ub4[0x3c];       /* 0x0b4 */
	double time;          /* 0x0f0 */
	char uf8[0x7c];       /* 0x0f8 */
	short u174;           /* 0x174 */
	short is_ai;          /* 0x176 */
} Machine;
