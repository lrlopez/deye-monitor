#pragma once
#include "tuning.h"

// Indicadores derivados de los totales diarios (DEYE-35): autosuficiencia, autoconsumo y
// CO2 evitado. Sin dependencias de Arduino; probado en `pio test -e native`. Los usan la
// pantalla Estadísticas, el bot de Telegram y la web.

// % del consumo cubierto sin importar de la red (cualquier origen: solar directo + batería).
// Sin consumo apreciable no tiene sentido: devuelve -1 para que la UI muestre «--».
inline float self_sufficiency_pct(float load_kwh, float import_kwh) {
    if (load_kwh < 0.05f) return -1.0f;
    float p = (load_kwh - import_kwh) / load_kwh * 100.0f;
    return p < 0 ? 0 : (p > 100 ? 100 : p);
}

// % de la producción solar consumida en casa (no exportada). -1 sin producción apreciable.
inline float self_consumption_pct(float pv_kwh, float export_kwh) {
    if (pv_kwh < 0.05f) return -1.0f;
    float p = (pv_kwh - export_kwh) / pv_kwh * 100.0f;
    return p < 0 ? 0 : (p > 100 ? 100 : p);
}

// CO2 que habría emitido la red para generar la energía solar producida: producción × factor
// de emisión (kg CO2/kWh del mix eléctrico; TUNE_CO2_KG_PER_KWH, ajustable con -D).
inline float co2_avoided_kg(float pv_kwh, float factor_kg_per_kwh = TUNE_CO2_KG_PER_KWH) {
    return pv_kwh < 0 ? 0.0f : pv_kwh * factor_kg_per_kwh;
}
