#pragma once
#include <lvgl.h>
#include "solarman.h"

void dashboard_init(lv_obj_t* parent);   
void dashboard_update(const EnergyData& data);
void dashboard_tick();