#include "recorder.h"
#include "app_state.h"
#include "data_store.h"
#include "psram_cache.h"
#include "safe_serial.h"

static inline uint16_t kwh_to_10wh(float kwh) { return (uint16_t)(kwh * 10.0f + 0.5f); }

// Medianoche local del día de `t`
static uint32_t local_midnight_of(time_t t) {
    struct tm tm_r; localtime_r(&t, &tm_r);
    tm_r.tm_hour = 0; tm_r.tm_min = 0; tm_r.tm_sec = 0; tm_r.tm_isdst = -1;
    return (uint32_t)mktime(&tm_r);
}

// DailyRecord a partir de una lectura diaria del inversor
static DailyRecord daily_from_stats(uint32_t dep, const DailyStats& d,
                                    uint8_t soc_start, uint8_t soc_end, uint8_t flags) {
    DailyRecord dr{};
    dr.day_epoch   = dep;
    dr.pv_10wh     = kwh_to_10wh(d.pv_kwh);
    dr.export_10wh = kwh_to_10wh(d.export_kwh);
    dr.import_10wh = kwh_to_10wh(d.import_kwh);
    dr.load_10wh   = kwh_to_10wh(d.load_kwh);
    dr.bchg_10wh   = kwh_to_10wh(d.batt_charge_kwh);
    dr.bdis_10wh   = kwh_to_10wh(d.batt_discharge_kwh);
    dr.soc_start   = soc_start;
    dr.soc_end     = soc_end;
    dr.flags       = flags;
    return dr;
}

void Recorder::snapFrom(const DailyStats& d) {
    _snap_pv   = kwh_to_10wh(d.pv_kwh);
    _snap_exp  = kwh_to_10wh(d.export_kwh);
    _snap_imp  = kwh_to_10wh(d.import_kwh);
    _snap_load = kwh_to_10wh(d.load_kwh);
    _snap_bchg = kwh_to_10wh(d.batt_charge_kwh);
    _snap_bdis = kwh_to_10wh(d.batt_discharge_kwh);
    _snap_valid = true;
}

// ── Lectura diaria buena ──────────────────────────────────────────────────
void Recorder::onDailyFetched(const DailyStats& d, uint32_t now_ep, bool reject_decrease) {
    // DEYE-01: guardar la lectura buena etiquetada con su día local
    if (now_ep > 1700000000UL)
        _dtrack.onFetch(d, local_midnight_of((time_t)now_ep), reject_decrease);

    // Inicialización tardía de snaps si el arranque ocurrió sin datos diarios
    if (_started && !_snap_valid) {
        snapFrom(d);
        APP_LOGI("Record", "Snaps diarios inicializados tardíamente");
    }
}

// ── Lectura de energía ────────────────────────────────────────────────────
Recorder::Result Recorder::onSample(const EnergyData& e, const DailyStats& last_daily,
                                    uint32_t now_ep, bool solar_active) {
    Result res;
    if (!e.valid) return res;

    time_t t = (time_t)now_ep;
    struct tm now_tm; localtime_r(&t, &now_tm);

    // Arranque: inicializar el estado desde la primera lectura válida. Los datos
    // diarios son opcionales aquí; si aún no hay se inicializarán al llegar
    // (ver onDailyFetched).
    if (!_started) {
        startup(e, last_daily, now_tm, now_ep);
        res.startup_done_now = true;
        return res;
    }

    const int new_hour = now_tm.tm_hour;
    const int new_day  = now_tm.tm_mday;

    _acc_pv   += e.pv_power;
    _acc_grid += e.grid_power;
    _acc_batt += e.batt_power;
    _acc_load += e.load_power;
    _acc_soc   = (uint8_t)e.batt_soc;
    _acc_n++;

    // Nuevo slot de 5 minutos
    const uint32_t slot_5min = now_ep / 300;
    if ((int32_t)slot_5min != _cur_5min_slot) {
        _cur_5min_slot = (int32_t)slot_5min;
        closeSlot(e, slot_5min * 300, solar_active);
    }
    // La muestra actual pertenece al slot en curso (se añade tras cerrar el
    // anterior para que no contamine su media)
    _slot.add(e.pv_power, e.grid_power, e.batt_power, e.load_power);

    if (new_hour != _cur_hour) closeHour(e, now_tm, new_hour);

    if (new_day != _cur_day) {
        closeDay(e, now_tm);
        res.day_changed = true;
    }
    return res;
}

// ── Arranque ──────────────────────────────────────────────────────────────
void Recorder::startup(const EnergyData& e, const DailyStats& d,
                       const struct tm& now_tm, uint32_t now_ep) {
    _started       = true;
    _cur_5min_slot = (int32_t)(now_ep / 300);
    _cur_hour      = now_tm.tm_hour;
    _cur_day       = now_tm.tm_mday;

    // DEYE-01: tras un reinicio a mitad de hora, sembrar el acumulador horario con
    // los registros de 5 min ya guardados en esta hora. Cada uno es la media de ~60
    // muestras de 5 s, de ahí el peso 60: así el HourlyRecord no se calcula solo
    // con lo posterior al arranque.
    {
        struct tm hs = now_tm;
        hs.tm_min = 0; hs.tm_sec = 0; hs.tm_isdst = -1;
        const uint32_t hour_start = (uint32_t)mktime(&hs);
        const uint32_t dep_today  = local_midnight_of((time_t)now_ep);
        uint32_t cnt = 0, seeded = 0;
        Cache.lock();
        const Record5Min* rr = Cache.getRawDay(dep_today, cnt);
        for (uint32_t i = 0; rr && i < cnt; i++) {
            // El registro con ts = hour_start cubre la hora anterior
            if (rr[i].timestamp <= hour_start || rr[i].timestamp > now_ep) continue;
            _acc_pv   += rr[i].pv_w   * 60.0f;
            _acc_grid += rr[i].grid_w * 60.0f;
            _acc_batt += rr[i].batt_w * 60.0f;
            _acc_load += rr[i].load_w * 60.0f;
            _acc_n    += 60;
            seeded++;
        }
        Cache.unlock();
        if (seeded)
            APP_LOGI("Record", "Acumulador horario sembrado con %lu registros de 5 min",
                     (unsigned long)seeded);
    }

    if (d.valid) snapFrom(d);
    _soc_start_of_day = (uint8_t)e.batt_soc;

    // Publicar en cache el primer valor del día
    {
        DailyRecord dr{};
        dr.day_epoch   = local_midnight_of((time_t)now_ep);
        dr.pv_10wh     = _snap_pv;
        dr.export_10wh = _snap_exp;
        dr.import_10wh = _snap_imp;
        dr.load_10wh   = _snap_load;
        dr.bchg_10wh   = _snap_bchg;
        dr.bdis_10wh   = _snap_bdis;
        dr.soc_start   = _soc_start_of_day;
        dr.soc_end     = (uint8_t)e.batt_soc;
        dr.flags       = 0x01;
        FlashGuard g;
        Cache.pushDaily(dr);
    }

    APP_LOGI("Record", "Startup ok: slot=%lu h=%d d=%d",
             (unsigned long)(now_ep / 300), (int)_cur_hour, (int)_cur_day);
}

// ── Cierre del slot de 5 minutos ──────────────────────────────────────────
void Recorder::closeSlot(const EnergyData& e, uint32_t record_ts, bool solar_active) {
    APP_LOGD("Record", "Slot: ts=%lu", (unsigned long)record_ts);

    // DEYE-02: valor medio de las muestras del periodo (no el instante del cambio
    // de slot). Si por algún motivo no hay muestras se usa la actual.
    Record5Min r{};
    r.timestamp = record_ts;
    if (!_slot.empty()) {
        r.pv_w   = _slot.pv();
        r.grid_w = _slot.grid();
        r.batt_w = _slot.batt();
        r.load_w = _slot.load();
    } else {
        r.pv_w   = (int16_t)constrain(e.pv_power,  -32767, 32767);
        r.grid_w = (int16_t)constrain(e.grid_power, -32767, 32767);
        r.batt_w = (int16_t)constrain(e.batt_power, -32767, 32767);
        r.load_w = (int16_t)constrain(e.load_power, -32767, 32767);
    }
    _slot.reset();
    r.soc   = (uint8_t)e.batt_soc;
    r.flags = 0x01;

    bool push_ok;
    {
        // Serializar con el flush del display en Core 1 (ver g_flash_display_mutex)
        FlashGuard g;
        push_ok = Store.push(r);
        // DEYE-03: NVS tiene ciclos de escritura limitados; el estado de sesión solo
        // se usa para comparar el día al arrancar, así que basta guardarlo cada hora
        // (los cambios de estado solar ya lo guardan).
        if (record_ts - _session_saved_ep >= 3600) {
            Storage.saveSessionState({record_ts, true, solar_active});
            _session_saved_ep = record_ts;
        }
    }
    if (push_ok) Cache.pushRaw(r);
}

// ── Cierre de la hora ─────────────────────────────────────────────────────
void Recorder::closeHour(const EnergyData& e, const struct tm& now_tm, int new_hour) {
    // Hora que se cierra y día local al que pertenece (a las 00:00 es AYER)
    struct tm mid = now_tm;
    if (new_hour == 0) mid.tm_mday--;   // mktime() normaliza mday=0 al último día del mes anterior
    mid.tm_hour = _cur_hour;
    mid.tm_min = 0; mid.tm_sec = 0; mid.tm_isdst = -1;
    struct tm mid_day = mid;
    mid_day.tm_hour = 0; mid_day.tm_isdst = -1;
    const uint32_t mid_dep = (uint32_t)mktime(&mid_day);

    // DEYE-01: totales diarios de ESE día (última lectura buena), no la última
    // lectura a secas: a medianoche puede ser un fallo o ya la del día nuevo con
    // los contadores a cero.
    const DailyStats* hd = _dtrack.forDay(mid_dep);

    if (_acc_n > 0 && _snap_valid && hd) {
        const uint16_t cur_pv   = kwh_to_10wh(hd->pv_kwh);
        const uint16_t cur_exp  = kwh_to_10wh(hd->export_kwh);
        const uint16_t cur_imp  = kwh_to_10wh(hd->import_kwh);
        const uint16_t cur_load = kwh_to_10wh(hd->load_kwh);
        const uint16_t cur_bchg = kwh_to_10wh(hd->batt_charge_kwh);
        const uint16_t cur_bdis = kwh_to_10wh(hd->batt_discharge_kwh);

        HourlyRecord hr{};
        hr.hour_epoch   = (uint32_t)mktime(&mid);
        hr.avg_pv_w     = (int16_t)(_acc_pv   / _acc_n);
        hr.avg_grid_w   = (int16_t)(_acc_grid / _acc_n);
        hr.avg_batt_w   = (int16_t)(_acc_batt / _acc_n);
        hr.avg_load_w   = (int16_t)(_acc_load / _acc_n);
        hr.soc_end      = _acc_soc;
        hr.sample_count = (uint8_t)min(_acc_n, 255);
        hr.flags        = 0x01;
        hr.day_pv       = cur_pv;
        hr.day_export   = cur_exp;
        hr.day_import   = cur_imp;
        hr.day_load     = cur_load;
        hr.day_bchg     = cur_bchg;
        hr.day_bdis     = cur_bdis;
        {
            FlashGuard g;
            Cache.pushHourly(hr);
        }

        _snap_pv   = cur_pv;   _snap_exp  = cur_exp;
        _snap_imp  = cur_imp;  _snap_load = cur_load;
        _snap_bchg = cur_bchg; _snap_bdis = cur_bdis;
    }

    _acc_pv = _acc_grid = _acc_batt = _acc_load = 0;
    _acc_n  = 0;
    _cur_hour = new_hour;

    // DailyRecord parcial al cambio de hora (requiere una lectura diaria buena de
    // HOY: a las 00:00 aún puede ser la de ayer)
    struct tm today_tm = now_tm;
    today_tm.tm_hour = 0; today_tm.tm_min = 0;
    today_tm.tm_sec  = 0; today_tm.tm_isdst = -1;
    const uint32_t dep_now = (uint32_t)mktime(&today_tm);
    const DailyStats* td = _dtrack.forDay(dep_now);
    if (td) {
        DailyRecord dr = daily_from_stats(dep_now, *td, _soc_start_of_day,
                                          (uint8_t)e.batt_soc, 0x01);
        FlashGuard g;
        Cache.pushDaily(dr);
    }
}

// ── Cierre del día ────────────────────────────────────────────────────────
void Recorder::closeDay(const EnergyData& e, const struct tm& now_tm) {
    struct tm yesterday = now_tm;
    yesterday.tm_mday--;   // mktime() normaliza mday=0 al último día del mes anterior
    yesterday.tm_hour = 0; yesterday.tm_min = 0;
    yesterday.tm_sec  = 0; yesterday.tm_isdst = -1;
    const uint32_t dep_yesterday = (uint32_t)mktime(&yesterday);

    HourlyRecord last_hr{};
    bool got_hr;
    {
        FlashGuard g;
        got_hr = Store.getLastHourly(dep_yesterday, last_hr);
    }

    // DEYE-01: cerrar el día con la última lectura diaria BUENA de ayer (como mucho
    // unos segundos antes de medianoche); solo si no la hay se recurre al último
    // registro horario, que puede estar hasta 1 h atrás.
    const DailyStats* yd = _dtrack.forDay(dep_yesterday);
    if (yd || got_hr) {
        const uint8_t soc_end = got_hr ? last_hr.soc_end : _acc_soc;
        DailyRecord dr{};
        if (yd) {
            dr = daily_from_stats(dep_yesterday, *yd, _soc_start_of_day, soc_end, 0x03);
        } else {
            dr.day_epoch   = dep_yesterday;
            dr.pv_10wh     = last_hr.day_pv;
            dr.export_10wh = last_hr.day_export;
            dr.import_10wh = last_hr.day_import;
            dr.load_10wh   = last_hr.day_load;
            dr.bchg_10wh   = last_hr.day_bchg;
            dr.bdis_10wh   = last_hr.day_bdis;
            dr.soc_start   = _soc_start_of_day;
            dr.soc_end     = soc_end;
            dr.flags       = 0x03;
        }
        FlashGuard g;
        Cache.pushDaily(dr);
    }

    _soc_start_of_day = (uint8_t)e.batt_soc;
    _cur_day = now_tm.tm_mday;
}
