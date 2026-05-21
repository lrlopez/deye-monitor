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

void StorageManager::loadConfig(AppConfig& out) {
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

    p.end();
    DBGSERIAL.printf("[NVS] Config cargada: SSID=%s  IP=%s  Serial=%lu  mDNS=%s\n",
                  out.wifi_ssid, out.logger_ip,
                  (unsigned long)out.logger_serial, out.mdns_hostname);
}

void StorageManager::saveConfig(const AppConfig& cfg) {
    Preferences p;
    p.begin(NS_CFG, /*readOnly=*/false);
    p.putString(K_SSID,    cfg.wifi_ssid);
    p.putString(K_PASS,    cfg.wifi_pass);
    p.putString(K_LIP,     cfg.logger_ip);
    p.putULong(K_LSERIAL,  cfg.logger_serial);
    p.putString(K_MDNS,    cfg.mdns_hostname);
    p.end();
    DBGSERIAL.println("[NVS] Config guardada");
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

void StorageManager::saveTelegramConfig(const TelegramConfig& cfg) {
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

TelegramConfig StorageManager::loadTelegramConfig() {
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

void StorageManager::saveBacklightConfig(const BacklightConfig& cfg) {
    Preferences p; p.begin(NS_CFG, false);
    p.putUChar("bl_norm",   cfg.normal_pct);
    p.putUChar("bl_red",    cfg.reduced_pct);
    p.putBool ("bl_inact",  cfg.inactivity_enabled);
    p.putUChar("bl_isecs",  cfg.inactivity_div10);
    p.putBool ("bl_night",  cfg.night_enabled);
    p.putUChar("bl_nstart", cfg.night_start_h);
    p.putUChar("bl_nend",   cfg.night_end_h);
    p.end();
}

BacklightConfig StorageManager::loadBacklightConfig() {
    BacklightConfig cfg{};
    Preferences p; p.begin(NS_CFG, true);
    cfg.normal_pct         = p.getUChar("bl_norm",   90);
    cfg.reduced_pct        = p.getUChar("bl_red",    10);
    cfg.inactivity_enabled = p.getBool ("bl_inact",  true);
    cfg.inactivity_div10   = p.getUChar("bl_isecs",  6);
    cfg.night_enabled      = p.getBool ("bl_night",  true);
    cfg.night_start_h      = p.getUChar("bl_nstart", 0);
    cfg.night_end_h        = p.getUChar("bl_nend",   8);
    p.end();
    return cfg;
}

void StorageManager::saveAdminPassword(const char* password) {
    Preferences p;
    p.begin(NS_CFG, false);
    p.putString("web_pass", password);
    p.end();
}

String StorageManager::loadAdminPassword() {
    Preferences p;
    p.begin(NS_CFG, true);
    String s = p.getString("web_pass", "");
    p.end();
    return s;
}