#pragma once
#include <lvgl.h>
#include "solarman.h"

void summary_screen_init(lv_obj_t* parent);
void summary_screen_set_active(bool active);
void summary_screen_tick();
void summary_screen_set_live(const DailyStats& d);
