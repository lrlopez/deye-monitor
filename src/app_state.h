#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>
#include "solarman.h"
#include "storage.h"

// Estado global compartido entre main.cpp (setup/loop, hilo LVGL en Core 1) y las
// tareas de red (Core 0). Definido en main.cpp.

extern AppConfig         g_cfg;            // cargada desde NVS en setup()
extern SemaphoreHandle_t g_mutex;          // protege g_energy, g_daily y sus contadores
extern EnergyData        g_energy;
extern DailyStats        g_daily;
// Contadores de secuencia: se incrementan SIEMPRE bajo g_mutex al publicar un
// dato nuevo. loop() recuerda el último visto y copia el dato bajo el mutex
// (DEYE-11).
extern uint32_t          g_energy_seq;
extern uint32_t          g_daily_seq;
extern volatile bool     g_wifi_connected;

// Serializa las escrituras en flash de Core 0 con el volcado al display de
// Core 1 (en las placas P4 ambos compiten por la PSRAM/flash). nullptr en la
// Sunton, donde no hace falta.
extern SemaphoreHandle_t g_flash_display_mutex;

// Toma g_flash_display_mutex mientras viva el objeto (no hace nada si es nullptr)
class FlashGuard {
public:
    FlashGuard()  { if (g_flash_display_mutex) xSemaphoreTake(g_flash_display_mutex, portMAX_DELAY); }
    ~FlashGuard() { if (g_flash_display_mutex) xSemaphoreGive(g_flash_display_mutex); }
    FlashGuard(const FlashGuard&) = delete;
    FlashGuard& operator=(const FlashGuard&) = delete;
};
