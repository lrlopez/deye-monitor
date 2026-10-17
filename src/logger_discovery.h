#pragma once
#include <Arduino.h>

// Descubrimiento del datalogger en la red local — DEYE-09 (redescubrimiento si
// cambia su IP por DHCP) y búsqueda manual desde el asistente / Configuración.
//
// Envía un broadcast UDP al puerto 48899 (el que usan los sticks Solarman/HF
// para descubrimiento) y recoge las IP que responden. Cada candidato se puede
// VERIFICAR con una lectura Modbus real usando el nº de serie, de modo que solo
// se acepta un dispositivo que realmente conteste como nuestro logger.
//
// Todas las funciones bloquean unos segundos (espera de respuestas / conexión TCP):
// llamar solo desde una tarea de red, nunca desde el hilo LVGL.

// Solo la parte UDP: rellena ips[0..n-1] (16 bytes cada una) con las IP que
// responden (sin repetidas) y devuelve n (<= max). Sin verificar.
int logger_scan(char (*ips)[16], int max);

// true si `ip` contesta una lectura Modbus real firmada con `serial`.
bool logger_verify(const char* ip, uint32_t serial, uint8_t unit_id);

// Escaneo + verificación con el serie configurado (redescubrimiento automático).
// Devuelve true y rellena ip_out (>= 16 bytes) si encuentra el logger.
bool logger_discover(uint32_t serial, uint8_t unit_id, char* ip_out, size_t ip_out_size);
