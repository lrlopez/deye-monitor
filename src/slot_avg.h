#pragma once
#include <stdint.h>

// DEYE-02: promedia las muestras (cada ~5 s) de un slot de 5 minutos para que el
// registro guardado represente el periodo y no un instante puntual.
struct SlotAverager {
    int32_t sum_pv = 0, sum_grid = 0, sum_batt = 0, sum_load = 0;
    uint16_t n = 0;

    void add(int32_t pv, int32_t grid, int32_t batt, int32_t load) {
        sum_pv += pv; sum_grid += grid; sum_batt += batt; sum_load += load;
        if (n < 0xFFFF) n++;
    }
    void reset() { *this = SlotAverager(); }
    bool empty() const { return n == 0; }

    static int16_t clamp16(int32_t v) {
        return (int16_t)(v > 32767 ? 32767 : (v < -32767 ? -32767 : v));
    }
    // Media redondeada al entero más cercano (importante con signos).
    static int32_t avg(int32_t sum, uint16_t n) {
        return (sum >= 0) ? (sum + n / 2) / n : -((-sum + n / 2) / n);
    }
    int16_t pv()   const { return clamp16(avg(sum_pv,   n)); }
    int16_t grid() const { return clamp16(avg(sum_grid, n)); }
    int16_t batt() const { return clamp16(avg(sum_batt, n)); }
    int16_t load() const { return clamp16(avg(sum_load, n)); }
};
