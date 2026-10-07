#include "prof.h"

#include <stdio.h>

int g_prof_on;

static uint64_t g_sum[PROF_COUNT];
static const char *const k_names[PROF_COUNT] = {"player", "opponents", "world", "scene", "clip"};

void prof_add(int id, uint32_t us)
{
	g_sum[id] += us;
}

void prof_report(char *buf, int size, int frames)
{
	int at = 0;
	buf[0] = 0;
	for (int i = 0; i < PROF_COUNT && at < size; i++) {
		at += snprintf(buf + at, (size_t)(size - at), "%s %.1f  ", k_names[i],
		               frames ? (double)g_sum[i] / frames / 1000.0 : 0.0);
		g_sum[i] = 0;
	}
}
