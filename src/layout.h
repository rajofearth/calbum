#pragma once
#include "types.h"

void gal_calc_layout(AppState *s, GridLayout *out);
int gal_max_scroll(AppState *s);
int gal_hit_test(AppState *s, int x, int y, int *out_index);
void gal_clamp_zoom_pan(AppState *s);
