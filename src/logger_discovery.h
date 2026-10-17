#pragma once
#include <Arduino.h>

// Redescubrimiento del datalogger si cambia su IP (DHCP) — DEYE-09.
//
// Envía un broadcast UDP al puerto 48899 (el que usan los sticks Solarman/HF
// para descubrimiento) y recoge las IP que responden. Cada candidato se VERIFICA
// con una lectura Modbus real usando el nº de serie configurado, de modo que
// solo se acepta un dispositivo que realmente conteste como nuestro logger.
//
// Bloquea unos segundos (espera de respuestas + verificación): llamar solo desde
// la tarea de red, nunca desde el hilo LVGL.
//
// Devuelve true y rellena ip_out (>= 16 bytes) si encuentra el logger.
bool logger_discover(uint32_t serial, uint8_t unit_id, char* ip_out, size_t ip_out_size);
