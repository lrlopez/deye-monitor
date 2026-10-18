#pragma once
#include <Arduino.h>
#include "solarman.h"   // DailyStats
#include "ring_index.h"  // RingState, DayIdxEntry

static constexpr uint32_t STORE_DAYS = 730;

// ── Registro de 5 minutos ─────────────────────────────────────────────────
// 16 bytes exactos — no cambiar orden sin incrementar versión de formato
#pragma pack(push, 1)
struct Record5Min {
    uint32_t timestamp;     // epoch UTC

    // Potencias instantáneas (W) — valores RAW del inversor
    int16_t  pv_w;          // ≥ 0
    int16_t  grid_w;        // + importando / - exportando
    int16_t  batt_w;        // + descargando / - cargando  (RAW)
    int16_t  load_w;        // ≥ 0

    uint8_t  soc;           // %
    uint8_t  flags;         // bit0=válido  bit1=parcial (arranque tras corte)

    int16_t  extra[1];      // reserva para 1 medida futura
};  // = 32 bytes

static_assert(sizeof(Record5Min) == 16, "Record5Min must be 16 bytes");

// ── HourlyRecord — agregación horaria pre-calculada ───────────────────────
struct HourlyRecord {
    uint32_t hour_epoch;     // medianoche local + hora×3600
    int16_t  avg_pv_w;       // media W del periodo
    int16_t  avg_grid_w;
    int16_t  avg_batt_w;
    int16_t  avg_load_w;
    uint8_t  soc_end;        // SOC al final de la hora
    uint8_t  sample_count;   // muestras recibidas (0-12)
    uint8_t  flags;          // bit0=válido bit1=parcial
    uint8_t  _pad1;
    uint16_t day_pv;         // acumulado diario al final de la hora (0.1 kWh)
    uint16_t day_export;
    uint16_t day_import;
    uint16_t day_load;
    uint16_t day_bchg;
    uint16_t day_bdis;
    int16_t  extra[2];       // reserva
};  // 32 bytes
static_assert(sizeof(HourlyRecord) == 32, "");

// ── DailyRecord — totales del día ─────────────────────────────────────────
struct DailyRecord {
    uint32_t day_epoch;      // medianoche local
    uint16_t pv_10wh;        // energía en 0.1 kWh
    uint16_t export_10wh;
    uint16_t import_10wh;
    uint16_t load_10wh;
    uint16_t bchg_10wh;
    uint16_t bdis_10wh;
    uint8_t  soc_start;
    uint8_t  soc_end;
    uint8_t  flags;          // bit0=válido bit1=día completo
    uint8_t  _pad;
    int16_t  extra[6];       // reserva
};  // 32 bytes
static_assert(sizeof(DailyRecord) == 32, "");
#pragma pack(pop)

struct HourAgg {
    float   pv_w;       // media W del periodo
    float   grid_w;
    float   batt_w;
    float   load_w;
    uint8_t soc;        // último valor de la hora
    // Totales acumulados al final de la hora (0.1 kWh)
    uint16_t day_pv, day_export, day_import;
    uint16_t day_load, day_bchg, day_bdis;
    bool    valid;
};

// ── Conversión a DailyStats ───────────────────────────────────────────────
inline DailyStats daily_record_to_stats(const DailyRecord& r) {
    DailyStats d{};
    d.pv_kwh             = r.pv_10wh     / 10.0f;
    d.export_kwh         = r.export_10wh / 10.0f;
    d.import_kwh         = r.import_10wh / 10.0f;
    d.load_kwh           = r.load_10wh   / 10.0f;
    d.batt_charge_kwh    = r.bchg_10wh   / 10.0f;
    d.batt_discharge_kwh = r.bdis_10wh   / 10.0f;
    d.valid = (r.flags & 0x01);
    return d;
}


struct FakeFile { operator bool() const { return false; } void close() {} };
struct FakeFS { FakeFile open(const char*, const char*) { return {}; } };
extern FakeFS LittleFS;
typedef FakeFile File;
struct FakeStore { void flush() {} DailyRecord getCurrentDaily() { DailyRecord r{}; return r; } };
extern FakeStore g_store;
#define Store g_store
