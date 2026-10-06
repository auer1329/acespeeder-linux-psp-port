/* The menus (original object at 0x45a658), game state 200. */
#ifndef MENU_H
#define MENU_H

#include <stdint.h>

typedef struct {
	int16_t screen;  /* see menu.c */
	int16_t state;   /* 300 shown, 400 leaving after a choice, 500 going back, 100 free */
	float t;         /* ticks in this state, grows 1.65 */
} MenuLayer;

typedef struct {
	int16_t state;       /* 0x00 20 = needs setup, 10 = running */
	int16_t pad0;
	float cooldown;      /* 0x04 input is taken when below 0 */
	int16_t cursor;      /* 0x08 options row / password position */
	int16_t code[8];     /* 0x0a password letters 0..5 */
	int16_t pad1;
	MenuLayer layer[5];  /* 0x1c */
	float time;          /* 0x44 */
	int16_t rotdir;      /* 0x48 carousel turning: 1, -1 or 0 */
	int16_t pad2;
	float rottime;       /* 0x4c */
} Menu;

extern Menu g_menu;

void Menu_Update(Menu *m);  /* 0x41f300 */
void Menu_Render(Menu *m);  /* 0x422990 */
void Menu_Free(Menu *m);    /* 0x41f1c0 */

#endif
