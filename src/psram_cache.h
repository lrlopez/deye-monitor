#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>
#include "data_store.h"

// ── Raw cache ─────────────────────────────────────────────────────────────
constexpr int    CACHE_RAW_DAYS   = 90;
constexpr int    RECS_PER_DAY     = 288;
constexpr size_t CACHE_RAW_SIZE   = (size_t)CACHE_RAW_DAYS * RECS_PER_DAY * sizeof(Record5Min);

// ── Hourly cache ──────────────────────────────────────────────────────────
constexpr int    CACHE_HRLY_DAYS  = STORE_DAYS;
constexpr size_t CACHE_HRLY_SIZE  = (size_t)CACHE_HRLY_DAYS * 24 * sizeof(HourlyRecord);

// ── Daily cache ───────────────────────────────────────────────────────────
constexpr int    CACHE_DAY_MAX    = STORE_DAYS;
constexpr size_t CACHE_DAY_SIZE   = (size_t)CACHE_DAY_MAX * sizeof(DailyRecord);

struct CachedRawDay {
    uint32_t day_epoch;
    uint16_t count;
    bool     valid;
    bool     loaded;
    Record5Min last;
};

struct CachedHrlyDay {
    uint32_t    day_epoch;
    bool        valid;
    bool        loaded;
    uint8_t     hours_valid;   // cuántas horas tienen datos
    HourlyRecord hours[24];
};

class PsramCache {
public:
    static PsramCache& instance();

    bool begin();

    // ── Raw ───────────────────────────────────────────────────────────────
    const Record5Min* getRawDay(uint32_t dep, uint32_t& count_out);
    bool getLastRaw(uint32_t dep, Record5Min& out);
    void pushRaw(const Record5Min& r);

    // ── Hourly (acceso O(1) desde PSRAM) ──────────────────────────────────
    // Devuelve puntero directo a array de 24 HourlyRecord en PSRAM
    // null si no hay datos para ese día
    const HourlyRecord* getHourly(uint32_t dep);
    void pushHourly(const HourlyRecord& r);
    void invalidateHourly(uint32_t dep);   // forzar recarga desde flash

    // ── Daily (toda la historia en PSRAM, acceso O(1)) ────────────────────
    bool getDaily(uint32_t dep, DailyRecord& out);
    void pushDaily(const DailyRecord& r);

    // Para stats/summary: acceso directo al array ordenado
    const DailyRecord* getDailyArray() const { return _day_buf; }
    uint32_t           getDailyCount()  const { return _day_count; }
    uint32_t           getOldestDailyEpoch() const;

    bool dayHasData(uint32_t day_epoch) const;

    // Llamar una vez tras la sincronización NTP para corregir los epochs de los
    // slots que se inicializaron en begin() con el reloj aún en 1970.
    void reinitAfterNtp();

    void lock()   { xSemaphoreTake(_mutex, portMAX_DELAY); }
    void unlock() { xSemaphoreGive(_mutex); }
    void printStats();

private:
    PsramCache() = default;

    static constexpr uint32_t BITMAP_DAYS = STORE_DAYS;

    SemaphoreHandle_t _mutex = nullptr;

    // Raw
    Record5Min*    _raw_buf  = nullptr;
    CachedRawDay*  _raw_days = nullptr;

    // Hourly
    HourlyRecord*  _hrly_buf  = nullptr;
    CachedHrlyDay* _hrly_days = nullptr;

    // Daily (array plano ordenado cronológicamente)
    DailyRecord*   _day_buf   = nullptr;
    uint32_t       _day_count = 0;

    uint8_t* _has_data_bitmap = nullptr;   // Bytes en PSRAM, 1 bit por día

    int  _raw_slot_for(uint32_t dep)  const;
    int  _raw_oldest_slot()           const;
    void _raw_load(uint32_t dep);

    int  _hrly_slot_for(uint32_t dep) const;
    int  _hrly_oldest_slot()          const;
    void _hrly_load(uint32_t dep);

    void _day_load_all();
    int  _day_find(uint32_t dep) const;

    void _bitmap_set(uint32_t day_epoch, bool has);
    void _bitmap_build();

    static void _bg_task(void* pv);
};

#define Cache PsramCache::instance()
