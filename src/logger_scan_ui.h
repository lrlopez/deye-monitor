#pragma once
#include <lvgl.h>

// Botón «buscar logger» para los campos IP / serie del datalogger (asistente de
// primer arranque y pantalla Configuración → Inversor).
//
// Al pulsarlo lanza el descubrimiento UDP (logger_discovery) en una tarea aparte y,
// mientras tanto, muestra un icono de espera. Si el campo de serie contiene un número
// válido, los candidatos se verifican con una lectura Modbus real y solo se ofrecen los
// verificados (si ninguno lo está, se ofrecen todos avisando de que no se han podido
// verificar). Un único resultado se rellena directamente; varios se ofrecen en una lista.
//
// Requiere WiFi conectada. Se llama desde el hilo LVGL.
//
// Crea el botón en (x, y) con ancho w y lo devuelve; `ta_ip` recibe la IP elegida
// (con lv_textarea_set_text, así se dispara la validación del campo) y `ta_serial`
// puede ser nullptr.
lv_obj_t* logger_scan_button(lv_obj_t* parent, int x, int y, int w,
                             lv_obj_t* ta_ip, lv_obj_t* ta_serial);
