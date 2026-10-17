#pragma once
#include <stdint.h>
#include <math.h>
#include "tuning.h"

// Máquinas de estado de las alertas (DEYE-60/41), sin dependencias de Arduino
// ni de Telegram/LVGL: reciben una lectura y devuelven el evento a notificar.
// El llamador decide cómo notificar (Telegram, banner, NVS). Probadas en
// `pio test -e native`.

// ── Producción solar ──────────────────────────────────────────────────────
// Inicio > 50 W, parada < 20 W, 3 lecturas consecutivas (15 s a 5 s/lectura)
struct SolarMonitor {
    static constexpr float   START_W = (float)TUNE_SOLAR_START_W;
    static constexpr float   STOP_W  = (float)TUNE_SOLAR_STOP_W;
    static constexpr uint8_t DEBOUNCE = TUNE_SOLAR_DEBOUNCE;
    enum class Event : uint8_t { NONE, START, STOP };

    bool    active   = false;
    uint8_t debounce = 0;

    Event update(float pv_w) {
        if (!active && pv_w > START_W) {
            if (++debounce >= DEBOUNCE) { active = true; debounce = 0; return Event::START; }
        } else if (active && pv_w < STOP_W) {
            if (++debounce >= DEBOUNCE) { active = false; debounce = 0; return Event::STOP; }
        } else {
            debounce = 0;
        }
        return Event::NONE;
    }
    // Cambio de día: de noche no hay producción
    void resetForNewDay() { active = false; debounce = 0; }
};

// ── Batería ───────────────────────────────────────────────────────────────
// Dos niveles: aviso (warn) y crítico (crit); 0 desactiva el nivel.
// Recuperación con histéresis +5 puntos sobre el umbral de aviso.
// CRIT→WARN al subir 5 puntos sobre crit es silencioso.
struct BatteryMonitor {
    enum class State : uint8_t { NORMAL, WARN, CRIT };
    enum class Event : uint8_t { NONE, WARN_LOW, CRITICAL, RECOVERED };

    State state = State::NORMAL;

    static uint8_t plus5(uint8_t v) { return (uint8_t)(v + 5 > 100 ? 100 : v + 5); }

    Event update(uint8_t soc, uint8_t warn, uint8_t crit) {
        const uint8_t rec = plus5(warn);
        switch (state) {
        case State::NORMAL:
            if (crit > 0 && soc < crit) { state = State::CRIT; return Event::CRITICAL; }
            if (warn > 0 && soc < warn) { state = State::WARN; return Event::WARN_LOW; }
            break;
        case State::WARN:
            if (crit > 0 && soc < crit) { state = State::CRIT; return Event::CRITICAL; }
            if (soc >= rec)             { state = State::NORMAL; return Event::RECOVERED; }
            break;
        case State::CRIT:
            if (soc >= rec)             { state = State::NORMAL; return Event::RECOVERED; }
            if (warn > 0 && soc >= plus5(crit)) state = State::WARN;   // silencioso
            break;
        }
        return Event::NONE;
    }
};

// ── Red eléctrica ─────────────────────────────────────────────────────────
// Corte: grid = 0 W exacto durante 12 lecturas (60 s). Solo se evalúa tras
// haber visto red activa al menos una vez (evita falsos cortes al arrancar).
struct GridMonitor {
    static constexpr uint8_t DEBOUNCE = TUNE_GRID_OUTAGE_READINGS;
    enum class Event : uint8_t { NONE, OUTAGE, RESTORED };

    bool    outage   = false;
    bool    had_grid = false;
    uint8_t debounce = 0;

    Event update(float grid_w) {
        const bool active = fabsf(grid_w) != 0.0f;
        if (active) had_grid = true;

        if (had_grid && !outage && !active) {
            if (++debounce >= DEBOUNCE) { outage = true; debounce = 0; return Event::OUTAGE; }
        } else if (outage && active) {
            outage = false; debounce = 0; return Event::RESTORED;
        } else if (active) {
            debounce = 0;
        }
        return Event::NONE;
    }
};

// ── Espacio libre en la flash (LittleFS) ──────────────────────────────────
// Aviso cuando el espacio libre baja del umbral y vuelta a "armado" solo cuando
// sube por encima de otro umbral mayor (histéresis), para no repetir el aviso
// con el valor oscilando alrededor del límite. La partición es de 8 MB y el
// histórico en régimen ocupa ~3,9 MB, así que saltar indica algo anómalo
// (importación, STORE_DAYS ampliado, ficheros residuales…).
struct FsSpaceMonitor {
    static constexpr uint8_t WARN_BELOW_PCT   = TUNE_FS_WARN_BELOW_PCT;    // aviso si libre < 10 %
    static constexpr uint8_t REARM_ABOVE_PCT  = TUNE_FS_REARM_ABOVE_PCT;   // se rearma con libre >= 15 %
    enum class Event : uint8_t { NONE, LOW_SPACE };

    bool warned = false;

    // total_bytes == 0 (FS sin montar) no genera avisos
    Event update(uint64_t total_bytes, uint64_t used_bytes) {
        if (total_bytes == 0) return Event::NONE;
        if (used_bytes > total_bytes) used_bytes = total_bytes;
        const uint8_t free_pct = freePercent(total_bytes, used_bytes);
        if (!warned && free_pct < WARN_BELOW_PCT) { warned = true; return Event::LOW_SPACE; }
        if (warned && free_pct >= REARM_ABOVE_PCT) warned = false;
        return Event::NONE;
    }
    static uint8_t freePercent(uint64_t total_bytes, uint64_t used_bytes) {
        if (total_bytes == 0 || used_bytes >= total_bytes) return 0;
        return (uint8_t)(((total_bytes - used_bytes) * 100ULL) / total_bytes);
    }
};

// ── Fallo del datalogger ──────────────────────────────────────────────────
// 5 fallos consecutivos (~25 s) → un solo aviso hasta que haya una lectura OK.
// `notified` solo se marca con markNotified(): si Telegram no está configurado
// el aviso se reintentará en el siguiente ciclo que llegue a 5.
struct LoggerFailMonitor {
    static constexpr uint8_t THRESHOLD = TUNE_LOGGER_FAIL_READINGS;
    uint8_t  fail_cnt    = 0;
    bool     notified    = false;
    uint16_t consecutive = 0;   // fallos seguidos (satura en 65535), para el backoff

    void onSuccess() { fail_cnt = 0; notified = false; consecutive = 0; }
    // true cuando se acaba de alcanzar el umbral y aún no se ha avisado
    bool onFailure() {
        if (consecutive < 0xFFFF) consecutive++;
        return ++fail_cnt == THRESHOLD && !notified;
    }
    void markNotified() { notified = true; }

    // Espera hasta la próxima lectura (DEYE-08). Hasta THRESHOLD fallos se
    // mantiene el intervalo normal (para que el aviso siga saltando a los
    // ~25 s); después se duplica con cada fallo hasta max_ms, para no
    // martillear un logger caído o saturado.
    uint32_t nextDelayMs(uint32_t base_ms, uint32_t max_ms) const {
        if (consecutive <= THRESHOLD) return base_ms;
        uint32_t extra = consecutive - THRESHOLD;
        if (extra > 10) extra = 10;
        uint32_t d = base_ms << extra;
        return d > max_ms ? max_ms : d;
    }
};
