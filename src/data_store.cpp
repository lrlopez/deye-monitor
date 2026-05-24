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

bool DataStore::writeFile(File& f, uint32_t phys, const void* data, size_t sz) {
    if (!f.seek(phys * sz)) return false;
    if (f.write((const uint8_t*)data, sz) != sz) return false;
    f.flush();
    return true;
}

bool DataStore::readFile(File& f, uint32_t phys, void* data, size_t sz) {
    if (!f.seek(phys * sz)) return false;
    return f.read((uint8_t*)data, sz) == sz;
}

// ── Inicialización ────────────────────────────────────────────────────────
// ── begin(): abrir ficheros y construir índice ────────────────────────────
bool DataStore::begin() {
    _mutex = xSemaphoreCreateMutex();

    // LittleFS debe estar montado por el llamador antes de invocar begin().
    // main.cpp lo monta en setup() antes de llamar a Store.begin().

    // Alojar índice de días en PSRAM
    _day_idx = (DayIdx*)heap_caps_malloc(
               DAY_IDX_MAX * sizeof(DayIdx),
               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_day_idx) {
        DBGSERIAL.println("[Store] Sin PSRAM para índice de días"); return false;
    }
    memset(_day_idx, 0, STORE_DAYS * sizeof(DayIdx));

    // Cargar meta; si falla (primer arranque, magia incorrecta o versión
    // incompatible) borrar los ficheros binarios para empezar desde cero.
    // Sin esto, los .bin quedan con datos de un formato anterior y las
    // escrituras nuevas desde posición 0 dejan basura al final del anillo.
    if (!loadMeta()) {
        DBGSERIAL.println("[Store] Meta inválida o ausente — reinicializando flash");
        LittleFS.remove(_raw.path);
        LittleFS.remove(_hrly.path);
        LittleFS.remove(_day.path);
        LittleFS.remove(META_FILE);
        // head y count ya son 0 por la inicialización de CircBuf
    }

    // ── Crear ficheros si no existen y abrir raw con handle permanente ───
    // NO pre-alocamos con seek+write(0): eso consume casi todos los bloques
    // LittleFS (6.7 MB en 8 MB de partición = 94%), dejando sin margen para
    // los COW de metadatos que necesita flush(). Los ficheros crecen on-demand.
    auto ensure_file = [](const char* path) {
        if (!LittleFS.exists(path)) {
            File tmp = LittleFS.open(path, "w");
            if (tmp) tmp.close();
        }
        File f = LittleFS.open(path, "r+");
        bool ok = (bool)f;
        if (f) f.close();
        return ok;
    };

    if (!ensure_file("/raw.bin")  ||
        !ensure_file("/hrly.bin") ||
        !ensure_file("/day.bin")) {
        DBGSERIAL.println("[Store] Error creando ficheros");
        return false;
    }
    _f_raw  = LittleFS.open("/raw.bin",  "r+");
    _f_hrly = LittleFS.open("/hrly.bin", "r+");
    _f_day  = LittleFS.open("/day.bin",  "r+");
    if (!_f_raw || !_f_hrly || !_f_day) {
        DBGSERIAL.println("[Store] Error abriendo ficheros de datos");
        return false;
    }

    DBGSERIAL.printf("[Store] Ficheros abiertos. raw=%lu hrly=%lu day=%lu registros. "
                  "LittleFS libre=%u KB\n",
                  (unsigned long)_raw.count,
                  (unsigned long)_hrly.count,
                  (unsigned long)_day.count,
                  (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024);

    // Construir índice de días desde el buffer raw
    if (_raw.count > 0) _day_idx_load();

    return true;
}

// ── Acceso directo con file handle abierto ────────────────────────────────
bool DataStore::writeRaw(uint32_t phys, const Record5Min& r) {
    if (!_f_raw.seek((uint32_t)phys * sizeof(Record5Min))) return false;
    if (_f_raw.write((const uint8_t*)&r, sizeof(r)) != sizeof(r)) return false;
    _f_raw.flush();   // forzar commit a flash; sin esto el write-buffer de LittleFS
                      // pierde los datos si el ESP se reinicia antes de llenar el bloque
    return true;
}
bool DataStore::readRaw(uint32_t phys, Record5Min& r) {
    return _f_raw.seek((uint32_t)phys * sizeof(Record5Min)) &&
           _f_raw.read((uint8_t*)&r, sizeof(r)) == sizeof(r) &&
           r.timestamp > 1577836800UL;  // >= 2020-01-01; filtra arranques pre-NTP (~1970)
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

        if (dep != prev_dep && _day_idx_count < DAY_IDX_MAX) {
            _day_idx[_day_idx_count++] = {dep, physIdx(_raw, li)};
            prev_dep = dep;
        }
    }
    DBGSERIAL.printf("[Store] Índice de días: %lu entradas (raw_count=%lu head=%lu cap=%lu)\n",
                  (unsigned long)_day_idx_count,
                  (unsigned long)_raw.count,
                  (unsigned long)_raw.head,
                  (unsigned long)_raw.capacity);
    for (uint32_t i = 0; i < _day_idx_count && i < 5; i++)
        DBGSERIAL.printf("[Store]   [%lu] dep=%lu li=%lu\n",
                       (unsigned long)i,
                       (unsigned long)_day_idx[i].day_epoch,
                       (unsigned long)_day_idx[i].phys_start);
    if (_day_idx_count > 5)
        DBGSERIAL.printf("[Store]   ... último: dep=%lu li=%lu\n",
                       (unsigned long)_day_idx[_day_idx_count-1].day_epoch,
                       (unsigned long)_day_idx[_day_idx_count-1].phys_start);

    // Diagnóstico: si el índice está vacío pero hay registros, leer los primeros
    // bytes del fichero directamente para ver qué contiene
    if (_day_idx_count == 0 && _raw.count > 0) {
        uint8_t raw16[16] = {};
        bool seekOk = _f_raw.seek(0);
        size_t nRead = _f_raw.read(raw16, 16);
        DBGSERIAL.printf("[Store] DIAG raw.bin[0]: seek=%d read=%u "
                       "bytes=[%02X %02X %02X %02X | %02X %02X | %02X %02X | "
                       "%02X %02X | %02X %02X | %02X %02X | %02X %02X]\n",
                       (int)seekOk, (unsigned)nRead,
                       raw16[0],raw16[1],raw16[2],raw16[3],
                       raw16[4],raw16[5], raw16[6],raw16[7],
                       raw16[8],raw16[9], raw16[10],raw16[11],
                       raw16[12],raw16[13], raw16[14],raw16[15]);
        DBGSERIAL.printf("[Store] DIAG sizeof(Record5Min)=%u "
                       "raw.bin size=%u\n",
                       (unsigned)sizeof(Record5Min),
                       (unsigned)_f_raw.size());
    }
}

void DataStore::_day_idx_insert(uint32_t dep, uint32_t phys_start) {
    if (_day_idx_count >= DAY_IDX_MAX) {
        memmove(_day_idx, _day_idx + 1,
                (_day_idx_count - 1) * sizeof(DayIdx));
        _day_idx_count--;
    }
    _day_idx[_day_idx_count++] = {dep, phys_start};
}

int DataStore::_day_idx_find(uint32_t dep) const {
    for (uint32_t i = 0; i < _day_idx_count; i++)
        if (_day_idx[i].day_epoch == dep) return (int)i;
    return -1;
}

// ── push(): meta guardada cada 12 pushes ──────────────────────────────────
bool DataStore::push(const Record5Min& r) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        DBGSERIAL.println("[Store] push: mutex timeout");
        return false;
    }

    // Detectar inicio de nuevo día para actualizar índice
    time_t t = (time_t)r.timestamp;
    struct tm tm_r; localtime_r(&t, &tm_r);
    tm_r.tm_hour = 0; tm_r.tm_min = 0; tm_r.tm_sec = 0; tm_r.tm_isdst = -1;
    uint32_t dep = (uint32_t)mktime(&tm_r);

    uint32_t phys = (_raw.count < _raw.capacity)
                  ? physIdx(_raw, _raw.count)
                  : _raw.head;

    if (!writeRaw(phys, r)) {
        DBGSERIAL.printf("[Store] push FALLO escritura ts=%lu phys=%lu\n",
                       (unsigned long)r.timestamp, (unsigned long)phys);
        xSemaphoreGive(_mutex);
        return false;
    }

    // Solo actualizar estado si la escritura tuvo éxito
    if (_raw.count < _raw.capacity) {
        _raw.count++;
    } else {
        _raw.head = (_raw.head + 1) % _raw.capacity;
    }

    // ¿Es el primer registro de este día? Guardar posición física (inmutable),
    // no lógica — la posición lógica cambia con cada avance de head.
    if (_day_idx_count == 0 ||
        _day_idx[_day_idx_count-1].day_epoch != dep) {
        _day_idx_insert(dep, phys);
    }

    DBGSERIAL.printf("[Store] push ts=%lu phys=%lu count=%lu\n",
                   (unsigned long)r.timestamp, (unsigned long)phys,
                   (unsigned long)_raw.count);

    if (++_pushes_since_meta_save >= META_SAVE_INTERVAL) {
        if (!saveMeta()) DBGSERIAL.println("[Store] saveMeta FALLO");
        _pushes_since_meta_save = 0;
    }

    xSemaphoreGive(_mutex);
    return true;
}

// ── readDay(): O(1) con índice + scan secuencial ──────────────────────────
uint32_t DataStore::readDay(uint32_t dep, Record5Min* out, uint32_t max) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) return 0;

    uint32_t next_dep = dep + 86400;
    uint32_t found    = 0;

    int idx_entry = _day_idx_find(dep);

    if (idx_entry < 0 && _day_idx_count > 0) {
        DBGSERIAL.printf("[Store] readDay dep=%lu NO ENCONTRADO. idx_count=%lu "
                       "first=%lu last=%lu\n",
                       (unsigned long)dep,
                       (unsigned long)_day_idx_count,
                       (unsigned long)_day_idx[0].day_epoch,
                       (unsigned long)_day_idx[_day_idx_count-1].day_epoch);
    }

    if (idx_entry < 0) {
        xSemaphoreGive(_mutex);
        return 0;
    }

    // Convertir posición física (inmutable) a índice lógico actual.
    // La posición física no varía nunca; el índice lógico cambia con cada
    // avance de head al envolver el anillo, de ahí que se recalcule aquí.
    uint32_t phys_s   = _day_idx[idx_entry].phys_start;
    uint32_t start_li = (phys_s - _raw.head + _raw.capacity) % _raw.capacity;

    DBGSERIAL.printf("[Store] readDay dep=%lu idx=%d phys=%lu start_li=%lu raw_count=%lu\n",
                   (unsigned long)dep, idx_entry,
                   (unsigned long)phys_s,
                   (unsigned long)start_li,
                   (unsigned long)_raw.count);

    for (uint32_t li = start_li; li < _raw.count && found < max; li++) {
        Record5Min r{};
        if (!readRaw(physIdx(_raw, li), r)) continue;
        if (r.timestamp >= next_dep) break;
        if (r.timestamp >= dep) out[found++] = r;
    }

    DBGSERIAL.printf("[Store] readDay encontrados=%lu\n", (unsigned long)found);
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
    bool ok = writeFile(_f_hrly, phys, &r, sizeof(r));
    if (ok) saveMeta();
    xSemaphoreGive(_mutex);

    DBGSERIAL.printf("[Hourly] %08lu guardado: pv=%dW grid=%dW bat=%dW load=%dW soc=%d%% n=%d\n",
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
        if (readFile(_f_day, pi, &existing, sizeof(existing)) &&
            existing.day_epoch == r.day_epoch) {
            writeFile(_f_day, pi, &r, sizeof(r));
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
    bool ok = writeFile(_f_day, phys, &r, sizeof(r));
    if (ok) saveMeta();
    currentDaily = r;
    xSemaphoreGive(_mutex);

    DBGSERIAL.printf("[Daily] %08lu guardado: pv=%.1f exp=%.1f imp=%.1f load=%.1f kWh\n",
                  (unsigned long)r.day_epoch,
                  r.pv_10wh/10.0f, r.export_10wh/10.0f,
                  r.import_10wh/10.0f, r.load_10wh/10.0f);
    return ok;
}

uint32_t DataStore::getLastRawDayEpoch() const {
    if (_day_idx_count == 0) return 0;
    return _day_idx[_day_idx_count - 1].day_epoch;
}

bool DataStore::getLastRecord(Record5Min& out) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    bool ok = (_raw.count > 0) &&
              readRaw(physIdx(_raw, _raw.count - 1), out);
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
        if (readFile(_f_hrly, physIdx(_hrly, mid), &r, sizeof(r)) && r.hour_epoch < ts)
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
        if (!readFile(_f_hrly, physIdx(_hrly, i), &r, sizeof(r))) continue;
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
        if (!readFile(_f_hrly, physIdx(_hrly, i), &r, sizeof(r))) continue;
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
        if (readFile(_f_day, physIdx(_day, i), &r, sizeof(r)) &&
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
        readFile(_f_day, physIdx(_day, i), &out[i], sizeof(out[i]));
    xSemaphoreGive(_mutex);
    return n;
}

DailyRecord DataStore::getCurrentDaily() {
    return currentDaily;
}
