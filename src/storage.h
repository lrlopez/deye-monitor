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

// ── Registro horario ──────────────────────────────────────────────────────
// Valores en Wh (deltas del periodo). int16_t → max 32767 Wh/h, suficiente para 6 kW.
struct HourlyRecord {
    int16_t pv_wh;
    int16_t export_wh;
    int16_t import_wh;
    int16_t batt_charge_wh;
    int16_t batt_discharge_wh;
    int16_t load_wh;
    uint8_t soc;      // % al final del periodo
    uint8_t valid;    // 0 = sin dato
};  // 14 bytes

// Un día completo = 24 registros horarios
struct DayData {
    uint32_t     day_epoch;   // medianoche local (epoch)
    HourlyRecord hours[24];
};  // 4 + 24×14 = 340 bytes — 7 días = 2380 bytes en NVS

// ── Config de la gráfica ──────────────────────────────────────────────────
struct ChartConfig {
    bool    autoscale;
    uint8_t max_kw;
};

// Cuántos días guardamos 
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
    void        pushDailyRecord(const DailyRecord& rec);
    uint8_t     getDailyHistory(DailyRecord* out, uint8_t maxCount);
    void        clearDailyHistory();
    void        saveHourlyRecord(uint32_t day_epoch, uint8_t hour, const HourlyRecord& rec);
    bool        getDayData(uint32_t day_epoch, DayData& out);
    void        saveChartConfig(const ChartConfig& cfg);
    ChartConfig loadChartConfig();
    bool getDayRecord(uint32_t day_epoch, DailyRecord& out);
    
private:
    StorageManager() = default;

    // Cabecera del buffer circular en namespace "hist_d"
    struct HistMeta {
        uint8_t head;   // índice del registro más antiguo
        uint8_t count;  // cuántos registros válidos hay
    };

    void        readMeta(HistMeta& m);
    void        writeMeta(const HistMeta& m);
    DailyRecord readRecord(uint8_t idx);
    void        writeRecord(uint8_t idx, const DailyRecord& r);
};

// Acceso global
#define Storage StorageManager::instance()
