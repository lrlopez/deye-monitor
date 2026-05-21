#include "psram_cache.h"
#include <time.h>
#include <string.h>

PsramCache& PsramCache::instance() { static PsramCache c; return c; }

// ═══════════════════════════════════════════════════════════════════════════
// begin()
// ═══════════════════════════════════════════════════════════════════════════
bool PsramCache::begin() {
    _mutex = xSemaphoreCreateRecursiveMutex();

    // ── Alojar buffers en PSRAM ───────────────────────────────────────────
    _raw_buf   = (Record5Min*)  heap_caps_malloc(CACHE_RAW_SIZE,
                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    _raw_days  = (CachedRawDay*)heap_caps_malloc(
                     CACHE_RAW_DAYS * sizeof(CachedRawDay),
                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    _hrly_buf  = (HourlyRecord*)heap_caps_malloc(CACHE_HRLY_SIZE,
                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    _hrly_days = (CachedHrlyDay*)heap_caps_malloc(
                     CACHE_HRLY_DAYS * sizeof(CachedHrlyDay),
                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    _day_buf   = (DailyRecord*) heap_caps_malloc(CACHE_DAY_SIZE,
                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!_raw_buf || !_raw_days || !_hrly_buf || !_hrly_days || !_day_buf) {
        DBGSERIAL.println("[Cache] ERROR: PSRAM insuficiente");
        return false;
    }

    memset(_raw_buf,   0, CACHE_RAW_SIZE);
    memset(_raw_days,  0, CACHE_RAW_DAYS  * sizeof(CachedRawDay));
    memset(_hrly_buf,  0, CACHE_HRLY_SIZE);
    memset(_hrly_days, 0, CACHE_HRLY_DAYS * sizeof(CachedHrlyDay));
    memset(_day_buf,   0, CACHE_DAY_SIZE);

    DBGSERIAL.printf("[Cache] PSRAM: raw=%uKB hrly=%uKB day=%uKB total=%uKB libre=%luKB\n",
                  (unsigned)(CACHE_RAW_SIZE/1024),
                  (unsigned)(CACHE_HRLY_SIZE/1024),
                  (unsigned)(CACHE_DAY_SIZE/1024),
                  (unsigned)((CACHE_RAW_SIZE+CACHE_HRLY_SIZE+CACHE_DAY_SIZE)/1024),
                  (unsigned long)ESP.getFreePsram()/1024);

    // ── Inicializar slots raw (últimos 90 días) ───────────────────────────
    time_t now; time(&now);
    struct tm tm_now; localtime_r(&now, &tm_now);
    tm_now.tm_hour = 0; tm_now.tm_min = 0; tm_now.tm_sec = 0; tm_now.tm_isdst = -1;
    uint32_t today = (uint32_t)mktime(&tm_now);

    for (int i = 0; i < CACHE_RAW_DAYS; i++) {
        _raw_days[i].day_epoch = today - (uint32_t)(CACHE_RAW_DAYS-1-i)*86400;
        _raw_days[i].loaded    = false;
        _raw_days[i].valid     = false;
    }

    // ── Inicializar slots hourly (todos los días disponibles) ─────────────
    for (int i = 0; i < CACHE_HRLY_DAYS; i++) {
        _hrly_days[i].day_epoch = today - (uint32_t)(CACHE_HRLY_DAYS-1-i)*86400;
        _hrly_days[i].loaded    = false;
        _hrly_days[i].valid     = false;
    }

    // ── Carga eager: daily (pequeño, 47 KB) + últimos 7 días raw + hrly ───
    _day_load_all();   // incluye _bitmap_build() internamente

    // Últimos 7 días en raw y hourly (para UI inmediata)
    for (int i = CACHE_RAW_DAYS-7; i < CACHE_RAW_DAYS; i++)
        _raw_load(_raw_days[i].day_epoch);
    for (int i = CACHE_HRLY_DAYS-7; i < CACHE_HRLY_DAYS; i++)
        _hrly_load(_hrly_days[i].day_epoch);

    // ── Carga en background de toda la historia horaria ───────────────────
    // Prioridad mínima, Core 0, para no interferir con la UI
    xTaskCreatePinnedToCore(_bg_task, "cache_bg", 4096, this, 0, nullptr, 0);

    printStats();
    return true;
}

void PsramCache::_bg_task(void* pv) {
    PsramCache* self = static_cast<PsramCache*>(pv);
    DBGSERIAL.println("[Cache] Background: cargando historia horaria...");

    uint32_t loaded = 0;
    for (int i = 0; i < CACHE_HRLY_DAYS - 7; i++) {
        self->lock();
        if (!self->_hrly_days[i].loaded) {
            self->_hrly_load(self->_hrly_days[i].day_epoch);
            loaded++;
        }
        self->unlock();
        vTaskDelay(pdMS_TO_TICKS(20));   // ceder para que push() pueda adquirir el mutex
    }

    uint32_t valid = 0;
    self->lock();
    for (int i = 0; i < CACHE_HRLY_DAYS; i++)
        if (self->_hrly_days[i].valid) valid++;
    self->unlock();

    DBGSERIAL.printf("[Cache] Background completado: %lu/%d días con datos horarios\n",
                  (unsigned long)valid, CACHE_HRLY_DAYS);
    vTaskDelete(nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════
// Daily cache — load all / find / push / get
// ═══════════════════════════════════════════════════════════════════════════
void PsramCache::_day_load_all() {
    _day_count = Store.readAllDaily(_day_buf, CACHE_DAY_MAX);
    DBGSERIAL.printf("[Cache] Daily cargado: %lu registros\n",
                  (unsigned long)_day_count);
    _has_data_bitmap = (uint8_t*)heap_caps_malloc(
        92, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (_has_data_bitmap) {
        memset(_has_data_bitmap, 0, 92);
        _bitmap_build();
    }
}

int PsramCache::_day_find(uint32_t dep) const {
    // Búsqueda lineal (730 elementos, trivial)
    for (uint32_t i = 0; i < _day_count; i++)
        if (_day_buf[i].day_epoch == dep) return (int)i;
    return -1;
}

bool PsramCache::getDaily(uint32_t dep, DailyRecord& out) {
    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    int idx = _day_find(dep);
    bool ok = (idx >= 0 && (_day_buf[idx].flags & 0x01));
    if (ok) out = _day_buf[idx];
    xSemaphoreGiveRecursive(_mutex);
    return ok;
}

void PsramCache::pushDaily(const DailyRecord& r) {
    Store.pushDaily(r);

    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    int idx = _day_find(r.day_epoch);
    if (idx >= 0) {
        _day_buf[idx] = r;
    } else if (_day_count < CACHE_DAY_MAX) {
        _day_buf[_day_count++] = r;
    } else {
        memmove(_day_buf, _day_buf + 1, (_day_count-1) * sizeof(DailyRecord));
        _day_buf[_day_count-1] = r;
    }
    _bitmap_set(r.day_epoch, r.flags & 0x01);
    xSemaphoreGiveRecursive(_mutex);
}

// ═══════════════════════════════════════════════════════════════════════════
// Hourly cache
// ═══════════════════════════════════════════════════════════════════════════
int PsramCache::_hrly_slot_for(uint32_t dep) const {
    for (int i = 0; i < CACHE_HRLY_DAYS; i++)
        if (_hrly_days[i].day_epoch == dep) return i;
    return -1;
}

int PsramCache::_hrly_oldest_slot() const {
    int oldest = 0;
    for (int i = 1; i < CACHE_HRLY_DAYS; i++)
        if (_hrly_days[i].day_epoch < _hrly_days[oldest].day_epoch) oldest = i;
    return oldest;
}

void PsramCache::_hrly_load(uint32_t dep) {
    int slot = _hrly_slot_for(dep);
    if (slot < 0) {
        slot = _hrly_oldest_slot();
        _hrly_days[slot].day_epoch   = dep;
        _hrly_days[slot].valid       = false;
        _hrly_days[slot].hours_valid = 0;
    }
    HourlyRecord* dst = _hrly_buf + slot * 24;
    uint8_t n = Store.readHourly(dep, dst);
    _hrly_days[slot].loaded      = true;
    _hrly_days[slot].valid       = (n > 0);
    _hrly_days[slot].hours_valid = n;
}

const HourlyRecord* PsramCache::getHourly(uint32_t dep) {
    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    int slot = _hrly_slot_for(dep);
    if (slot < 0 || !_hrly_days[slot].loaded) {
        _hrly_load(dep);
        slot = _hrly_slot_for(dep);
    }
    const HourlyRecord* result = nullptr;
    if (slot >= 0 && _hrly_days[slot].valid)
        result = _hrly_buf + slot * 24;
    xSemaphoreGiveRecursive(_mutex);
    return result;
}

void PsramCache::pushHourly(const HourlyRecord& r) {
    Store.pushHourly(r);

    time_t t = (time_t)r.hour_epoch;
    struct tm tm_r; localtime_r(&t, &tm_r);
    tm_r.tm_hour = 0; tm_r.tm_min = 0; tm_r.tm_sec = 0; tm_r.tm_isdst = -1;
    uint32_t dep = (uint32_t)mktime(&tm_r);
    int h        = (int)((r.hour_epoch - dep) / 3600);
    if (h < 0 || h >= 24) return;

    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    int slot = _hrly_slot_for(dep);
    if (slot < 0) {
        slot = _hrly_oldest_slot();
        _hrly_days[slot].day_epoch   = dep;
        _hrly_days[slot].valid       = false;
        _hrly_days[slot].hours_valid = 0;
        _hrly_days[slot].loaded      = true;
        memset(_hrly_buf + slot*24, 0, 24*sizeof(HourlyRecord));
    }
    _hrly_buf[slot*24 + h]  = r;
    _hrly_days[slot].valid  = true;
    if (_hrly_days[slot].hours_valid < 24) _hrly_days[slot].hours_valid++;
    xSemaphoreGiveRecursive(_mutex);
}

void PsramCache::invalidateHourly(uint32_t dep) {
    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    int slot = _hrly_slot_for(dep);
    if (slot >= 0) { _hrly_days[slot].loaded = false; _hrly_days[slot].valid = false; }
    xSemaphoreGiveRecursive(_mutex);
}

// ═══════════════════════════════════════════════════════════════════════════
// Raw cache (igual que antes, simplificado)
// ═══════════════════════════════════════════════════════════════════════════
int PsramCache::_raw_slot_for(uint32_t dep) const {
    for (int i = 0; i < CACHE_RAW_DAYS; i++)
        if (_raw_days[i].day_epoch == dep) return i;
    return -1;
}

int PsramCache::_raw_oldest_slot() const {
    int oldest = 0;
    for (int i = 1; i < CACHE_RAW_DAYS; i++)
        if (_raw_days[i].day_epoch < _raw_days[oldest].day_epoch) oldest = i;
    return oldest;
}

void PsramCache::_raw_load(uint32_t dep) {
    int slot = _raw_slot_for(dep);
    if (slot < 0) {
        slot = _raw_oldest_slot();
        _raw_days[slot].day_epoch = dep;
        _raw_days[slot].count     = 0;
        _raw_days[slot].valid     = false;
    }
    Record5Min* dst = _raw_buf + slot * RECS_PER_DAY;
    uint32_t n = Store.readDay(dep, dst, RECS_PER_DAY);
    _raw_days[slot].count  = (uint16_t)n;
    _raw_days[slot].valid  = (n > 0);
    _raw_days[slot].loaded = true;
    if (n > 0) _raw_days[slot].last = dst[n-1];
}

const Record5Min* PsramCache::getRawDay(uint32_t dep, uint32_t& cnt) {
    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    int slot = _raw_slot_for(dep);
    if (slot < 0 || !_raw_days[slot].loaded) { _raw_load(dep); slot = _raw_slot_for(dep); }
    if (slot < 0 || !_raw_days[slot].valid) {
        cnt = 0;
        xSemaphoreGiveRecursive(_mutex);
        return nullptr;
    }
    cnt = _raw_days[slot].count;
    // El puntero devuelto es válido mientras el llamador mantenga Cache.lock()
    xSemaphoreGiveRecursive(_mutex);
    return _raw_buf + slot * RECS_PER_DAY;
}

bool PsramCache::getLastRaw(uint32_t dep, Record5Min& out) {
    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    int slot = _raw_slot_for(dep);
    if (slot < 0 || !_raw_days[slot].loaded) { _raw_load(dep); slot = _raw_slot_for(dep); }
    bool ok = (slot >= 0 && _raw_days[slot].valid);
    if (ok) out = _raw_days[slot].last;
    xSemaphoreGiveRecursive(_mutex);
    return ok;
}

void PsramCache::pushRaw(const Record5Min& r) {
    time_t t = (time_t)r.timestamp;
    struct tm tm_r; localtime_r(&t, &tm_r);
    tm_r.tm_hour = 0; tm_r.tm_min = 0; tm_r.tm_sec = 0; tm_r.tm_isdst = -1;
    uint32_t dep = (uint32_t)mktime(&tm_r);

    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    int slot = _raw_slot_for(dep);
    if (slot < 0 || !_raw_days[slot].loaded) {
        _raw_load(dep);
        slot = _raw_slot_for(dep);
        if (slot < 0) {
            xSemaphoreGiveRecursive(_mutex);
            return;
        }
    }
    uint16_t& cnt = _raw_days[slot].count;
    if (cnt > 0 && _raw_buf[slot * RECS_PER_DAY + cnt - 1].timestamp == r.timestamp) {
        _raw_buf[slot * RECS_PER_DAY + cnt - 1] = r;
        _raw_days[slot].last = r;
        xSemaphoreGiveRecursive(_mutex);
        return;
    }
    if (cnt < RECS_PER_DAY) {
        _raw_buf[slot*RECS_PER_DAY + cnt] = r; cnt++;
    } else {
        memmove(_raw_buf + slot*RECS_PER_DAY,
                _raw_buf + slot*RECS_PER_DAY + 1,
                (RECS_PER_DAY-1)*sizeof(Record5Min));
        _raw_buf[slot*RECS_PER_DAY + RECS_PER_DAY-1] = r;
    }
    _raw_days[slot].valid = true;
    _raw_days[slot].last  = r;
    xSemaphoreGiveRecursive(_mutex);
}

void PsramCache::reinitAfterNtp() {
    time_t now; time(&now);
    if (now < 1700000000UL) return;   // NTP aún no listo

    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);

    struct tm tm_now; localtime_r(&now, &tm_now);
    tm_now.tm_hour = 0; tm_now.tm_min = 0; tm_now.tm_sec = 0; tm_now.tm_isdst = -1;
    uint32_t today = (uint32_t)mktime(&tm_now);

    DBGSERIAL.printf("[Cache] reinitAfterNtp: today=%lu\n", (unsigned long)today);

    // Corregir los slots raw que tienen epochs incorrectos (originados en begin()
    // cuando NTP aún no estaba disponible, lo que provoca overflow de uint32_t).
    for (int i = 0; i < CACHE_RAW_DAYS; i++) {
        uint32_t expected = today - (uint32_t)(CACHE_RAW_DAYS - 1 - i) * 86400;
        if (_raw_days[i].day_epoch != expected) {
            _raw_days[i].day_epoch = expected;
            _raw_days[i].count     = 0;
            _raw_days[i].valid     = false;
            _raw_days[i].loaded    = false;
        }
    }
    for (int i = 0; i < CACHE_HRLY_DAYS; i++) {
        uint32_t expected = today - (uint32_t)(CACHE_HRLY_DAYS - 1 - i) * 86400;
        if (_hrly_days[i].day_epoch != expected) {
            _hrly_days[i].day_epoch   = expected;
            _hrly_days[i].valid       = false;
            _hrly_days[i].loaded      = false;
            _hrly_days[i].hours_valid = 0;
        }
    }

    // Eager-load de los últimos 7 días con los epochs correctos
    for (int i = CACHE_RAW_DAYS - 7; i < CACHE_RAW_DAYS; i++)
        _raw_load(_raw_days[i].day_epoch);
    for (int i = CACHE_HRLY_DAYS - 7; i < CACHE_HRLY_DAYS; i++)
        _hrly_load(_hrly_days[i].day_epoch);

    xSemaphoreGiveRecursive(_mutex);
    printStats();
}

uint32_t PsramCache::getOldestDailyEpoch() const {
    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    if (_day_count == 0) { xSemaphoreGiveRecursive(_mutex); return 0; }
    uint32_t oldest = UINT32_MAX;
    for (uint32_t i = 0; i < _day_count; i++) {
        if ((_day_buf[i].flags & 0x01) &&
            _day_buf[i].day_epoch < oldest)
            oldest = _day_buf[i].day_epoch;
    }
    xSemaphoreGiveRecursive(_mutex);
    return (oldest == UINT32_MAX) ? 0 : oldest;
}

void PsramCache::_bitmap_build() {
    if (!_has_data_bitmap) return;
    for (uint32_t i = 0; i < _day_count; i++) {
        if (_day_buf[i].flags & 0x01)
            _bitmap_set(_day_buf[i].day_epoch, true);
    }
}

void PsramCache::_bitmap_set(uint32_t dep, bool has) {
    if (!_has_data_bitmap) return;
    // Usar hoy como referencia: bit N = hace N días
    uint32_t today = [] {
        time_t now; time(&now);
        struct tm t; localtime_r(&now, &t);
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
        return (uint32_t)mktime(&t);
    }();
    if (dep > today) return;
    uint32_t days_ago = (today - dep) / 86400;
    if (days_ago >= BITMAP_DAYS) return;
    uint32_t byte_idx = days_ago / 8;
    uint32_t bit_idx  = days_ago % 8;
    if (has) _has_data_bitmap[byte_idx] |=  (1 << bit_idx);
    else     _has_data_bitmap[byte_idx] &= ~(1 << bit_idx);
}

bool PsramCache::dayHasData(uint32_t dep) const {
    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    bool result;
    if (!_has_data_bitmap) {
        DailyRecord dr{};
        result = const_cast<PsramCache*>(this)->getDaily(dep, dr);
    } else {
        time_t now; time(&now);
        struct tm t; localtime_r(&now, &t);
        t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
        uint32_t today = (uint32_t)mktime(&t);
        if (dep > today) {
            xSemaphoreGiveRecursive(_mutex);
            return false;
        }
        uint32_t days_ago = (today - dep) / 86400;
        result = (days_ago < BITMAP_DAYS) &&
                 ((_has_data_bitmap[days_ago/8] >> (days_ago%8)) & 1);
    }
    xSemaphoreGiveRecursive(_mutex);
    return result;
}

void PsramCache::printStats() {
    uint32_t total_psram = CACHE_RAW_SIZE + CACHE_HRLY_SIZE + CACHE_DAY_SIZE;
    DBGSERIAL.printf("[Cache] PSRAM total: %u KB | Daily: %lu/%d registros\n",
                  (unsigned)(total_psram/1024),
                  (unsigned long)_day_count, CACHE_DAY_MAX);
}
