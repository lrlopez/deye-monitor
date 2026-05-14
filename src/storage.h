#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "solarman.h"

// ── Configuración de red e inversor ──────────────────────────────────────
struct AppConfig {
    char     wifi_ssid[64];
    char     wifi_pass[64];
    char     logger_ip[24];
    uint32_t logger_serial;
};

// ── Estado de sesión persistido en NVS ───────────────────────────────────
// Se guarda tras cada poll exitoso. Al arrancar, permite recuperar
// el día y la hora que no se registraron si el ESP32 estaba apagado.
struct SessionState {
    uint32_t last_record_epoch;
    bool     valid;
};

// ── Config de la gráfica ──────────────────────────────────────────────────
struct ChartConfig {
    bool    autoscale;
    uint8_t max_kw;
};

struct BacklightConfig {
    uint8_t  normal_pct;          // 10–100, default 90
    uint8_t  reduced_pct;         // 0–100,  default 10
    bool     inactivity_enabled;  // default true
    uint8_t  inactivity_div10;    // 1–18 (×10 = 10–180 s), default 6
    bool     night_enabled;       // default true
    uint8_t  night_start_h;       // 0–23, default 0
    uint8_t  night_end_h;         // 0–23, default 8
};

// Cuántos días guardamos 
constexpr uint8_t DAILY_HISTORY_SIZE = 90;

struct TelegramConfig {
    char    token[64];
    char    chat_id[32];
    uint8_t batt_threshold;   // % — alerta si SOC < este valor
    bool    notify_solar;     // notificar inicio/fin de producción
    bool    notify_grid;      // notificar corte/restauración de red
    bool    notify_logger;    // notificar fallo de comunicación
};

class StorageManager {
public:
    static StorageManager& instance() {
        static StorageManager s;
        return s;
    }

    // ── Config ────────────────────────────────────────────────────────────
    void      loadConfig(AppConfig& out);
    void      saveConfig(const AppConfig& cfg);

    // ── Histórico diario ──────────────────────────────────────────────────
    // Añade o sobreescribe el registro del día indicado por timestamp.
    // Uso futuro desde la tarea de red al finalizar cada día.
    void           saveChartConfig(const ChartConfig& cfg);
    ChartConfig    loadChartConfig();

    // ── Configuración Telegram ────────────────────────────────────────────
    void           saveTelegramConfig(const TelegramConfig& cfg);
    TelegramConfig loadTelegramConfig();

    // ── Estado de la sesión ───────────────────────────────────────────────
    void        saveSessionState(const SessionState& s);
    bool        loadSessionState(SessionState& s);
    
    // ── Brillo de la pantalla ─────────────────────────────────────────────
    void            saveBacklightConfig(const BacklightConfig& cfg);
    BacklightConfig loadBacklightConfig(); 
    
private:
    StorageManager() = default;
};

// Acceso global
#define Storage StorageManager::instance()
