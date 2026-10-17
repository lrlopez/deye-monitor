#pragma once
#include <stdint.h>
#include <math.h>

// Máquinas de estado de las alertas (DEYE-60/41), sin dependencias de Arduino
// ni de Telegram/LVGL: reciben una lectura y devuelven el evento a notificar.
// El llamador decide cómo notificar (Telegram, banner, NVS). Probadas en
// `pio test -e native`.

// ── Producción solar ──────────────────────────────────────────────────────
// Inicio > 50 W, parada < 20 W, 3 lecturas consecutivas (15 s a 5 s/lectura)
struct SolarMonitor {
    static constexpr float   START_W = 50.0f;
    static constexpr float   STOP_W  = 20.0f;
    static constexpr uint8_t DEBOUNCE = 3;
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
    static constexpr uint8_t DEBOUNCE = 12;
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

// ── Fallo del datalogger ──────────────────────────────────────────────────
// 5 fallos consecutivos (~25 s) → un solo aviso hasta que haya una lectura OK.
// `notified` solo se marca con markNotified(): si Telegram no está configurado
// el aviso se reintentará en el siguiente ciclo que llegue a 5.
struct LoggerFailMonitor {
    static constexpr uint8_t THRESHOLD = 5;
    uint8_t fail_cnt = 0;
    bool    notified = false;

    void onSuccess() { fail_cnt = 0; notified = false; }
    // true cuando se acaba de alcanzar el umbral y aún no se ha avisado
    bool onFailure() { return ++fail_cnt == THRESHOLD && !notified; }
    void markNotified() { notified = true; }
};
