#pragma once
#include <lvgl.h>

void energy_profile_init(lv_obj_t* tile);
void energy_profile_tick();
void energy_profile_set_active(bool active);
