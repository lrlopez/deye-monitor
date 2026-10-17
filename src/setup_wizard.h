#pragma once
#include <lvgl.h>

// Asistente de primer arranque (DEYE-59).
//
// Un equipo recién flasheado no tiene WiFi ni datalogger configurados, y hasta
// ahora el único camino era descubrir la pantalla Configuración y rellenarla a
// ciegas. El asistente guía en cuatro pasos (WiFi con búsqueda de redes, IP y
// serie del datalogger, clave de la web de administración y resumen), valida cada
// paso con las mismas reglas que la pantalla Configuración, y guarda y reinicia al
// terminar. Se puede omitir; en ese caso no vuelve a salir (todo sigue disponible
// en Configuración).
//
// Todo se llama desde el hilo LVGL.

// true si hay que lanzarlo: el asistente no se ha completado/omitido Y no hay WiFi
// configurada. Si el equipo ya venía configurado lo da por completado y devuelve false.
bool wizard_should_run();

// Crea el asistente sobre lv_layer_top()
void wizard_start();

bool wizard_active();
