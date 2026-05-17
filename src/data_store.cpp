#include "data_store.h"
#include <time.h>

const char* DataStore::META_FILE = "/meta2.bin";

DataStore& DataStore::instance() {
    static DataStore d; return d;
}

// ── Meta ──────────────────────────────────────────────────────────────────
struct Meta2 {
    uint32_t magic, version;
    uint32_t raw_head,  raw_count,  raw_cap;
    uint32_t hrly_head, hrly_count, hrly_cap;
    uint32_t day_head,  day_count,  day_cap;
};

bool DataStore::loadMeta() {
    File f = LittleFS.open(META_FILE, "r");
    if (!f) return false;
    Meta2 m{};
    bool ok = f.read((uint8_t*)&m, sizeof(m)) == sizeof(m);
    f.close();
    if (!ok || m.magic != MAGIC || m.version != VERSION) return false;
    _raw.head  = m.raw_head;  _raw.count  = m.raw_count;  _raw.capacity  = m.raw_cap;
    _hrly.head = m.hrly_head; _hrly.count = m.hrly_count; _hrly.capacity = m.hrly_cap;
    _day.head  = m.day_head;  _day.count  = m.day_count;  _day.capacity  = m.day_cap;
    return true;
}

bool DataStore::saveMeta() {
    File f = LittleFS.open(META_FILE, "w");
    if (!f) return false;
    Meta2 m{MAGIC, VERSION,
            _raw.head,  _raw.count,  _raw.capacity,
            _hrly.head, _hrly.count, _hrly.capacity,
            _day.head,  _day.count,  _day.capacity};
    bool ok = f.write((uint8_t*)&m, sizeof(m)) == sizeof(m);
    f.close();
    return ok;
}

// ── Lectura/escritura genérica ────────────────────────────────────────────
uint32_t DataStore::physIdx(const CircBuf& cb, uint32_t logical) const {
    return (cb.head + logical) % cb.capacity;
}

bool DataStore::writeAt(const CircBuf& cb, uint32_t phys,
                         const void* data, size_t sz) {
    File f = LittleFS.open(cb.path, LittleFS.exists(cb.path) ? "r+" : "w");
    if (!f) return false;
    bool ok = f.seek((uint32_t)phys * sz) &&
              f.write((const uint8_t*)data, sz) == sz;
    f.close();
    return ok;
}

bool DataStore::readAt(const CircBuf& cb, uint32_t phys,
                        void* data, size_t sz) {
    File f = LittleFS.open(cb.path, "r");
    if (!f) return false;
    bool ok = f.seek((uint32_t)phys * sz) &&
              f.read((uint8_t*)data, sz) == sz;
    f.close();
    return ok;
}

// ── Inicialización ────────────────────────────────────────────────────────
// ── begin(): abrir ficheros y construir índice ────────────────────────────
bool DataStore::begin() {
    _mutex = xSemaphoreCreateMutex();

    if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
        Serial.println("[Store] Error montando LittleFS"); return false;
    }

    // Alojar índice de días en PSRAM
    _day_idx = (DayIdx*)heap_caps_malloc(
               DAY_IDX_MAX * sizeof(DayIdx),
               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_day_idx) {
        Serial.println("[Store] Sin PSRAM para índice de días"); return false;
    }
    memset(_day_idx, 0, 730 * sizeof(DayIdx));

    // Cargar meta
    bool meta_ok = loadMeta();

    // ── Abrir ficheros y mantenerlos abiertos ────────────────────────────
    auto open_rw = [](File& f, const char* path, uint32_t cap, uint32_t rec_sz) {
        if (!LittleFS.exists(path)) {
            // Pre-alocar con un write al último byte para evitar
            // fragmentación posterior
            File tmp = LittleFS.open(path, "w");
            if (tmp) {
                tmp.seek((uint32_t)(cap * rec_sz) - 1);
                tmp.write(0);
                tmp.close();
            }
        }
        f = LittleFS.open(path, "r+");
        return (bool)f;
    };

    if (!open_rw(_f_raw,  "/raw.bin",  _raw.capacity,  sizeof(Record5Min))  ||
        !open_rw(_f_hrly, "/hrly.bin", _hrly.capacity, sizeof(HourlyRecord)) ||
        !open_rw(_f_day,  "/day.bin",  _day.capacity,  sizeof(DailyRecord))) {
        Serial.println("[Store] Error abriendo ficheros");
        return false;
    }

    Serial.printf("[Store] Ficheros abiertos. raw=%lu hrly=%lu day=%lu registros\n",
                  (unsigned long)_raw.count,
                  (unsigned long)_hrly.count,
                  (unsigned long)_day.count);

    // Construir índice de días desde el buffer raw
    if (_raw.count > 0) _day_idx_load();

    return true;
}

// ── Acceso directo con file handle abierto ────────────────────────────────
bool DataStore::writeRaw(uint32_t phys, const Record5Min& r) {
    return _f_raw.seek((uint32_t)phys * sizeof(Record5Min)) &&
           _f_raw.write((const uint8_t*)&r, sizeof(r)) == sizeof(r);
}
bool DataStore::readRaw(uint32_t phys, Record5Min& r) {
    return _f_raw.seek((uint32_t)phys * sizeof(Record5Min)) &&
           _f_raw.read((uint8_t*)&r, sizeof(r)) == sizeof(r) &&
           (r.flags & 0x01);
}
bool DataStore::writeHrly(uint32_t phys, const HourlyRecord& r) {
    return _f_hrly.seek((uint32_t)phys * sizeof(HourlyRecord)) &&
           _f_hrly.write((const uint8_t*)&r, sizeof(r)) == sizeof(r);
}
bool DataStore::readHrly(uint32_t phys, HourlyRecord& r) {
    return _f_hrly.seek((uint32_t)phys * sizeof(HourlyRecord)) &&
           _f_hrly.read((uint8_t*)&r, sizeof(r)) == sizeof(r);
}
bool DataStore::writeDay_(uint32_t phys, const DailyRecord& r) {
    return _f_day.seek((uint32_t)phys * sizeof(DailyRecord)) &&
           _f_day.write((const uint8_t*)&r, sizeof(r)) == sizeof(r);
}
bool DataStore::readDay_(uint32_t phys, DailyRecord& r) {
    return _f_day.seek((uint32_t)phys * sizeof(DailyRecord)) &&
           _f_day.read((uint8_t*)&r, sizeof(r)) == sizeof(r);
}

// ── Índice de días ────────────────────────────────────────────────────────
void DataStore::_day_idx_load() {
    // Escanear el buffer raw para encontrar el inicio de cada día
    // Solo se ejecuta una vez en begin()
    _day_idx_count = 0;
    uint32_t prev_dep = 0;

    for (uint32_t li = 0; li < _raw.count; li++) {
        Record5Min r{};
        if (!readRaw(physIdx(_raw, li), r)) continue;

        // Calcular medianoche del registro
        time_t t = (time_t)r.timestamp;
        struct tm tm_r; localtime_r(&t, &tm_r);
        tm_r.tm_hour = 0; tm_r.tm_min = 0; tm_r.tm_sec = 0; tm_r.tm_isdst = -1;
        uint32_t dep = (uint32_t)mktime(&tm_r);

        if (dep != prev_dep && _day_idx_count < 730) {
            _day_idx[_day_idx_count++] = {dep, li};
            prev_dep = dep;
        }
    }
    Serial.printf("[Store] Índice de días: %lu entradas\n",
                  (unsigned long)_day_idx_count);
}

void DataStore::_day_idx_insert(uint32_t dep, uint32_t logical_start) {
    if (_day_idx_count >= DAY_IDX_MAX) {
        // Desplazar (el más antiguo se pierde con el buffer circular)
        memmove(_day_idx, _day_idx + 1,
                (_day_idx_count - 1) * sizeof(DayIdx));
        _day_idx_count--;
    }
    _day_idx[_day_idx_count++] = {dep, logical_start};
}

int DataStore::_day_idx_find(uint32_t dep) const {
    for (uint32_t i = 0; i < _day_idx_count; i++)
        if (_day_idx[i].day_epoch == dep) return (int)i;
    return -1;
}

// ── push(): meta guardada cada 12 pushes ──────────────────────────────────
bool DataStore::push(const Record5Min& r) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    // Detectar inicio de nuevo día para actualizar índice
    time_t t = (time_t)r.timestamp;
    struct tm tm_r; localtime_r(&t, &tm_r);
    tm_r.tm_hour = 0; tm_r.tm_min = 0; tm_r.tm_sec = 0; tm_r.tm_isdst = -1;
    uint32_t dep = (uint32_t)mktime(&tm_r);

    uint32_t next_logical;
    if (_raw.count < _raw.capacity) {
        next_logical = _raw.count;
        _raw.count++;
    } else {
        next_logical = _raw.head;
        _raw.head    = (_raw.head + 1) % _raw.capacity;
    }

    // ¿Es el primer registro de este día?
    if (_day_idx_count == 0 ||
        _day_idx[_day_idx_count-1].day_epoch != dep) {
        _day_idx_insert(dep, next_logical);
    }

    bool ok = writeRaw(physIdx(_raw, next_logical > 0 ?
                                next_logical : _raw.count - 1), r);

    // Meta cada META_SAVE_INTERVAL pushes
    if (++_pushes_since_meta_save >= META_SAVE_INTERVAL) {
        saveMeta();
        _pushes_since_meta_save = 0;
    }

    xSemaphoreGive(_mutex);
    return ok;
}

// ── readDay(): O(1) con índice + scan secuencial ──────────────────────────
uint32_t DataStore::readDay(uint32_t dep, Record5Min* out, uint32_t max) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) return 0;

    uint32_t next_dep = dep + 86400;
    uint32_t found    = 0;

    // Buscar inicio del día en el índice O(n_days) ≈ O(730) comparaciones
    int idx_entry = _day_idx_find(dep);
    uint32_t start_li = (idx_entry >= 0) ?
                        _day_idx[idx_entry].logical_start : 0;

    // Si no está en el índice, no hay datos para ese día
    if (idx_entry < 0) {
        xSemaphoreGive(_mutex);
        return 0;
    }

    // Scan secuencial desde el inicio del día (sin seek por búsqueda binaria)
    for (uint32_t li = start_li; li < _raw.count && found < max; li++) {
        Record5Min r{};
        if (!readRaw(physIdx(_raw, li), r)) continue;
        if (r.timestamp >= next_dep) break;
        if (r.timestamp >= dep) out[found++] = r;
    }

    xSemaphoreGive(_mutex);
    return found;
}

// ── Hourly push ───────────────────────────────────────────────────────────
bool DataStore::pushHourly(const HourlyRecord& r) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    uint32_t phys;
    if (_hrly.count < _hrly.capacity) {
        phys = physIdx(_hrly, _hrly.count++);
    } else {
        phys = _hrly.head;
        _hrly.head = (_hrly.head + 1) % _hrly.capacity;
    }
    bool ok = writeAt(_hrly, phys, &r, sizeof(r));
    if (ok) saveMeta();
    xSemaphoreGive(_mutex);

    Serial0.printf("[Hourly] %08lu guardado: pv=%dW grid=%dW bat=%dW load=%dW soc=%d%% n=%d\n",
                  (unsigned long)r.hour_epoch,
                  r.avg_pv_w, r.avg_grid_w, r.avg_batt_w, r.avg_load_w,
                  r.soc_end, r.sample_count);
    return ok;
}

// ── Daily push ────────────────────────────────────────────────────────────
bool DataStore::pushDaily(const DailyRecord& r) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    // Buscar si ya existe un registro para ese día (actualizar en lugar de duplicar)
    for (uint32_t i = 0; i < _day.count; i++) {
        uint32_t pi = physIdx(_day, i);
        DailyRecord existing{};
        if (readAt(_day, pi, &existing, sizeof(existing)) &&
            existing.day_epoch == r.day_epoch) {
            writeAt(_day, pi, &r, sizeof(r));
            saveMeta();
            xSemaphoreGive(_mutex);
            return true;
        }
    }

    uint32_t phys;
    if (_day.count < _day.capacity) {
        phys = physIdx(_day, _day.count++);
    } else {
        phys = _day.head;
        _day.head = (_day.head + 1) % _day.capacity;
    }
    bool ok = writeAt(_day, phys, &r, sizeof(r));
    if (ok) saveMeta();
    xSemaphoreGive(_mutex);

    Serial0.printf("[Daily] %08lu guardado: pv=%.1f exp=%.1f imp=%.1f load=%.1f kWh\n",
                  (unsigned long)r.day_epoch,
                  r.pv_10wh/10.0f, r.export_10wh/10.0f,
                  r.import_10wh/10.0f, r.load_10wh/10.0f);
    return ok;
}

// ── Raw read ──────────────────────────────────────────────────────────────
uint32_t DataStore::lowerBoundRaw(uint32_t ts) {
    if (_raw.count == 0) return 0;
    uint32_t lo = 0, hi = _raw.count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        Record5Min r{};
        if (readAt(_raw, physIdx(_raw, mid), &r, sizeof(r)) && r.timestamp < ts)
            lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

bool DataStore::getLastRecord(Record5Min& out) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    bool ok = (_raw.count > 0) &&
              readAt(_raw, physIdx(_raw, _raw.count - 1), &out, sizeof(out));
    xSemaphoreGive(_mutex);
    return ok;
}

// ── Hourly read ───────────────────────────────────────────────────────────
uint32_t DataStore::lowerBoundHrly(uint32_t ts) {
    if (_hrly.count == 0) return 0;
    uint32_t lo = 0, hi = _hrly.count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        HourlyRecord r{};
        if (readAt(_hrly, physIdx(_hrly, mid), &r, sizeof(r)) && r.hour_epoch < ts)
            lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

uint8_t DataStore::readHourly(uint32_t dep, HourlyRecord out[24]) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) return 0;

    // Inicializar todas las horas como inválidas
    for (int h = 0; h < 24; h++) {
        out[h] = {};
        out[h].hour_epoch = dep + (uint32_t)h * 3600;
    }

    uint32_t next  = dep + 86400;
    uint32_t start = lowerBoundHrly(dep);
    uint8_t  found = 0;

    for (uint32_t i = start; i < _hrly.count; i++) {
        HourlyRecord r{};
        if (!readAt(_hrly, physIdx(_hrly, i), &r, sizeof(r))) continue;
        if (r.hour_epoch >= next) break;
        if (!(r.flags & 0x01)) continue;
        // Calcular índice de hora
        uint32_t h = (r.hour_epoch - dep) / 3600;
        if (h < 24) { out[h] = r; found++; }
    }

    xSemaphoreGive(_mutex);
    return found;
}

bool DataStore::getLastHourly(uint32_t dep, HourlyRecord& out) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) return false;
    uint32_t next = dep + 86400;
    bool found = false;
    uint32_t start = lowerBoundHrly(dep);
    for (uint32_t i = start; i < _hrly.count; i++) {
        HourlyRecord r{};
        if (!readAt(_hrly, physIdx(_hrly, i), &r, sizeof(r))) continue;
        if (r.hour_epoch >= next) break;
        if (r.flags & 0x01) { out = r; found = true; }
    }
    xSemaphoreGive(_mutex);
    return found;
}

// ── Daily read ────────────────────────────────────────────────────────────
bool DataStore::readDaily(uint32_t dep, DailyRecord& out) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    bool found = false;
    for (uint32_t i = 0; i < _day.count; i++) {
        DailyRecord r{};
        if (readAt(_day, physIdx(_day, i), &r, sizeof(r)) &&
            r.day_epoch == dep && (r.flags & 0x01)) {
            out = r; found = true; break;
        }
    }
    xSemaphoreGive(_mutex);
    return found;
}

uint32_t DataStore::readAllDaily(DailyRecord* out, uint32_t max) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) return 0;
    uint32_t n = (_day.count < max) ? _day.count : max;
    for (uint32_t i = 0; i < n; i++)
        readAt(_day, physIdx(_day, i), &out[i], sizeof(out[i]));
    xSemaphoreGive(_mutex);
    return n;
}
