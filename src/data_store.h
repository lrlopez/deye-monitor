#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>
#include <LittleFS.h>
#include "solarman.h"   // DailyStats

static constexpr uint32_t STORE_DAYS = 1461;

// ── Registro de 5 minutos ─────────────────────────────────────────────────
// 32 bytes exactos — no cambiar orden sin incrementar versión de formato
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

// ── Buffer circular en LittleFS ───────────────────────────────────────────
class DataStore {
public:
    static DataStore& instance();

    bool begin();

    // ── Raw 5 min ─────────────────────────────────────────────────────────
    bool     push(const Record5Min& r);
    uint32_t readDay(uint32_t day_epoch, Record5Min* out, uint32_t max);
    bool     getLastRecord(Record5Min& out);
    uint32_t getLastRawDayEpoch() const;   // epoch medianoche del último día con datos 5-min

    // ── Hourly ────────────────────────────────────────────────────────────
    // Finaliza la hora anterior y escribe el HourlyRecord
    bool     pushHourly(const HourlyRecord& r);
    // Lee las 24 horas de un día (out debe tener espacio para 24 elementos)
    // Devuelve número de horas con datos
    uint8_t  readHourly(uint32_t day_epoch, HourlyRecord out[24]);
    bool     getLastHourly(uint32_t day_epoch, HourlyRecord& out);

    // ── Daily ─────────────────────────────────────────────────────────────
    bool     pushDaily(const DailyRecord& r);
    bool     readDaily(uint32_t day_epoch, DailyRecord& out);
    // Todos los registros diarios ordenados de más antiguo a más reciente
    uint32_t readAllDaily(DailyRecord* out, uint32_t max);

    // ── Capacidad ─────────────────────────────────────────────────────────
    uint32_t getRawCount()    const { return _raw_count;    }
    uint32_t getHourlyCount() const { return _hrly_count;   }
    uint32_t getDailyCount()  const { return _day_count;    }

private:
    DataStore() = default;

    SemaphoreHandle_t _mutex = nullptr;

    // Tres buffers circulares independientes
    struct CircBuf {
        const char* path;
        uint32_t    capacity;
        uint32_t    head;
        uint32_t    count;
    };

    CircBuf  _raw  {"/raw.bin",  STORE_DAYS * 288, 0, 0};  // STORE_DAYS días × 288
    CircBuf  _hrly {"/hrly.bin",  STORE_DAYS * 24, 0, 0};  // STORE_DAYS días × 24
    CircBuf  _day  {"/day.bin",        STORE_DAYS, 0, 0};  // STORE_DAYS días

    uint32_t& _raw_count  = _raw.count;
    uint32_t& _hrly_count = _hrly.count;
    uint32_t& _day_count  = _day.count;
    
    // File handle del buffer raw (abierto permanentemente para rendimiento)
    File _f_raw;

    // Índice de días en PSRAM — guarda posición FÍSICA (inmutable) en lugar de
    // lógica, para que no se desfase cuando head avanza al envolver el anillo.
    struct DayIdx { uint32_t day_epoch; uint32_t phys_start; };
    DayIdx*  _day_idx       = nullptr;
    uint32_t _day_idx_count  = 0;
    static constexpr uint32_t DAY_IDX_MAX = STORE_DAYS;

    // Meta en RAM, guardada solo periódicamente
    uint32_t _pushes_since_meta_save = 0;
    static constexpr uint32_t META_SAVE_INTERVAL = 1;

    static const char* META_FILE;
    static const uint32_t MAGIC   = 0x5A5ADEA2;
    static const uint8_t  VERSION = 4;           // versión 3: optimizado

    bool loadMeta();
    bool saveMeta();

    bool writeAt(const CircBuf& cb, uint32_t phys, const void* data, size_t sz);
    bool readAt (const CircBuf& cb, uint32_t phys, void* data,       size_t sz);

    bool writeRaw(uint32_t phys, const Record5Min& r);
    bool readRaw (uint32_t phys, Record5Min& r);

    void   _day_idx_insert(uint32_t day_epoch, uint32_t phys_start);
    int    _day_idx_find  (uint32_t day_epoch) const;
    void   _day_idx_load  ();
    uint32_t physIdx(const CircBuf& cb, uint32_t logical) const;
    uint32_t lowerBoundHrly(uint32_t ts);
};

#define Store DataStore::instance()
