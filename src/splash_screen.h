#pragma once
#include <lvgl.h>

// Pasos de inicialización
enum class SplashStep : uint8_t {
    LITTLEFS = 0,
    DATASTORE,
    PSRAM_CACHE,
    NTP,
    WIFI_CONNECTING,
    WIFI_OK,
    WIFI_FAIL,
    WEBSERVER,
    TELEGRAM,
    DONE
};

enum class SplashState : uint8_t {
    PENDING = 0,
    RUNNING,
    OK,
    WARN,     // completado con advertencia (ej. WiFi sin conectar)
    ERROR
};

void splash_init();
void splash_update(SplashStep step, SplashState state,
                   const char* detail = nullptr);
void splash_finish();   // elimina la pantalla y muestra el tileview
