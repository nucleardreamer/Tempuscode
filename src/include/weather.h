//
// File: weather.h                      -- Part of TempusMUD
//
// All modifications and additions are
// Copyright 1998 by John Watson, all rights reserved.
//

#ifndef __weather_h__
#define __weather_h__


/* Sun state for weather_data */
#define SUN_DARK	0
#define SUN_RISE	1
#define SUN_LIGHT	2
#define SUN_SET		3


/* Sky conditions for weather_data */
#define SKY_CLOUDLESS	0
#define SKY_CLOUDY	1
#define SKY_RAINING	2
#define SKY_LIGHTNING	3

#define MOON_NEW           0	/* 23 /  0 */
#define MOON_WAX_CRESCENT  1	/*  1 -  4 */
#define MOON_FIRST_QUARTER 2	/*  5 -  6 */
#define MOON_WAX_GIBBOUS   3	/*  7 - 10 */
#define MOON_FULL          4	/* 11 - 12 */
#define MOON_WANE_GIBBOUS  5	/* 13 - 16 */
#define MOON_LAST_QUARTER  6	/* 17 - 18 */
#define MOON_WANE_CRESCENT 7	/* 19 - 22 */

#define MOON_SKY_NONE      0
#define MOON_SKY_RISE      1
#define MOON_SKY_EAST      2
#define MOON_SKY_HIGH      3
#define MOON_SKY_WEST      4
#define MOON_SKY_SET       5


struct weather_data {
	int pressure;				/* How is the pressure ( Mb ) */
	int change;					/* How fast and what way does it change. */
	byte sky;					/* How is the sky. */
	byte sunlight;				/* And how much sun. */
	byte moonlight;
	byte temp;					/* temperature */
	byte humid;					/* humidity */
};

#endif
