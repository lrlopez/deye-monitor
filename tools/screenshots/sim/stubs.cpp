#include "storage.h"
void StorageManager::loadConfig(AppConfig& o) { memset(&o, 0, sizeof o); o.inv_max_w = 6000; o.grid_max_w = 6000; o.bat_cap_w = 16000; strcpy(o.logger_ip, "192.168.1.50"); o.logger_serial = 2712345678UL; strcpy(o.wifi_ssid, "MiRed"); strcpy(o.mdns_hostname, "inversor"); }
extern "C" { void lv_os_init(void) {} int lv_lock(void) { return 0; } void lv_unlock(void) {} }
uint8_t g_dash_view = 0;
uint8_t StorageManager::loadDashView() { return g_dash_view; }
void StorageManager::saveDashView(uint8_t v) { g_dash_view = v; }
#include "psram_cache.h"
#include "time_utils.h"
// Datos de prueba: "ayer" tuvo consumo 13 kWh / producción 20 kWh acumulándose a lo largo del día
static HourlyRecord s_hy[24];
bool g_has_week = true;
bool g_rich_days = false;
static void fill_hy() {
    static bool done = false; if (done) return; done = true;
    for (int h = 0; h < 24; h++) {
        float f = h < 7 ? 0.05f * h : (h < 19 ? 0.35f + (h - 7) * 0.0525f : 1.0f);   // curva acumulada
        s_hy[h].flags = 0x01;
        s_hy[h].day_load = (uint16_t)(130 * (0.1f + 0.9f * f));
        s_hy[h].day_pv   = (uint16_t)(200 * f);
        float sun = (h >= 7 && h <= 19) ? sinf((h - 7) * 3.14159f / 12.0f) : 0.0f;
        s_hy[h].avg_pv_w = (int16_t)(4200 * sun);
        s_hy[h].avg_load_w = (int16_t)(500 + 700 * (h > 17 && h < 23 ? 1 : 0.3f) + 200 * sinf(h));
        s_hy[h].avg_grid_w = (int16_t)(sun > 0.3f ? -(int)(1800 * sun) : 400);
        s_hy[h].avg_batt_w = (int16_t)(sun > 0.3f ? -(int)(1400 * sun) : (h > 17 ? 900 : 0));
        s_hy[h].soc_end = (uint8_t)(h < 7 ? 35 : (h < 17 ? 35 + (h - 7) * 6 : 95 - (h - 17) * 12));
        s_hy[h].sample_count = 12;
    }
}
const HourlyRecord* PsramCache::getHourly(uint32_t) { fill_hy(); return s_hy; }
bool PsramCache::getDaily(uint32_t dep, DailyRecord& out) {
    // El día "de hoy - 1" y "de hoy - 7" existen; el resto, no
    time_t now = time(nullptr); struct tm t; localtime_r(&now, &t); t.tm_hour = t.tm_min = t.tm_sec = 0; t.tm_isdst = -1;
    uint32_t today = (uint32_t)mktime(&t);
    out = DailyRecord{}; out.flags = 0x03;
    if (g_rich_days && dep < today && dep + 200u * 86400u > today) {   // histórico de ejemplo para capturas del manual
        const int k = (int)((today - dep) / 86400u);
        const float sun = 0.35f + 0.65f * fabsf(sinf(k * 0.83f + 0.4f));
        out.pv_10wh = (uint16_t)(260 * sun); out.load_10wh = (uint16_t)(105 + 25 * fabsf(sinf(k * 1.7f)));
        out.export_10wh = (uint16_t)(out.pv_10wh * 0.35f); out.import_10wh = (uint16_t)(out.load_10wh * (0.45f - 0.3f * sun));
        out.bchg_10wh = (uint16_t)(out.pv_10wh * 0.22f); out.bdis_10wh = (uint16_t)(out.load_10wh * 0.30f);
        return true;
    }
    if (dep == local_midnight_plus_days(today, -1)) { out.load_10wh = 130; out.pv_10wh = 200; return true; }
    if (g_has_week && dep == local_midnight_plus_days(today, -7)) { out.load_10wh = 118; out.pv_10wh = 260; return true; }
    return false;
}
bool g_clock_mode = true;
bool StorageManager::loadClockMode() { return g_clock_mode; }
void StorageManager::saveClockMode(bool v) { g_clock_mode = v; }
ChartConfig StorageManager::loadChartConfig() { ChartConfig c; c.autoscale = true; c.max_kw = 6; return c; }
#include "telegram.h"
#include "backlight.h"
bool StorageManager::loadWizardDone() { return false; } void StorageManager::saveWizardDone(bool) {}
uint8_t StorageManager::loadTimezone() { return 0; } void StorageManager::saveTimezone(uint8_t) {}
volatile uint32_t g_batt_eta = (1u << 16) | 320;   // EMPTY, 5h20
int g_scan_mode = 0;
int logger_scan(char (*ips)[16], int max) { int n = g_scan_mode > max ? max : g_scan_mode; for (int i = 0; i < n; i++) snprintf(ips[i], 16, "192.168.1.%d", 50 + i * 17); return n; }
bool logger_verify(const char*, uint32_t, uint8_t) { return true; }
EspMock ESP; TelegramMock g_tg; BacklightMock g_bl; FakeFS LittleFS; FakeStore g_store;
#include "telegram.h"
TelegramConfig StorageManager::loadTelegramConfig() { TelegramConfig t{}; t.batt_warn = 25; t.batt_threshold = 20; t.notify_solar = t.notify_grid = t.notify_logger = true; return t; }
BacklightConfig StorageManager::loadBacklightConfig() { BacklightConfig b{}; b.op_pct = 100; b.normal_pct = 70; b.reduced_pct = 10; b.inactivity_enabled = true; b.inactivity_div10 = 6; b.night_enabled = true; b.night_start_h = 22; b.night_end_h = 6; return b; }
String StorageManager::loadAdminPassword() { return ""; }
void StorageManager::saveConfig(const AppConfig&) {} void StorageManager::saveChartConfig(const ChartConfig&) {}
void StorageManager::saveTelegramConfig(const TelegramConfig&) {} void StorageManager::saveAdminPassword(const char*) {}

// ── Actualización de firmware (fw_update.cpp): estado simulado para las capturas ──────────────
#include "fw_update.h"
int g_fw_mock = 0;   // 0 sin consultar · 1 lista tras la consulta · 2 instalando · 3 error de modelo de datos
int  fw_data_model() { return 2; }
bool fw_update_check_async() { g_fw_mock = 1; return true; }
bool fw_update_install_async(const char*) { g_fw_mock = 2; return true; }
void fw_update_status(FwStatus& o) {
    memset(&o, 0, sizeof o);
    strcpy(o.env, "guition_jc4880p443c"); strcpy(o.current, "4.4.2"); o.data_model = 2;
    o.state = FwState::IDLE;
    if (g_fw_mock == 1) { o.state = FwState::AVAILABLE; strcpy(o.latest, "v4.5.0"); o.n_releases = 4; }
    if (g_fw_mock == 2) { o.state = FwState::DOWNLOADING; strcpy(o.target, "v4.5.0"); o.size = 2274368; o.done = 1380000; }
    if (g_fw_mock == 3) { o.state = FwState::ERROR; strcpy(o.latest, "v4.5.0"); o.n_releases = 4;
                          strcpy(o.error, "Modelo de datos 1 < 2 instalado: no se puede bajar a esa versi\xC3\xB3n"); }
}
int fw_update_releases(FwReleaseInfo* out, int max) {
    if (g_fw_mock == 0 || g_fw_mock == 2) return 0;
    static const struct { const char* tag; bool pre; int8_t cmp; } R[] = {
        { "v4.5.0", false, 1 }, { "v4.4.2", false, 0 }, { "v4.4.1", false, -1 }, { "v4.4.0", false, -1 } };
    int n = 0;
    for (; n < 4 && n < max; n++) {
        memset(&out[n], 0, sizeof out[n]); strcpy(out[n].tag, R[n].tag);
        out[n].prerelease = R[n].pre; out[n].cmp = R[n].cmp; out[n].installed = R[n].cmp == 0;
        out[n].size = 2274368; out[n].has_digest = true;
    }
    return n;
}
