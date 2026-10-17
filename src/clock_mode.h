#pragma once
#include <lvgl.h>
#include "solarman.h"

// Modo reloj en reposo (DEYE-54): cuando la pantalla lleva un rato sin tocarse (la
// misma inactividad que atenúa el brillo) se cubre con un reloj grande, la fecha
// y los tres datos que importan de un vistazo (solar, consumo, batería). Cualquier
// toque lo retira y ese primer toque NO llega a la pantalla de debajo.
//
// Todo se llama desde el hilo LVGL (loop()).

// Lee el ajuste guardado (activado por defecto). Llamar una vez en setup().
void clock_mode_init();
// Activa/desactiva el modo reloj y guarda el ajuste en NVS
void clock_mode_set_enabled(bool enabled);
bool clock_mode_enabled();

// Último dato de energía (para los valores del reloj)
void clock_mode_set_energy(const EnergyData& e);

// Llamar en cada vuelta de loop(): `active` = debe estar visible ahora
void clock_mode_tick(bool active);

bool clock_mode_visible();
