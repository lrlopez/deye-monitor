#pragma once
#include <Arduino.h>
#include "solarman.h"
#include "alerts_logic.h"

// Evaluación de alertas y avisos (extraído de solarmanTask — DEYE-41).
//
// Combina las máquinas de estado puras de alerts_logic.h con su "cableado":
// leer la configuración de Telegram, encolar el aviso en Telegram y en el banner
// de la pantalla, y persistir el estado solar. Todo el estado que antes eran
// variables `static` de la tarea vive aquí.
class AlertEngine {
public:
    // ── Datalogger ────────────────────────────────────────────────────────
    void onLoggerOk()   { _logger.onSuccess(); }
    void onLoggerFail();                       // 5º fallo seguido → aviso (una vez)
    uint16_t loggerFailures() const { return _logger.consecutive; }
    // Espera hasta la próxima lectura (backoff si el logger está caído — DEYE-08)
    uint32_t pollDelayMs(uint32_t base_ms, uint32_t max_ms) const {
        return _logger.nextDelayMs(base_ms, max_ms);
    }

    // ── Estado solar ──────────────────────────────────────────────────────
    bool solarActive() const { return _solar.active; }
    // Restaura el estado solar desde NVS (solo si es del mismo día). Una sola vez.
    void restoreSolarState(uint32_t now_ep);
    void onNewDay() { _solar.resetForNewDay(); }

    // ── Alertas de potencia/batería/red ───────────────────────────────────
    // Solo actúa si Telegram está configurado (comportamiento heredado: los
    // banners de pantalla de estas alertas también dependen de ello).
    void evaluate(const EnergyData& e, uint32_t now_ep);

    // ── Temperatura del inversor (DEYE-71) ────────────────────────────────
    // Llamar con cada lectura diaria buena (cada ~60 s); ignora temperaturas no disponibles.
    void evaluateTemp(const DailyStats& d);

    // ── Informe diario por Telegram (DEYE-71) ─────────────────────────────
    // Sale una vez al día a partir de TUNE_DAILY_REPORT_HOUR si está activado en Telegram.
    void checkDailyReport(uint32_t now_ep);

    // ── Espacio libre en flash (DEYE-46) ──────────────────────────────────
    // Comprueba como mucho cada 15 min; llamar libremente desde el bucle.
    void checkFlashSpace();

private:
    SolarMonitor      _solar;
    BatteryMonitor    _batt;
    GridMonitor       _grid;
    LoggerFailMonitor _logger;
    FsSpaceMonitor    _fs;
    TempMonitor       _temp;
    uint32_t          _report_day_key = 0;
    bool              _solar_restored = false;
    uint32_t          _fs_checked_ms  = 0;
};
