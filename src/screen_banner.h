#pragma once
#include <Arduino.h>
#include "telegram.h"   // AlertType

// Banner no-modal de pantalla completa mostrado sobre lv_layer_top().
// Thread-safe: enqueue() puede llamarse desde cualquier tarea;
// tick() debe llamarse SOLO desde loop() (hilo LVGL, Core 1).

void screen_banner_init();
void screen_banner_enqueue(AlertType type, int32_t value = 0);
void screen_banner_tick();
