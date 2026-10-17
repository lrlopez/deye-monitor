#pragma once
#include <stdint.h>
#include <math.h>

// Seguimiento de los totales diarios del inversor por DÍA LOCAL (DEYE-01).
//
// Problema que resuelve: los contadores diarios del inversor se ponen a cero a
// medianoche (según SU reloj). Si al cerrar el día se usa "la última lectura",
// puede ser (a) una lectura fallida, (b) ya la del día nuevo con ceros, o (c)
// una de hace 1 h. Aquí se guarda la última lectura BUENA etiquetada con el día
// al que pertenece, y la del día anterior se conserva para poder cerrarlo.
//
// Genérico en T para poder probarse sin depender de solarman.h (DailyStats).
// T debe tener pv_kwh, load_kwh, import_kwh, export_kwh.
template <typename T>
struct DailyTracker {
    T        cur{};            // última lectura buena del día `cur_dep`
    T        prev{};           // última lectura buena del día anterior a cur
    uint32_t cur_dep  = 0;
    uint32_t prev_dep = 0;
    bool     has_cur  = false;
    bool     has_prev = false;

    // Tolerancia para ignorar ruido de redondeo (los contadores son de 0.1 kWh)
    // (no llamarla EPS: es una macro de las cabeceras Xtensa del ESP32)
    static constexpr float NOISE_KWH = 0.05f;

    // Registra una lectura buena tomada durante el día local `dep` (medianoche
    // local). Si `reject_decrease` y algún contador es MENOR que en la última
    // lectura del mismo día, se descarta (devuelve false): es un reinicio
    // anticipado de los contadores porque el reloj del inversor va por delante
    // del nuestro. El llamador lo activa solo en los últimos minutos del día;
    // fuera de esa ventana un descenso se acepta (reinicio tardío tras
    // medianoche o del propio inversor) para no quedarse congelado en datos viejos.
    bool onFetch(const T& s, uint32_t dep, bool reject_decrease) {
        if (reject_decrease && has_cur && dep == cur_dep && decreased(s, cur)) return false;
        if (has_cur && dep != cur_dep) {
            prev = cur; prev_dep = cur_dep; has_prev = true;
        }
        cur = s; cur_dep = dep; has_cur = true;
        return true;
    }

    // Última lectura buena del día `dep`, o nullptr si no hay.
    const T* forDay(uint32_t dep) const {
        if (has_cur && cur_dep == dep)   return &cur;
        if (has_prev && prev_dep == dep) return &prev;
        return nullptr;
    }

private:
    static bool decreased(const T& n, const T& o) {
        return n.pv_kwh     + NOISE_KWH < o.pv_kwh     ||
               n.load_kwh   + NOISE_KWH < o.load_kwh   ||
               n.import_kwh + NOISE_KWH < o.import_kwh ||
               n.export_kwh + NOISE_KWH < o.export_kwh;
    }
};
