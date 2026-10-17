#pragma once
#include <lvgl.h>

// Aviso breve y no modal (una línea) sobre lv_layer_top(). No bloquea el uso: no
// captura toques y se retira solo. Llamar SOLO desde el hilo LVGL (loop()).

// Pista informativa en la parte inferior (encima de los puntos de paginación)
void ui_toast_show(const char* text, uint32_t duration_ms = 4500);

// Aviso de error en la parte SUPERIOR con borde rojo: para formularios, donde el
// teclado en pantalla ocupa la parte inferior y taparía un aviso normal.
void ui_toast_error(const char* text, uint32_t duration_ms = 4000);
