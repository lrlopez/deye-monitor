#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ── Configuración de red e inversor ──────────────────────────────────────
struct AppConfig {
    char     wifi_ssid[64];
    char     wifi_pass[64];
    char     logger_ip[24];
    uint32_t logger_serial;
};

// ── Entrada de histórico diario (para gráficas futuras) ──────────────────
// Tamaño fijo → permite buffer circular en NVS sin fragmentación
struct DailyRecord {
    uint32_t timestamp;        // epoch UTC del día (00:00:00)
    float    pv_kwh;
    float    export_kwh;
    float    import_kwh;
    float    load_kwh;
    float    batt_charge_kwh;
    float    batt_discharge_kwh;
};

// Cuántos días guardamos (cada registro ocupa ~28 bytes)
// NVS tiene ~20 KB libres típicamente → 90 días = 2520 bytes, muy holgado
constexpr uint8_t DAILY_HISTORY_SIZE = 90;

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
    void      pushDailyRecord(const DailyRecord& rec);
    uint8_t   getDailyHistory(DailyRecord* out, uint8_t maxCount);
    void      clearDailyHistory();

private:
    StorageManager() = default;

    // Cabecera del buffer circular en namespace "hist_d"
    struct HistMeta {
        uint8_t head;   // índice del registro más antiguo
        uint8_t count;  // cuántos registros válidos hay
    };

    void      readMeta(HistMeta& m);
    void      writeMeta(const HistMeta& m);
    DailyRecord readRecord(uint8_t idx);
    void        writeRecord(uint8_t idx, const DailyRecord& r);
};

// Acceso global
#define Storage StorageManager::instance()
