#include "storage.h"
#include "config.h"   // fallbacks de compilación

// ── Namespaces NVS ────────────────────────────────────────────────────────
static const char* NS_CFG    = "cfg";
static const char* NS_HIST_D = "hist_d";
static const char* NS_HIST_H = "hist_h";

// ── Claves config ─────────────────────────────────────────────────────────
static const char* K_SSID    = "ssid";
static const char* K_PASS    = "pass";
static const char* K_LIP     = "lip";
static const char* K_LSERIAL = "lserial";

// ── Claves histórico ──────────────────────────────────────────────────────
static const char* K_META    = "meta";
// Los registros se guardan como "rXX" donde XX es el índice (00-89)
// Ejemplo: "r00", "r01", ..., "r89"
static const char* K_SESSION = "session";

// ═════════════════════════════════════════════════════════════════════════
// Config
// ═════════════════════════════════════════════════════════════════════════

void StorageManager::loadConfig(AppConfig& out) {
    Preferences p;
    p.begin(NS_CFG, /*readOnly=*/true);

    // Fallback a los #define de config.h si no hay valor guardado
    String ssid = p.getString(K_SSID,    WIFI_SSID);
    String pass = p.getString(K_PASS,    WIFI_PASS);
    String lip  = p.getString(K_LIP,     LOGGER_IP);
    out.logger_serial = p.getULong(K_LSERIAL, LOGGER_SERIAL);

    ssid.toCharArray(out.wifi_ssid,    sizeof(out.wifi_ssid));
    pass.toCharArray(out.wifi_pass,    sizeof(out.wifi_pass));
    lip.toCharArray(out.logger_ip,     sizeof(out.logger_ip));

    p.end();
    Serial0.printf("[NVS] Config cargada: SSID=%s  IP=%s  Serial=%lu\n",
                  out.wifi_ssid, out.logger_ip, (unsigned long)out.logger_serial);
}

void StorageManager::saveConfig(const AppConfig& cfg) {
    Preferences p;
    p.begin(NS_CFG, /*readOnly=*/false);
    p.putString(K_SSID,    cfg.wifi_ssid);
    p.putString(K_PASS,    cfg.wifi_pass);
    p.putString(K_LIP,     cfg.logger_ip);
    p.putULong(K_LSERIAL,  cfg.logger_serial);
    p.end();
    Serial0.println("[NVS] Config guardada");
}

// ═════════════════════════════════════════════════════════════════════════
// Histórico diario – buffer circular
// ═════════════════════════════════════════════════════════════════════════

void StorageManager::readMeta(HistMeta& m) {
    Preferences p;
    p.begin(NS_HIST_D, true);
    size_t s = p.getBytesLength(K_META);
    if (s == sizeof(HistMeta))
        p.getBytes(K_META, &m, sizeof(m));
    else
        m = {0, 0};   // primera vez
    p.end();
}

void StorageManager::writeMeta(const HistMeta& m) {
    Preferences p;
    p.begin(NS_HIST_D, false);
    p.putBytes(K_META, &m, sizeof(m));
    p.end();
}

DailyRecord StorageManager::readRecord(uint8_t idx) {
    char key[4];
    snprintf(key, sizeof(key), "r%02d", idx);
    DailyRecord r{};
    Preferences p;
    p.begin(NS_HIST_D, true);
    p.getBytes(key, &r, sizeof(r));
    p.end();
    return r;
}

void StorageManager::writeRecord(uint8_t idx, const DailyRecord& r) {
    char key[4];
    snprintf(key, sizeof(key), "r%02d", idx);
    Preferences p;
    p.begin(NS_HIST_D, false);
    p.putBytes(key, &r, sizeof(r));
    p.end();
}

void StorageManager::pushDailyRecord(const DailyRecord& rec) {
    HistMeta m;
    readMeta(m);

    uint8_t writeIdx;
    if (m.count < DAILY_HISTORY_SIZE) {
        // Buffer no lleno: escribimos al final
        writeIdx = (m.head + m.count) % DAILY_HISTORY_SIZE;
        m.count++;
    } else {
        // Buffer lleno: sobreescribimos el más antiguo y avanzamos head
        writeIdx = m.head;
        m.head   = (m.head + 1) % DAILY_HISTORY_SIZE;
    }

    writeRecord(writeIdx, rec);
    writeMeta(m);
    Serial0.printf("[NVS] Registro diario guardado en slot %d (%d/%d)\n",
                  writeIdx, m.count, DAILY_HISTORY_SIZE);
}

uint8_t StorageManager::getDailyHistory(DailyRecord* out, uint8_t maxCount) {
    HistMeta m;
    readMeta(m);
    uint8_t n = (m.count < maxCount) ? m.count : maxCount;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (m.head + i) % DAILY_HISTORY_SIZE;
        out[i] = readRecord(idx);
    }
    return n;
}

void StorageManager::clearDailyHistory() {
    HistMeta m{0, 0};
    writeMeta(m);
    Serial0.println("[NVS] Histórico diario borrado");
}

// ── Histórico horario ─────────────────────────────────────────────────────
// Slot = (day_epoch / 86400) % 7  →  claves "d0".."d6"
// El campo DayData::day_epoch distingue la semana actual de hace 7 días.

void StorageManager::saveHourlyRecord(uint32_t day_epoch, uint8_t hour,
                                      const HourlyRecord& rec) {
    if (hour >= 24) return;
    uint8_t slot = (day_epoch / 86400) % 7;
    char    key[3]; snprintf(key, sizeof(key), "d%d", slot);

    DayData dd{};
    Preferences p;
    p.begin(NS_HIST_H, true);
    if (p.getBytesLength(key) == sizeof(DayData))
        p.getBytes(key, &dd, sizeof(dd));
    p.end();

    if (dd.day_epoch != day_epoch) {   // slot de otro día → resetear
        dd = DayData{};
        dd.day_epoch = day_epoch;
    }
    dd.hours[hour] = rec;

    p.begin(NS_HIST_H, false);
    p.putBytes(key, &dd, sizeof(dd));
    p.end();

    Serial0.printf("[NVS] Hora %02d guardada en slot d%d\n", hour, slot);
}

bool StorageManager::getDayData(uint32_t day_epoch, DayData& out) {
    uint8_t slot = (day_epoch / 86400) % 7;
    char    key[3]; snprintf(key, sizeof(key), "d%d", slot);

    Preferences p;
    p.begin(NS_HIST_H, true);
    bool ok = (p.getBytesLength(key) == sizeof(DayData));
    if (ok) p.getBytes(key, &out, sizeof(out));
    p.end();

    return ok && (out.day_epoch == day_epoch);
}

// ── Config gráfica ────────────────────────────────────────────────────────
void StorageManager::saveChartConfig(const ChartConfig& cfg) {
    Preferences p;
    p.begin(NS_CFG, false);
    p.putBool("ch_auto", cfg.autoscale);
    p.putUChar("ch_kw",  cfg.max_kw);
    p.end();
}

ChartConfig StorageManager::loadChartConfig() {
    Preferences p;
    p.begin(NS_CFG, true);
    ChartConfig cfg;
    cfg.autoscale = p.getBool("ch_auto",  CHART_AUTOSCALE_DEF);
    cfg.max_kw    = p.getUChar("ch_kw",   CHART_MAX_KW_DEF);
    p.end();
    return cfg;
}

bool StorageManager::getDayRecord(uint32_t day_epoch, DailyRecord& out) {
    DailyRecord buf[DAILY_HISTORY_SIZE];
    uint8_t n = getDailyHistory(buf, DAILY_HISTORY_SIZE);
    for (uint8_t i = 0; i < n; i++) {
        if (buf[i].timestamp == day_epoch) { out = buf[i]; return true; }
    }
    return false;
}

void StorageManager::saveSessionState(const SessionState& s) {
    Preferences p;
    p.begin(NS_CFG, false);
    p.putBytes(K_SESSION, &s, sizeof(s));
    p.end();
}

bool StorageManager::loadSessionState(SessionState& s) {
    Preferences p;
    p.begin(NS_CFG, true);
    bool ok = (p.getBytesLength(K_SESSION) == sizeof(SessionState));
    if (ok) p.getBytes(K_SESSION, &s, sizeof(s));
    p.end();
    return ok && s.valid;
}
