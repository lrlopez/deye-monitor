#pragma once
#include <stdint.h>

// Capacidad de la batería: la configurada a mano (Wh) o la que el instalador fijó en el inversor
// (registro 204, en Ah), convertida a Wh con la tensión nominal. Lógica pura, probada en nativo.

// Rango verosímil del registro 204 (Ah). Fuera de él el valor se ignora (registro no soportado o basura).
constexpr uint16_t BAT_CAP_AH_MIN = 10;
constexpr uint16_t BAT_CAP_AH_MAX = 3000;

// Tensión nominal de la batería en décimas de voltio: 512 = 51,2 V (LiFePO4 de 16 celdas, lo habitual en
// los híbridos Deye). Una batería de 48 V (15 celdas de NMC) sería 480.
constexpr uint16_t BAT_VNOM_DV_DEF = 512;
constexpr uint16_t BAT_VNOM_DV_MIN = 100;   // 10,0 V
constexpr uint16_t BAT_VNOM_DV_MAX = 1000;  // 100,0 V

inline bool bat_cap_ah_valid(uint16_t ah)    { return ah >= BAT_CAP_AH_MIN && ah <= BAT_CAP_AH_MAX; }
inline bool bat_vnom_dv_valid(uint16_t dv)   { return dv >= BAT_VNOM_DV_MIN && dv <= BAT_VNOM_DV_MAX; }

struct BatCapacity {
    uint32_t wh;             // capacidad efectiva
    bool     from_inverter;  // true si sale del registro 204; false si es la manual
};

// `use_inverter`: el usuario quiere la del inversor. Si aún no se ha leído (ah = 0), no es válida o la
// tensión nominal no es verosímil, se usa la manual.
inline BatCapacity bat_cap_effective(uint16_t manual_wh, bool use_inverter, uint16_t inv_ah, uint16_t vnom_dv) {
    if (use_inverter && bat_cap_ah_valid(inv_ah) && bat_vnom_dv_valid(vnom_dv))
        return { (uint32_t)inv_ah * vnom_dv / 10u, true };
    return { manual_wh, false };
}
