/* Game settings and progress (original object at 0x451438), stored in gamedata.dat. */
#ifndef SAVE_H
#define SAVE_H

#include <stdint.h>

enum { MODE_GRANDPRIX = 0, MODE_TIMEATTACK = 1, MODE_REPLAY = 2 };

typedef struct {
	int16_t mode;            /* 0x000 0 grand prix, 1 time attack, 2 replay */
	int16_t machine;         /* 0x002 selected machine 0..7 */
	int16_t stage;           /* 0x004 0..5 */
	int16_t course;          /* 0x006 0..5 */
	int16_t lives;           /* 0x008 grand prix retries left */
	int16_t pad0;
	float power;             /* 0x00c power carried between grand prix courses */
	float course_time[6];    /* 0x010 grand prix times of the current stage */
	int16_t rank;            /* 0x028 rank of the stage just finished */
	int16_t pad1[3];
	double best_time[6][6];  /* 0x030 */
	double best_lap[6][6];   /* 0x150 */
	int16_t stage_rank[6];   /* 0x270 best rank per stage, 5 = none (E) .. 0 = S */
	int16_t has_replay[6][6];/* 0x27c */
	int16_t opt[4];          /* 0x2c4 options (0x2c4..0x2ca) */
	int16_t opt2[3];         /* 0x2cc options defaulting to 1 */
	int16_t pad2;
	int32_t code[3];         /* 0x2d4 unlock code checksum */
	int16_t extra;           /* 0x2e0 extra courses unlocked (6 per stage) */
	int16_t pad3;
} SaveData;

extern SaveData g_save;

void Save_Defaults(SaveData *s);                               /* 0x418730 */
void Save_Reset(SaveData *s);                                  /* 0x418810 */
void Save_Load(SaveData *s);                                   /* 0x418880 */
void Save_Write(const SaveData *s);                            /* 0x418a10 */
/* Port: store the save files on the card (asks on the PSP; see platform.h).
 * Called where progress should be kept: settings, password, end of a grand
 * prix or a time attack race. */
void Save_Commit(void);
void Save_MarkKept(void);       /* what is on the card now (after loading) */
void Save_ReplayWritten(void);  /* a replay file changed: commit it */
void Save_CheckCode(SaveData *s, const int16_t code[8]);       /* 0x418b70 */
int Save_IsRecord(const SaveData *s, double value, int kind);  /* 0x418be0: kind 10 time, 20 lap, 30 rank */
void Save_SetRecord(SaveData *s, double value, int kind);      /* 0x418c80 */
void Save_MarkCleared(SaveData *s);                            /* 0x418d40 */
void Save_UpdateRank(SaveData *s);                             /* 0x418d60 */

#endif
