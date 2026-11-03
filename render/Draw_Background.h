#ifndef DRAW_BACKGROUND_H
#define DRAW_BACKGROUND_H

/* Internal object helpers; clients should include Art.h. */
#include "Art.h"
#include "Art_Style.h"


void draw_floor(void);
void draw_back_wall(void);
void draw_rafters(void);
void draw_cobweb(float cx, float cy, float reach, int flip);
void wooden_crate(float x, float y, float w, float h);
void draw_clutter(void);
void draw_vignette(void);
void name_plate(float cx, float y, float px, const char *text, unsigned ink);
void draw_station(int item);
void draw_cauldron(void);
void draw_spellbook(void);
void candle_flame(float x, float y, float time_s, int seed);

#endif
