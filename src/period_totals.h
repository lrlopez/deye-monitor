#pragma once
#include <math.h>
#include "energy_metrics.h"

// Totales de un periodo (mes o año) sumando días, y comparación con el periodo
// equivalente anterior (DEYE-32). Sin dependencias de Arduino; probado en nativo.
struct PeriodTotals {
    float pv = 0, export_kwh = 0, import_kwh = 0, load = 0, charge = 0, discharge = 0;
    int   days = 0;   // días con datos

    void add(float pv_, float exp_, float imp_, float load_, float chg_, float dis_) {
        pv += pv_; export_kwh += exp_; import_kwh += imp_; load += load_; charge += chg_; discharge += dis_;
        days++;
    }
    float autosufficiency() const { return self_sufficiency_pct(load, import_kwh); }
    float autoconsumption() const { return self_consumption_pct(pv, export_kwh); }
    float co2_kg() const { return co2_avoided_kg(pv); }
};

// Variación porcentual de `cur` frente a `prev`; NAN si no hay base de comparación
// (periodo previo sin datos o con total prácticamente 0).
inline float period_change_pct(float cur, float prev) {
    if (prev < 0.5f) return NAN;
    return (cur - prev) / prev * 100.0f;
}
