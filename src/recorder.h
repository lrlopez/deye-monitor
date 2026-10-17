#pragma once
#include <Arduino.h>
#include <time.h>
#include "solarman.h"
#include "daily_tracker.h"
#include "slot_avg.h"

// Grabación del histórico (extraído de solarmanTask — DEYE-41).
//
// Recibe una lectura cada ~5 s y se encarga de:
//   · registro de 5 min (media de las muestras del slot)      → Store + Cache
//   · registro horario al cambiar de hora (con acumulados)    → Cache
//   · registro diario parcial cada hora y completo a medianoche
// Todo el estado que antes eran variables `static` dentro de la tarea vive aquí.
class Recorder {
public:
    struct Result {
        bool startup_done_now = false;   // primera lectura procesada (arranque completado)
        bool day_changed      = false;   // se ha cerrado el día
    };

    // Llamar cuando llega una lectura diaria BUENA. `reject_decrease`: estamos en
    // los últimos minutos del día (ver DailyTracker). now_ep debe ser válido (NTP).
    void onDailyFetched(const DailyStats& d, uint32_t now_ep, bool reject_decrease);

    // Procesa una lectura de energía. `last_daily`: la última lectura diaria
    // conocida (puede no ser válida). `solar_active` se guarda en NVS junto al
    // registro de sesión. Requiere now_ep válido (NTP sincronizado).
    Result onSample(const EnergyData& e, const DailyStats& last_daily,
                    uint32_t now_ep, bool solar_active);

    bool started() const { return _started; }

private:
    DailyTracker<DailyStats> _dtrack;

    bool    _started       = false;
    int32_t _cur_5min_slot = -1;
    int32_t _cur_hour      = -1;
    int32_t _cur_day       = -1;

    // Acumulador de la hora en curso (potencias; los registros de 5 min ya
    // guardados se vuelcan aquí tras un reinicio — ver startup())
    float   _acc_pv = 0, _acc_grid = 0, _acc_batt = 0, _acc_load = 0;
    int     _acc_n   = 0;
    uint8_t _acc_soc = 0;

    SlotAverager _slot;                 // media del slot de 5 min en curso
    uint32_t _session_saved_ep = 0;     // último guardado de la sesión en NVS

    // Acumulados diarios (0.1 kWh) al cerrar la última hora
    uint16_t _snap_pv = 0, _snap_exp = 0, _snap_imp = 0;
    uint16_t _snap_load = 0, _snap_bchg = 0, _snap_bdis = 0;
    bool     _snap_valid = false;

    uint8_t  _soc_start_of_day = 0;

    void startup(const EnergyData& e, const DailyStats& d, const struct tm& now_tm, uint32_t now_ep);
    void closeSlot(const EnergyData& e, uint32_t record_ts, bool solar_active);
    void closeHour(const EnergyData& e, const struct tm& now_tm, int new_hour);
    void closeDay(const EnergyData& e, const struct tm& now_tm);
    void snapFrom(const DailyStats& d);
};
