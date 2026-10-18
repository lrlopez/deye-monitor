#pragma once
#include <lvgl.h>

// Botón «inicio» (icono de casa) en la esquina superior izquierda, a la izquierda del botón
// de navegación «‹» de cada pantalla. Solo se ve fuera del dashboard y lleva a él al tocarlo.
// Es hijo de la pantalla principal (no de layer_top): lo tapan el modo reloj, los diálogos y
// el asistente, igual que a los puntos de paginación.
typedef void (*home_button_cb_t)();

void home_button_init(lv_obj_t* parent, home_button_cb_t on_tap);
// `on_dashboard`: la pantalla activa es el dashboard (el botón se oculta)
void home_button_set_dashboard(bool on_dashboard);
