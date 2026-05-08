#pragma once
#include <lvgl.h>
#include "solarman.h"

void dashboard_init();
void dashboard_update(const EnergyData& data);