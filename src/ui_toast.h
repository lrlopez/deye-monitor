#pragma once
#include <lvgl.h>

// Aviso breve y no modal (una línea) en la parte inferior de la pantalla, sobre
// lv_layer_top(). No bloquea el uso: no captura toques y se retira solo.
// Llamar SOLO desde el hilo LVGL (loop()).
void ui_toast_show(const char* text, uint32_t duration_ms = 4500);
