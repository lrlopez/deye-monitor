#pragma once
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "tuning.h"

// Autonomía estimada de la batería (DEYE-33): cuánto falta para vaciarse (descargando) o
// para llegar al 100 % (cargando) si la potencia actual se mantiene. Sin dependencias de
// Arduino; probado en `pio test -e native`.
//
// La potencia instantánea del inversor oscila mucho (arranque de un horno, nubes), así que
// se promedia con una media móvil exponencial; si cambia el sentido (de cargar a descargar o
// al revés) se reinicia al valor nuevo para no mezclar regímenes.

struct BatteryEta {
    enum class Mode : uint8_t { NONE, EMPTY, FULL };
    Mode     mode    = Mode::NONE;   // EMPTY: tiempo hasta vaciarse; FULL: hasta estar llena
    uint16_t minutes = 0;
};

struct BatteryEstimator {
    // Convención del proyecto: batt_power > 0 descargando, < 0 cargando
    static constexpr float ALPHA = 0.05f;   // ≈ 100 s de constante de tiempo con lecturas cada 5 s

    float ema_w = 0;
    bool  have  = false;

    void reset() { ema_w = 0; have = false; }

    // soc en %, batt_power_w con signo, cap_wh = capacidad total de la instalación
    BatteryEta update(int soc, int batt_power_w, uint32_t cap_wh) {
        const float p = (float)batt_power_w;
        if (!have || (p > 0) != (ema_w > 0)) { ema_w = p; have = true; }
        else                                 ema_w += ALPHA * (p - ema_w);

        BatteryEta r;
        const float mag = fabsf(ema_w);
        if (cap_wh == 0 || soc < 0 || soc > 100 || mag < (float)TUNE_BATT_ETA_MIN_W) return r;

        float energy_wh;
        if (ema_w > 0) {                                  // descargando
            const int usable = soc - TUNE_BATT_MIN_SOC;   // por debajo, el inversor corta
            if (usable <= 0) return r;
            energy_wh = usable / 100.0f * (float)cap_wh;
            r.mode = BatteryEta::Mode::EMPTY;
        } else {                                          // cargando
            if (soc >= 100) return r;
            energy_wh = (100 - soc) / 100.0f * (float)cap_wh;
            r.mode = BatteryEta::Mode::FULL;
        }
        const float minutes = energy_wh / mag * 60.0f;
        if (minutes > (float)(TUNE_BATT_ETA_MAX_H * 60)) { r.mode = BatteryEta::Mode::NONE; return r; }
        r.minutes = (uint16_t)(minutes + 0.5f);
        return r;
    }
};

// "45 min", "5h20", "12h" — corto para caber en las tarjetas
inline void battery_eta_format(uint16_t minutes, char* out, size_t n) {
    if (minutes < 60) snprintf(out, n, "%u min", (unsigned)minutes);
    else if (minutes % 60 == 0) snprintf(out, n, "%uh", (unsigned)(minutes / 60));
    else snprintf(out, n, "%uh%02u", (unsigned)(minutes / 60), (unsigned)(minutes % 60));
}

// Empaquetado en un uint32 para publicarlo entre tareas sin mutex (una escritura atómica)
inline uint32_t battery_eta_pack(const BatteryEta& e) { return ((uint32_t)e.mode << 16) | e.minutes; }
inline BatteryEta battery_eta_unpack(uint32_t v) {
    BatteryEta e; e.mode = (BatteryEta::Mode)((v >> 16) & 0xFF); e.minutes = (uint16_t)(v & 0xFFFF); return e;
}
