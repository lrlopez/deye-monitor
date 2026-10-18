#pragma once
#include <lvgl.h>

void config_screen_init(lv_obj_t* parent);
void config_screen_tick();   // llamar desde loop() para refrescar señal/IP
