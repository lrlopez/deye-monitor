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
static const char* K_MDNS    = "mdns_host";

// ── Claves histórico ──────────────────────────────────────────────────────
static const char* K_META    = "meta";
// Los registros se guardan como "rXX" donde XX es el índice (00-89)
// Ejemplo: "r00", "r01", ..., "r89"
static const char* K_SESSION = "session";

// ═════════════════════════════════════════════════════════════════════════
// Config
// ═════════════════════════════════════════════════════════════════════════

static void readConfigNvs(AppConfig& out) {
    Preferences p;
    p.begin(NS_CFG, /*readOnly=*/true);

    // Fallback a los #define de config.h si no hay valor guardado
    String ssid = p.getString(K_SSID,    WIFI_SSID);
    String pass = p.getString(K_PASS,    WIFI_PASS);
    String lip  = p.getString(K_LIP,     LOGGER_IP);
    String mdns = p.getString(K_MDNS,    MDNS_HOSTNAME);
    out.logger_serial = p.getULong(K_LSERIAL, LOGGER_SERIAL);

    ssid.toCharArray(out.wifi_ssid,      sizeof(out.wifi_ssid));
    pass.toCharArray(out.wifi_pass,      sizeof(out.wifi_pass));
    lip.toCharArray(out.logger_ip,       sizeof(out.logger_ip));
    mdns.toCharArray(out.mdns_hostname,  sizeof(out.mdns_hostname));

    out.inv_max_w  = p.getUShort("inv_max_w",  INV_MAX_W_DEF);
    out.grid_max_w = p.getUShort("grid_max_w", GRID_MAX_W_DEF);
    out.bat_cap_w  = p.getUShort("bat_cap_w",  BAT_CAP_W_DEF);

    p.end();
    DBGSERIAL.printf("[NVS] Config cargada: SSID=%s  IP=%s  Serial=%lu  mDNS=%s\n",
                  out.wifi_ssid, out.logger_ip,
                  (unsigned long)out.logger_serial, out.mdns_hostname);
}

static void writeConfigNvs(const AppConfig& cfg) {
    Preferences p;
    p.begin(NS_CFG, /*readOnly=*/false);
    p.putString(K_SSID,    cfg.wifi_ssid);
    p.putString(K_PASS,    cfg.wifi_pass);
    p.putString(K_LIP,     cfg.logger_ip);
    p.putULong(K_LSERIAL,  cfg.logger_serial);
    p.putString(K_MDNS,    cfg.mdns_hostname);
    p.putUShort("inv_max_w",  cfg.inv_max_w);
    p.putUShort("grid_max_w", cfg.grid_max_w);
    p.putUShort("bat_cap_w",  cfg.bat_cap_w);
    p.end();
    DBGSERIAL.println("[NVS] Config guardada");
}

// ── Config gráfica ────────────────────────────────────────────────────────
static void writeChartConfigNvs(const ChartConfig& cfg) {
    Preferences p;
    p.begin(NS_CFG, false);
    p.putBool("ch_auto", cfg.autoscale);
    p.putUChar("ch_kw",  cfg.max_kw);
    p.end();
}

static ChartConfig readChartConfigNvs() {
    Preferences p;
    p.begin(NS_CFG, true);
    ChartConfig cfg;
    cfg.autoscale = p.getBool("ch_auto",  CHART_AUTOSCALE_DEF);
    cfg.max_kw    = p.getUChar("ch_kw",   CHART_MAX_KW_DEF);
    p.end();
    return cfg;
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

static void writeTelegramConfigNvs(const TelegramConfig& cfg) {
    Preferences p;
    p.begin(NS_CFG, false);
    p.putString("tg_token",  cfg.token);
    p.putString("tg_chatid", cfg.chat_id);
    p.putUChar("tg_batt",    cfg.batt_threshold);
    p.putUChar("tg_bwarn",   cfg.batt_warn);
    p.putBool("tg_solar",    cfg.notify_solar);
    p.putBool("tg_grid",     cfg.notify_grid);
    p.putBool("tg_logger",   cfg.notify_logger);
    p.end();
}

static TelegramConfig readTelegramConfigNvs() {
    TelegramConfig cfg{};
    Preferences p;
    p.begin(NS_CFG, true);
    String tok = p.getString("tg_token",  "");
    String cid = p.getString("tg_chatid", "");
    tok.toCharArray(cfg.token,   sizeof(cfg.token)   - 1);
    cid.toCharArray(cfg.chat_id, sizeof(cfg.chat_id) - 1);
    cfg.batt_threshold = p.getUChar("tg_batt",  20);
    cfg.batt_warn      = p.getUChar("tg_bwarn", 25);
    cfg.notify_solar   = p.getBool("tg_solar",  true);
    cfg.notify_grid    = p.getBool("tg_grid",   true);
    cfg.notify_logger  = p.getBool("tg_logger", true);
    p.end();
    return cfg;
}

static void writeBacklightConfigNvs(const BacklightConfig& cfg) {
    Preferences p; p.begin(NS_CFG, false);
    p.putUChar("bl_op",    cfg.op_pct);
    p.putUChar("bl_norm",   cfg.normal_pct);
    p.putUChar("bl_red",    cfg.reduced_pct);
    p.putBool ("bl_inact",  cfg.inactivity_enabled);
    p.putUChar("bl_isecs",  cfg.inactivity_div10);
    p.putBool ("bl_night",  cfg.night_enabled);
    p.putUChar("bl_nstart", cfg.night_start_h);
    p.putUChar("bl_nend",   cfg.night_end_h);
    p.end();
}

static BacklightConfig readBacklightConfigNvs() {
    BacklightConfig cfg{};
    Preferences p; p.begin(NS_CFG, true);
    cfg.op_pct             = p.getUChar("bl_op",    100);
    cfg.normal_pct         = p.getUChar("bl_norm",   70);
    cfg.reduced_pct        = p.getUChar("bl_red",    10);
    cfg.inactivity_enabled = p.getBool ("bl_inact",  true);
    cfg.inactivity_div10   = p.getUChar("bl_isecs",  6);
    cfg.night_enabled      = p.getBool ("bl_night",  true);
    cfg.night_start_h      = p.getUChar("bl_nstart", 0);
    cfg.night_end_h        = p.getUChar("bl_nend",   8);
    p.end();
    return cfg;
}

static void writeAdminPasswordNvs(const char* password) {
    Preferences p;
    p.begin(NS_CFG, false);
    p.putString("web_pass", password);
    p.end();
}

static String readAdminPasswordNvs() {
    Preferences p;
    p.begin(NS_CFG, true);
    String s = p.getString("web_pass", "");
    p.end();
    return s;
}

uint8_t StorageManager::loadDashView() {
    Preferences p;
    p.begin(NS_CFG, true);
    uint8_t v = p.getUChar("dash_view", 0);
    p.end();
    return v > 1 ? 0 : v;
}

void StorageManager::saveDashView(uint8_t view) {
    if (loadDashView() == view) return;       // no gastar ciclos de NVS si no cambia
    Preferences p;
    p.begin(NS_CFG, false);
    p.putUChar("dash_view", view);
    p.end();
}

bool StorageManager::loadClockMode() {
    Preferences p;
    p.begin(NS_CFG, true);
    bool v = p.getBool("ui_clock", true);
    p.end();
    return v;
}

void StorageManager::saveClockMode(bool enabled) {
    if (loadClockMode() == enabled) return;
    Preferences p;
    p.begin(NS_CFG, false);
    p.putBool("ui_clock", enabled);
    p.end();
}

uint8_t StorageManager::loadHints() {
    Preferences p;
    p.begin(NS_CFG, true);
    uint8_t v = p.getUChar("ui_hints", 0);
    p.end();
    return v;
}

void StorageManager::saveHints(uint8_t bits) {
    if (loadHints() == bits) return;
    Preferences p;
    p.begin(NS_CFG, false);
    p.putUChar("ui_hints", bits);
    p.end();
}

bool StorageManager::loadFsKnownGood() {
    Preferences p;
    p.begin(NS_CFG, true);
    bool ok = p.getBool("fs_ok", false);
    p.end();
    return ok;
}

void StorageManager::saveFsKnownGood() {
    if (loadFsKnownGood()) return;
    Preferences p;
    p.begin(NS_CFG, false);
    p.putBool("fs_ok", true);
    p.end();
}

// ═════════════════════════════════════════════════════════════════════════
// Caché en RAM de la configuración (DEYE-43, DEYE-22)
// ═════════════════════════════════════════════════════════════════════════
// Antes cada loadXxx() abría Preferences y leía NVS: la tarea de red recargaba
// la config de Telegram cada 60 s y cada petición web protegida releía la
// contraseña. Ahora la primera lectura puebla la caché y las siguientes
// devuelven una copia desde RAM. Cualquier save* la invalida; la siguiente
// lectura vuelve a NVS, así la caché nunca difiere de lo realmente guardado
// (p.ej. cadenas truncadas al guardar). Se llama desde varias tareas
// (red, Telegram, web, LVGL), de ahí el mutex.
namespace {
struct ConfigCache {
    SemaphoreHandle_t mtx;
    bool cfg_ok = false, chart_ok = false, tg_ok = false, bl_ok = false, pw_ok = false;
    AppConfig       cfg{};
    ChartConfig     chart{};
    TelegramConfig  tg{};
    BacklightConfig bl{};
    String          pw;
    ConfigCache() : mtx(xSemaphoreCreateMutex()) {}
};
ConfigCache& cache() { static ConfigCache c; return c; }   // inicialización thread-safe (C++11)

struct CacheLock {
    CacheLock()  { xSemaphoreTake(cache().mtx, portMAX_DELAY); }
    ~CacheLock() { xSemaphoreGive(cache().mtx); }
};
}  // namespace

void StorageManager::loadConfig(AppConfig& out) {
    CacheLock l; ConfigCache& c = cache();
    if (!c.cfg_ok) { readConfigNvs(c.cfg); c.cfg_ok = true; }
    out = c.cfg;
}
void StorageManager::saveConfig(const AppConfig& cfg) {
    CacheLock l; writeConfigNvs(cfg); cache().cfg_ok = false;
}

ChartConfig StorageManager::loadChartConfig() {
    CacheLock l; ConfigCache& c = cache();
    if (!c.chart_ok) { c.chart = readChartConfigNvs(); c.chart_ok = true; }
    return c.chart;
}
void StorageManager::saveChartConfig(const ChartConfig& cfg) {
    CacheLock l; writeChartConfigNvs(cfg); cache().chart_ok = false;
}

TelegramConfig StorageManager::loadTelegramConfig() {
    CacheLock l; ConfigCache& c = cache();
    if (!c.tg_ok) { c.tg = readTelegramConfigNvs(); c.tg_ok = true; }
    return c.tg;
}
void StorageManager::saveTelegramConfig(const TelegramConfig& cfg) {
    CacheLock l; writeTelegramConfigNvs(cfg); cache().tg_ok = false;
}

BacklightConfig StorageManager::loadBacklightConfig() {
    CacheLock l; ConfigCache& c = cache();
    if (!c.bl_ok) { c.bl = readBacklightConfigNvs(); c.bl_ok = true; }
    return c.bl;
}
void StorageManager::saveBacklightConfig(const BacklightConfig& cfg) {
    CacheLock l; writeBacklightConfigNvs(cfg); cache().bl_ok = false;
}

String StorageManager::loadAdminPassword() {
    CacheLock l; ConfigCache& c = cache();
    if (!c.pw_ok) { c.pw = readAdminPasswordNvs(); c.pw_ok = true; }
    return c.pw;
}
void StorageManager::saveAdminPassword(const char* password) {
    CacheLock l; writeAdminPasswordNvs(password); cache().pw_ok = false;
}