/* Gates and buildings of each course (data extracted from the original's 36
 * course-object functions by re/scripts/extract_objects.py). */
#ifndef COURSE_OBJECTS_H
#define COURSE_OBJECTS_H

#include <stdint.h>

typedef struct {
	int16_t kind;      /* gate<kind+1>.x */
	float s;           /* track position */
	int16_t mode;      /* 0 upright, 1 banked with the track */
	float speed;       /* spin per tick around the track direction */
	float scale[3];
} CourseGate;

typedef struct {
	int16_t kind;      /* build<kind+1>.x */
	float s;           /* track position */
	float side;        /* lateral offset from the centre line */
	float yaw;         /* turns around the vertical axis */
	float scale[3];
} CourseBuilding;

typedef struct {
	int ngates;
	CourseGate gates[16];
	int nbuildings;
	CourseBuilding buildings[24];
} CourseObjects;

extern const CourseObjects k_course_objects[6][6];

#endif
