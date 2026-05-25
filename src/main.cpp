#include <Arduino.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <lvgl.h>
#include <time.h>
#include <LittleFS.h>

#include "config.h"
#include "storage.h"
#include "data_store.h"
#include "solarman.h"
#include "dashboard.h"
#include "stats_screen.h"
#include "chart_screen.h"
#include "config_screen.h"
#include "energy_profile.h"
#include "splash_screen.h"
#include "web_server.h"
#include "telegram.h"
#include "psram_alloc.h"
#include "psram_cache.h"
#include "backlight.h"
#include "pagination_dots.h"
#include "screen_banner.h"


/* Change to your screen resolution */
static uint32_t screenWidth = 480;
static uint32_t screenHeight = 272;

#include <Arduino_GFX_Library.h>

#if defined(BOARD_GUITION_JC1060P470)
#define TFT2_BL 23 // Backlight GPIO for Guition JC1060P470

//               vsync_pulse_width, vsync_back_porch, vsync_front_porch, prefer_speed, lane_bit_rate)
Arduino_ESP32DSIPanel *dsipanel = new Arduino_ESP32DSIPanel(
    160 /* hsync_pulse_width */, 160 /* hsync_back_porch */, 40 /* hsync_front_porch */,
    23  /* vsync_pulse_width */, 12  /* vsync_back_porch */, 10 /* vsync_front_porch */,
    48000000 /* prefer_speed */
);
Arduino_GFX *gfx = new Arduino_DSI_Display(
    1024, 600, dsipanel, 0 /* rotation */, true /* auto_flush */,
    GFX_NOT_DEFINED /* rst */,
    jd9165_init_operations, sizeof(jd9165_init_operations) / sizeof(lcd_init_cmd_t));
#else
#define GFX_BL DF_GFX_BL // default backlight pin, you may replace DF_GFX_BL to actual backlight pin
#define TFT2_BL 2

Arduino_ESP32RGBPanel *panel = new Arduino_ESP32RGBPanel(
    40 /* DE */, 41 /* VSYNC */, 39 /* HSYNC */, 42 /* DCLK */,
    45 /* R0 */, 48 /* R1 */, 47 /* R2 */, 21 /* R3 */, 14 /* R4 */,
    5 /* G0 */, 6 /* G1 */, 7 /* G2 */, 15 /* G3 */, 16 /* G4 */, 4 /* G5 */,
    8 /* B0 */, 3 /* B1 */, 46 /* B2 */, 9 /* B3 */, 1 /* B4 */,
    0 /*hsync_polarity*/, 1 /* hsync_front_porch*/, 4 /* hsync_pulse_width*/, 43 /* hsync_back_porch*/,
    0 /*vsync_polarity*/, 3 /*vsync_front_porch*/, 4 /*vsync_pulse_width*/, 12 /*vsync_back_porch*/,
    1 /*pclk_active_neg*/, 9000000 /*prefer_speed*/, false /*useBigEndian*/,
    0 /*de_idle_high*/, 0 /*pclk_idle_high*/
);

// Original: 8 /* hsync_front_porch*/ y 8 /*vsync_front_porch*/
Arduino_GFX *gfx = new Arduino_RGB_Display(screenWidth, screenHeight, panel);
#endif

/*******************************************************************************
 * End of Arduino_GFX setting
 ******************************************************************************/

#include "touch.h"

static lv_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf;
static lv_color_t *disp_draw_buf2;
static lv_display_t *disp_drv;
static unsigned long last_ms;

static SemaphoreHandle_t s_flash_display_mutex;  // definido más abajo, forward-decl para flush_cb

/* Display flushing */
void my_disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
#if defined(BOARD_GUITION_JC1060P470)
    // esp_lcd_panel_draw_bitmap usa DMA2D para copiar el frame al back-buffer y
    // programa el swap en el siguiente vsync → sin tearing. También bloquea hasta
    // que DMA2D termina, por lo que px_map es libre al salir.
    // El mutex serializa con las escrituras flash de Core 0.
    if (s_flash_display_mutex) xSemaphoreTake(s_flash_display_mutex, portMAX_DELAY);
    esp_lcd_panel_draw_bitmap(dsipanel->getPanelHandle(),
                              area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    if (s_flash_display_mutex) xSemaphoreGive(s_flash_display_mutex);
#else
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*) px_map, w, h);
#endif
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    if (touch_has_signal())
    {
        if (touch_touched())
        {
            data->state = LV_INDEV_STATE_PR;

            data->point.x = touch_last_x;
            data->point.y = touch_last_y;

            //Mostrar coordenadas por consola
            //DBGSERIAL.printf("%d, %d\n", data->point.x, data->point.y);
        }
        else if (touch_released())
        {
            data->state = LV_INDEV_STATE_REL;
        }
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }
}

uint32_t esp_tick_get_cb() {
    return esp_timer_get_time() / 1000;
}

uint32_t                 g_uptime_start = 0;
EnergyData               g_energy;
DailyStats               g_daily;
SemaphoreHandle_t        g_mutex;
static volatile bool     g_energy_ready = false;
static volatile bool     g_daily_ready  = false;
static lv_obj_t*         g_tile_dash    = nullptr;
static lv_obj_t*         g_tile_chart   = nullptr;
static lv_obj_t*         g_tile_stats   = nullptr;
static lv_obj_t*         g_tile_energy  = nullptr;
static lv_obj_t*         g_tile_config  = nullptr;

// Config en RAM cargada desde NVS al arrancar
static AppConfig g_cfg;
// Flag: WiFi conectado (escrito por solarmanTask, leído por loop)
static volatile bool g_wifi_connected = false;
// Flags OTA (protocolo Core0→Core1):
//   g_ota_request: webserver pide mostrar overlay
//   g_ota_active:  loop confirma que el overlay está pintado y el render está congelado
volatile bool g_ota_request = false;
volatile bool g_ota_active  = false;

// Pantalla principal (tileview vive aquí)
lv_obj_t* g_main_screen = nullptr;

// ── Tarea de red (Core 0) ─────────────────────────────────────────────────
static void solarmanTask(void* /*pv*/) {
    SolarmanClient client(g_cfg.logger_ip, LOGGER_PORT,
                          g_cfg.logger_serial, MODBUS_UNIT_ID);
    EnergyData  local_e{};
    DailyStats  local_d{};

    static int32_t  s_cur_5min_slot = -1;
    static int32_t  s_cur_hour      = -1;
    static int32_t  s_cur_day       = -1;
    static bool     s_startup_done  = false;

    static float    s_acc_pv = 0, s_acc_grid = 0;
    static float    s_acc_batt = 0, s_acc_load = 0;
    static int      s_acc_n   = 0;
    static uint8_t  s_acc_soc = 0;

    static uint16_t s_snap_day_pv = 0, s_snap_day_exp = 0;
    static uint16_t s_snap_day_imp = 0, s_snap_day_load = 0;
    static uint16_t s_snap_day_bchg = 0, s_snap_day_bdis = 0;
    static bool     s_snap_valid = false;

    // ── Estado de alertas ─────────────────────────────────────────────────
    static bool     s_solar_active    = false;
    static uint8_t  s_solar_debounce  = 0;
    static bool     s_solar_restored  = false;  // cargado desde NVS una sola vez

    enum class BattState : uint8_t { NORMAL, WARN, CRIT };
    static BattState s_batt_state = BattState::NORMAL;

    static uint8_t  s_logger_fail_cnt = 0;
    static bool     s_logger_notified = false;

    static bool     s_grid_outage     = false;
    static uint8_t  s_grid_debounce   = 0;
    static bool     s_had_grid        = false;  // al menos 1 lectura con red activa

    // Config Telegram cacheada — se recarga desde NVS cada 60 s
    static bool           s_tgcfg_loaded  = false;
    static TelegramConfig s_tgcfg{};
    static uint32_t       s_tgcfg_last_ms = 0;

    static uint8_t  s_soc_start_of_day = 0;

    static bool s_wifi_ever_connected = false;

    for (;;) {
        // ── Reconexión WiFi ───────────────────────────────────────────────
        if (WiFi.status() != WL_CONNECTED) {
            g_wifi_connected = false;
            // Sin SSID configurado no hay nada que intentar
            if (g_cfg.wifi_ssid[0] == '\0') {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            static uint32_t last_reconnect = 0;
            if (millis() - last_reconnect > 15000) {
                DBGSERIAL.println("[WiFi] Reconectando...");
                // Solo desconectar si alguna vez estuvo conectado;
                // en P4 llamar disconnect() antes de la primera conexión puede crashear.
                if (s_wifi_ever_connected) WiFi.disconnect();
                WiFi.begin(g_cfg.wifi_ssid, g_cfg.wifi_pass);
                last_reconnect = millis();
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // ── WiFi recién conectado ─────────────────────────────────────────
        if (!g_wifi_connected) {
            g_wifi_connected = true;
            s_wifi_ever_connected = true;
            DBGSERIAL.printf("[WiFi] Conectado: %s\n",
                          WiFi.localIP().toString().c_str());
            uint32_t ntp_wait = millis();
            while (time(nullptr) < 1700000000UL &&
                   millis() - ntp_wait < 10000)
                vTaskDelay(pdMS_TO_TICKS(500));
            if (time(nullptr) > 1700000000UL) {
                DBGSERIAL.println("[NTP] Sincronizado");
                // Corregir los slots del cache que se inicializaron en begin()
                // con el reloj en 1970 (antes de tener WiFi/NTP).
                Cache.reinitAfterNtp();
            } else {
                DBGSERIAL.println("[NTP] Timeout");
            }
            if (MDNS.begin(g_cfg.mdns_hostname)) {
                MDNS.addService("http", "tcp", 80);
                DBGSERIAL.printf("[mDNS] Activo: %s.local\n", g_cfg.mdns_hostname);
            } else {
                DBGSERIAL.println("[mDNS] Error al iniciar");
            }
        }

        // ── 1. Datos instantáneos ─────────────────────────────────────────
        if (client.fetchEnergyData(local_e)) {
            s_logger_fail_cnt = 0;
            s_logger_notified = false;
            DBGSERIAL.printf("[Live] PV:%dW Grid:%dW Bat:%dW(%d%%) Load:%dW\n",
                (int)local_e.pv_power, (int)local_e.grid_power,
                (int)local_e.batt_power, (int)local_e.batt_soc,
                (int)local_e.load_power);
            if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_energy       = local_e;
                g_energy_ready = true;
                xSemaphoreGive(g_mutex);
            }
        } else {
            // 5 fallos consecutivos (~25 s) → notificar una sola vez
            if (++s_logger_fail_cnt == 5 && !s_logger_notified
                    && Telegram.isConfigured()) {
                if (!s_tgcfg_loaded) {
                    s_tgcfg = Storage.loadTelegramConfig();
                    s_tgcfg_loaded  = true;
                    s_tgcfg_last_ms = millis();
                }
                if (s_tgcfg.notify_logger)
                    Telegram.enqueueAlert(AlertType::LOGGER_FAIL);
                screen_banner_enqueue(AlertType::LOGGER_FAIL);
                s_logger_notified = true;
            }
        }

        // ── 2. Datos diarios ──────────────────────────────────────────────
        static uint32_t last_daily_ms = millis() - POLL_DAILY_MS + POLL_INTERVAL_MS;
        if (millis() - last_daily_ms >= POLL_DAILY_MS) {
            if (client.fetchDailyStats(local_d)) {
                last_daily_ms = millis();
                // Actualizar g_daily para el servidor web
                if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    g_daily       = local_d;
                    g_daily_ready = true;
                    xSemaphoreGive(g_mutex);
                }
                // Inicialización tardía de snaps si el startup ocurrió sin daily data
                if (s_startup_done && !s_snap_valid) {
                    s_snap_day_pv   = (uint16_t)(local_d.pv_kwh            * 10.0f + 0.5f);
                    s_snap_day_exp  = (uint16_t)(local_d.export_kwh         * 10.0f + 0.5f);
                    s_snap_day_imp  = (uint16_t)(local_d.import_kwh         * 10.0f + 0.5f);
                    s_snap_day_load = (uint16_t)(local_d.load_kwh           * 10.0f + 0.5f);
                    s_snap_day_bchg = (uint16_t)(local_d.batt_charge_kwh    * 10.0f + 0.5f);
                    s_snap_day_bdis = (uint16_t)(local_d.batt_discharge_kwh * 10.0f + 0.5f);
                    s_snap_valid    = true;
                    DBGSERIAL.println("[Record] Snaps diarios inicializados tardíamente");
                }
            }
        }

        // ── 3. Grabación ──────────────────────────────────────────────────
        uint32_t now_ep    = (uint32_t)time(nullptr);
        uint32_t slot_5min = now_ep / 300;

        if (now_ep < 1700000000UL) goto end_record;   // NTP no listo

        // Startup: inicializar estado desde la primera lectura válida de energía.
        // Los datos diarios (local_d) son opcionales aquí; si aún no están disponibles
        // se inicializarán cuando lleguen (ver bloque fetchDailyStats más arriba).
        if (!s_startup_done) {
            if (!local_e.valid) goto end_record;
            s_startup_done  = true;
            s_cur_5min_slot = (int32_t)slot_5min;

            struct tm now_tm; getLocalTime(&now_tm, 500);
            s_cur_hour = now_tm.tm_hour;
            s_cur_day  = now_tm.tm_mday;

            if (local_d.valid) {
                s_snap_day_pv   = (uint16_t)(local_d.pv_kwh             * 10.0f + 0.5f);
                s_snap_day_exp  = (uint16_t)(local_d.export_kwh          * 10.0f + 0.5f);
                s_snap_day_imp  = (uint16_t)(local_d.import_kwh          * 10.0f + 0.5f);
                s_snap_day_load = (uint16_t)(local_d.load_kwh            * 10.0f + 0.5f);
                s_snap_day_bchg = (uint16_t)(local_d.batt_charge_kwh     * 10.0f + 0.5f);
                s_snap_day_bdis = (uint16_t)(local_d.batt_discharge_kwh  * 10.0f + 0.5f);
                s_snap_valid    = true;
            }
            s_soc_start_of_day = (uint8_t)local_e.batt_soc;

            // Publicar en cache el primer valor del día
            {
                struct tm today_tm = now_tm;
                today_tm.tm_hour = 0; today_tm.tm_min = 0;
                today_tm.tm_sec  = 0; today_tm.tm_isdst = -1;
                uint32_t dep = (uint32_t)mktime(&today_tm);
                DailyRecord dr{};
                dr.day_epoch   = dep;
                dr.pv_10wh     = s_snap_day_pv;
                dr.export_10wh = s_snap_day_exp;
                dr.import_10wh = s_snap_day_imp;
                dr.load_10wh   = s_snap_day_load;
                dr.bchg_10wh   = s_snap_day_bchg;
                dr.bdis_10wh   = s_snap_day_bdis;
                dr.soc_start   = s_soc_start_of_day;
                dr.soc_end     = (uint8_t)local_e.batt_soc;
                dr.flags       = 0x01;
                if (s_flash_display_mutex) xSemaphoreTake(s_flash_display_mutex, portMAX_DELAY);
                Cache.pushDaily(dr);
                if (s_flash_display_mutex) xSemaphoreGive(s_flash_display_mutex);
            }

            // Restaurar estado solar desde NVS comparando la fecha guardada con hoy.
            // Si es de otro día, resetear — la producción solar empieza de cero cada día.
            if (!s_solar_restored) {
                s_solar_restored = true;
                SessionState ss{};
                if (Storage.loadSessionState(ss) && ss.last_record_epoch > 0) {
                    struct tm saved_tm{};
                    time_t t = (time_t)ss.last_record_epoch;
                    localtime_r(&t, &saved_tm);
                    bool same_day = (saved_tm.tm_mday == now_tm.tm_mday &&
                                     saved_tm.tm_mon  == now_tm.tm_mon  &&
                                     saved_tm.tm_year == now_tm.tm_year);
                    s_solar_active = same_day ? ss.solar_active : false;
                    DBGSERIAL.printf("[Solar] Estado %s: %s\n",
                                     same_day ? "restaurado" : "reseteado (día anterior)",
                                     s_solar_active ? "activo" : "inactivo");
                }
            }

            DBGSERIAL.printf("[Record] Startup ok: slot=%lu h=%d d=%d\n",
                (unsigned long)slot_5min, s_cur_hour, s_cur_day);
            goto end_record;
        }

        if (!local_e.valid) goto end_record;

        {
            struct tm now_tm; getLocalTime(&now_tm, 500);
            int new_hour = now_tm.tm_hour;
            int new_day  = now_tm.tm_mday;

            s_acc_pv   += local_e.pv_power;
            s_acc_grid += local_e.grid_power;
            s_acc_batt += local_e.batt_power;
            s_acc_load += local_e.load_power;
            s_acc_soc   = (uint8_t)local_e.batt_soc;
            s_acc_n++;

            // ── Nuevo slot de 5 minutos ───────────────────────────────────
            if ((int32_t)slot_5min != s_cur_5min_slot) {
                s_cur_5min_slot = (int32_t)slot_5min;
                uint32_t record_ts = slot_5min * 300;
                DBGSERIAL.printf("[Record] Slot: ts=%lu\n", (unsigned long)record_ts);

                Record5Min r{};
                r.timestamp  = record_ts;
                r.pv_w       = (int16_t)constrain(local_e.pv_power,  -32767, 32767);
                r.grid_w     = (int16_t)constrain(local_e.grid_power, -32767, 32767);
                r.batt_w     = (int16_t)constrain(local_e.batt_power, -32767, 32767);
                r.load_w     = (int16_t)constrain(local_e.load_power, -32767, 32767);
                r.soc        = (uint8_t)local_e.batt_soc;
                r.flags      = 0x01;

                // Serializar con el flush del display en Core 1 (ver s_flash_display_mutex)
                if (s_flash_display_mutex) xSemaphoreTake(s_flash_display_mutex, portMAX_DELAY);
                bool push_ok = Store.push(r);
                Storage.saveSessionState({record_ts, true, s_solar_active});
                if (s_flash_display_mutex) xSemaphoreGive(s_flash_display_mutex);
                if (push_ok) Cache.pushRaw(r);
            }

            // ── Cambio de hora ────────────────────────────────────────────
            if (new_hour != s_cur_hour) {
                if (s_acc_n > 0 && s_snap_valid && local_d.valid) {
                    struct tm mid = now_tm;
                    if (new_hour == 0) mid.tm_mday--;  // mktime() normaliza mday=0 al último día del mes anterior
                    mid.tm_hour = s_cur_hour;
                    mid.tm_min = 0; mid.tm_sec = 0; mid.tm_isdst = -1;

                    uint16_t cur_pv   = (uint16_t)(local_d.pv_kwh            * 10.0f + 0.5f);
                    uint16_t cur_exp  = (uint16_t)(local_d.export_kwh         * 10.0f + 0.5f);
                    uint16_t cur_imp  = (uint16_t)(local_d.import_kwh         * 10.0f + 0.5f);
                    uint16_t cur_load = (uint16_t)(local_d.load_kwh           * 10.0f + 0.5f);
                    uint16_t cur_bchg = (uint16_t)(local_d.batt_charge_kwh    * 10.0f + 0.5f);
                    uint16_t cur_bdis = (uint16_t)(local_d.batt_discharge_kwh * 10.0f + 0.5f);

                    HourlyRecord hr{};
                    hr.hour_epoch   = (uint32_t)mktime(&mid);
                    hr.avg_pv_w     = (int16_t)(s_acc_pv   / s_acc_n);
                    hr.avg_grid_w   = (int16_t)(s_acc_grid  / s_acc_n);
                    hr.avg_batt_w   = (int16_t)(s_acc_batt  / s_acc_n);
                    hr.avg_load_w   = (int16_t)(s_acc_load  / s_acc_n);
                    hr.soc_end       = s_acc_soc;
                    hr.sample_count  = (uint8_t)min(s_acc_n, 255);
                    hr.flags         = 0x01;
                    hr.day_pv        = cur_pv;
                    hr.day_export    = cur_exp;
                    hr.day_import    = cur_imp;
                    hr.day_load      = cur_load;
                    hr.day_bchg      = cur_bchg;
                    hr.day_bdis      = cur_bdis;
                    if (s_flash_display_mutex) xSemaphoreTake(s_flash_display_mutex, portMAX_DELAY);
                    Cache.pushHourly(hr);
                    if (s_flash_display_mutex) xSemaphoreGive(s_flash_display_mutex);

                    s_snap_day_pv   = cur_pv;   s_snap_day_exp  = cur_exp;
                    s_snap_day_imp  = cur_imp;  s_snap_day_load = cur_load;
                    s_snap_day_bchg = cur_bchg; s_snap_day_bdis = cur_bdis;
                }

                s_acc_pv = s_acc_grid = s_acc_batt = s_acc_load = 0;
                s_acc_n  = 0;
                s_cur_hour = new_hour;

                // Actualizar DailyRecord parcial al cambio de hora (requiere daily data)
                if (local_d.valid) {
                    struct tm today_tm = now_tm;
                    today_tm.tm_hour = 0; today_tm.tm_min = 0;
                    today_tm.tm_sec  = 0; today_tm.tm_isdst = -1;
                    uint32_t dep = (uint32_t)mktime(&today_tm);
                    DailyRecord dr{};
                    dr.day_epoch   = dep;
                    dr.pv_10wh     = (uint16_t)(local_d.pv_kwh            * 10.0f + 0.5f);
                    dr.export_10wh = (uint16_t)(local_d.export_kwh         * 10.0f + 0.5f);
                    dr.import_10wh = (uint16_t)(local_d.import_kwh         * 10.0f + 0.5f);
                    dr.load_10wh   = (uint16_t)(local_d.load_kwh           * 10.0f + 0.5f);
                    dr.bchg_10wh   = (uint16_t)(local_d.batt_charge_kwh    * 10.0f + 0.5f);
                    dr.bdis_10wh   = (uint16_t)(local_d.batt_discharge_kwh * 10.0f + 0.5f);
                    dr.soc_start   = s_soc_start_of_day;
                    dr.soc_end     = (uint8_t)local_e.batt_soc;
                    dr.flags       = 0x01;
                    if (s_flash_display_mutex) xSemaphoreTake(s_flash_display_mutex, portMAX_DELAY);
                    Cache.pushDaily(dr);
                    if (s_flash_display_mutex) xSemaphoreGive(s_flash_display_mutex);
                }
            }

            // ── Cambio de día ─────────────────────────────────────────────
            if (new_day != s_cur_day) {
                struct tm yesterday = now_tm;
                yesterday.tm_mday--;  // mktime() normaliza mday=0 al último día del mes anterior
                yesterday.tm_hour = 0; yesterday.tm_min = 0;
                yesterday.tm_sec  = 0; yesterday.tm_isdst = -1;
                uint32_t dep_yesterday = (uint32_t)mktime(&yesterday);

                HourlyRecord last_hr{};
                if (s_flash_display_mutex) xSemaphoreTake(s_flash_display_mutex, portMAX_DELAY);
                bool got_hr = Store.getLastHourly(dep_yesterday, last_hr);
                if (s_flash_display_mutex) xSemaphoreGive(s_flash_display_mutex);
                if (got_hr) {
                    DailyRecord dr{};
                    dr.day_epoch   = dep_yesterday;
                    dr.pv_10wh     = last_hr.day_pv;
                    dr.export_10wh = last_hr.day_export;
                    dr.import_10wh = last_hr.day_import;
                    dr.load_10wh   = last_hr.day_load;
                    dr.bchg_10wh   = last_hr.day_bchg;
                    dr.bdis_10wh   = last_hr.day_bdis;
                    dr.soc_start   = s_soc_start_of_day;
                    dr.soc_end     = last_hr.soc_end;
                    dr.flags       = 0x03;
                    if (s_flash_display_mutex) xSemaphoreTake(s_flash_display_mutex, portMAX_DELAY);
                    Cache.pushDaily(dr);
                    if (s_flash_display_mutex) xSemaphoreGive(s_flash_display_mutex);
                }

                s_soc_start_of_day = (uint8_t)local_e.batt_soc;
                s_cur_day = new_day;
                // Resetear estado solar al cambiar de día — de noche no hay producción
                s_solar_active   = false;
                s_solar_debounce = 0;
            }
        }

        end_record:;

        // ── Alertas Telegram ──────────────────────────────────────────────
        if (s_startup_done && local_e.valid && Telegram.isConfigured()) {
            // Refrescar config de Telegram desde NVS cada 60 s
            if (!s_tgcfg_loaded || millis() - s_tgcfg_last_ms >= 60000) {
                s_tgcfg = Storage.loadTelegramConfig();
                s_tgcfg_loaded  = true;
                s_tgcfg_last_ms = millis();
            }

            // ── Producción solar ──────────────────────────────────────────
            // Umbral de inicio: 50 W / parada: 20 W — debounce 3 lecturas (15 s)
            if (s_tgcfg.notify_solar) {
                if (!s_solar_active && local_e.pv_power > 50.0f) {
                    if (++s_solar_debounce >= 3) {
                        s_solar_active   = true;
                        s_solar_debounce = 0;
                        Telegram.enqueueAlert(AlertType::SOLAR_START,
                                              (int32_t)local_e.pv_power);
                        screen_banner_enqueue(AlertType::SOLAR_START,
                                              (int32_t)local_e.pv_power);
                        Storage.saveSessionState({now_ep, true, true});
                    }
                } else if (s_solar_active && local_e.pv_power < 20.0f) {
                    if (++s_solar_debounce >= 3) {
                        s_solar_active   = false;
                        s_solar_debounce = 0;
                        Telegram.enqueueAlert(AlertType::SOLAR_STOP);
                        screen_banner_enqueue(AlertType::SOLAR_STOP);
                        Storage.saveSessionState({now_ep, true, false});
                    }
                } else {
                    s_solar_debounce = 0;
                }
            }

            // ── Batería ───────────────────────────────────────────────────
            // Dos niveles: aviso (batt_warn) y crítico (batt_threshold)
            // Recuperación con histéresis +5% sobre el umbral de aviso
            {
                const uint8_t warn = s_tgcfg.batt_warn;
                const uint8_t crit = s_tgcfg.batt_threshold;
                const uint8_t soc  = (uint8_t)local_e.batt_soc;
                const uint8_t rec  = (uint8_t)min((int)warn + 5, 100);
                switch (s_batt_state) {
                case BattState::NORMAL:
                    if (crit > 0 && soc < crit) {
                        s_batt_state = BattState::CRIT;
                        Telegram.enqueueAlert(AlertType::BATT_CRITICAL, (int32_t)soc);
                        screen_banner_enqueue(AlertType::BATT_CRITICAL, (int32_t)soc);
                    } else if (warn > 0 && soc < warn) {
                        s_batt_state = BattState::WARN;
                        Telegram.enqueueAlert(AlertType::BATT_LOW, (int32_t)soc);
                        screen_banner_enqueue(AlertType::BATT_LOW, (int32_t)soc);
                    }
                    break;
                case BattState::WARN:
                    if (crit > 0 && soc < crit) {
                        s_batt_state = BattState::CRIT;
                        Telegram.enqueueAlert(AlertType::BATT_CRITICAL, (int32_t)soc);
                        screen_banner_enqueue(AlertType::BATT_CRITICAL, (int32_t)soc);
                    } else if (soc >= rec) {
                        s_batt_state = BattState::NORMAL;
                        Telegram.enqueueAlert(AlertType::BATT_RECOVERED, (int32_t)soc);
                        screen_banner_enqueue(AlertType::BATT_RECOVERED, (int32_t)soc);
                    }
                    break;
                case BattState::CRIT:
                    if (soc >= rec) {
                        s_batt_state = BattState::NORMAL;
                        Telegram.enqueueAlert(AlertType::BATT_RECOVERED, (int32_t)soc);
                        screen_banner_enqueue(AlertType::BATT_RECOVERED, (int32_t)soc);
                    } else if (warn > 0 && soc >= (uint8_t)min((int)crit + 5, 100)) {
                        s_batt_state = BattState::WARN;
                        // transición silenciosa de crítico a aviso
                    }
                    break;
                }
            }

            // ── Red eléctrica ─────────────────────────────────────────────
            // Corte: |grid_power| = 0 W durante 3 lecturas (15 s)
            // Solo se evalúa tras haber visto red activa al menos una vez
            if (s_tgcfg.notify_grid) {
                const bool grid_active = fabsf(local_e.grid_power) != 0.0f;
                if (grid_active) s_had_grid = true;

                if (s_had_grid && !s_grid_outage && !grid_active) {
                    if (++s_grid_debounce >= 3) {
                        s_grid_outage   = true;
                        s_grid_debounce = 0;
                        Telegram.enqueueAlert(AlertType::GRID_OUTAGE,
                                              (int32_t)local_e.grid_power);
                        screen_banner_enqueue(AlertType::GRID_OUTAGE);
                    }
                } else if (s_grid_outage && grid_active) {
                    s_grid_outage   = false;
                    s_grid_debounce = 0;
                    Telegram.enqueueAlert(AlertType::GRID_RESTORED);
                    screen_banner_enqueue(AlertType::GRID_RESTORED);
                } else if (grid_active) {
                    s_grid_debounce = 0;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));   // ← dentro del bucle
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
    DBGSERIAL.begin(DBG_UART, 115200);
    g_uptime_start = millis();

    // ── Tu código de display/touch aquí (ya configurado) ─────────────────
    touch_init();

    // Init Display
    gfx->begin();

    Backlight.begin(TFT2_BL);

    lv_init();

    screenWidth = gfx->width();
    screenHeight = gfx->height();
    // ESP32-P4 (Guition JC1060P470): buffer completo en PSRAM. LVGL renderiza aquí y
    // esp_lcd_panel_draw_bitmap lo copia al buffer trasero DSI via DMA2D + swap en vsync.
    // ESP32-S3 (esp32s3box): buffer parcial en SRAM para DMA.
#if defined(BOARD_GUITION_JC1060P470)
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(screenWidth * screenHeight * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(2 * screenWidth * screenHeight, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    if (!disp_draw_buf)
    {
        DBGSERIAL.println("LVGL disp_draw_buf allocate failed!");
        while (true)
            delay(1);
    }

    /* Initialize the display */
    disp_drv = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp_drv, my_disp_flush);
#if defined(BOARD_GUITION_JC1060P470)
    // FULL mode: LVGL acumula el frame completo en disp_draw_buf.
    // flush_cb llama a esp_lcd_panel_draw_bitmap → DMA2D copia al buffer trasero DSI
    // y el swap se hace en el siguiente vsync → sin tearing.
    lv_display_set_buffers(disp_drv, disp_draw_buf, NULL,
                           screenWidth * screenHeight * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_FULL);
#else
    disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(2 * screenWidth * screenHeight, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!disp_draw_buf2)
    {
        DBGSERIAL.println("LVGL disp_draw_buf2 allocate failed!");
        while (true)
            delay(1);
    }
    lv_display_set_buffers(disp_drv, disp_draw_buf, disp_draw_buf2, 2 * screenWidth * screenHeight, LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif

    /* Initialize the (dummy) input device driver */
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);
    lv_tick_set_cb(esp_tick_get_cb);

    // ─────────────────────────────────────────────────────────────────────
    
    // ── Splash screen ─────────────────────────────────────────────────────
    splash_init();

        // ── LittleFS ──────────────────────────────────────────────────────────
    splash_update(SplashStep::LITTLEFS, SplashState::RUNNING);
    if (!LittleFS.begin(false, "/littlefs", 10, "spiffs")) {
        // Primer arranque o partición corrupta → formatear en Core 0 para que
        // LVGL siga corriendo en Core 1 y la pantalla no parpadee.
        splash_update(SplashStep::LITTLEFS, SplashState::WARN, "Formateando flash...");
        static volatile bool s_fmt_done = false;
        xTaskCreatePinnedToCore([](void*) {
            LittleFS.begin(true, "/littlefs", 10, "spiffs");
            s_fmt_done = true;
            vTaskDelete(nullptr);
        }, "lfs_fmt", 4096, nullptr, 1, nullptr, 0);
        while (!s_fmt_done) {
            lv_timer_handler();
            esp_task_wdt_reset();
            delay(10);
        }
    }
    if (!LittleFS.begin(false, "/littlefs", 10, "spiffs")) {
        splash_update(SplashStep::LITTLEFS, SplashState::ERROR, "Error montando LittleFS");
        delay(3000);
    } else {
        char detail[48];
        snprintf(detail, sizeof(detail), "%u KB libres",
                 (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024);
        splash_update(SplashStep::LITTLEFS, SplashState::OK, detail);
    }

    // ── Zona horaria — configurar ANTES de Store.begin() ──────────────────
    // _day_idx_load() usa localtime_r sobre los timestamps de flash para calcular
    // los epochs de medianoche. Si la TZ no está configurada aquí, usa UTC y los
    // epochs quedan desfasados respecto al resto del código (que sí usa la TZ).
    configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
    setenv("TZ", TIMEZONE, 1);
    tzset();

    // ── DataStore ─────────────────────────────────────────────────────────
    splash_update(SplashStep::DATASTORE, SplashState::RUNNING);
    if (!Store.begin()) {
        splash_update(SplashStep::DATASTORE, SplashState::ERROR, "Error en DataStore");
        delay(2000);
    } else {
        char detail[64];
        snprintf(detail, sizeof(detail), "Raw:%lu Hrly:%lu Day:%lu",
                 (unsigned long)Store.getRawCount(),
                 (unsigned long)Store.getHourlyCount(),
                 (unsigned long)Store.getDailyCount());
        splash_update(SplashStep::DATASTORE, SplashState::OK, detail);
    }

    // ── PSRAM Cache ───────────────────────────────────────────────────────
    splash_update(SplashStep::PSRAM_CACHE, SplashState::RUNNING);
    print_mem_stats("antes de cache");
    if (!Cache.begin()) {
        splash_update(SplashStep::PSRAM_CACHE, SplashState::ERROR, "Sin PSRAM");
        delay(2000);
    } else {
        char detail[48];
        snprintf(detail, sizeof(detail), "PSRAM libre: %lu KB",
                 (unsigned long)ESP.getFreePsram() / 1024);
        splash_update(SplashStep::PSRAM_CACHE, SplashState::OK, detail);
    }

    // ── Cargar config desde NVS ───────────────────────────────────────────
    Storage.loadConfig(g_cfg);

    // ── WiFi — inicio NO bloqueante ───────────────────────────────────────
    splash_update(SplashStep::WIFI_CONNECTING, SplashState::RUNNING,
                  g_cfg.wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_cfg.wifi_ssid, g_cfg.wifi_pass);
    // NO esperamos aquí — la tarea Solarman gestiona la conexión

    // ── NTP — la TZ ya está configurada arriba; el sync ocurre al conectar WiFi ─
    splash_update(SplashStep::NTP, SplashState::RUNNING);
    splash_update(SplashStep::NTP, SplashState::OK, "Pendiente de WiFi");

    // ── Construir pantalla principal (tileview) ───────────────────────────
    g_main_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_main_screen, lv_color_hex(0x0D1117), 0);
    lv_obj_set_style_bg_opa(g_main_screen, LV_OPA_COVER, 0);

    lv_obj_t* tv = lv_tileview_create(g_main_screen);
    lv_obj_set_size(tv, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x0D1117), 0);

    // Dashboard(0) → Chart(1) → Stats(2) → Perfil energía(3) → Config(4)
    g_tile_dash    = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    g_tile_chart   = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    g_tile_stats   = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
    g_tile_energy  = lv_tileview_add_tile(tv, 3, 0, LV_DIR_HOR);
    g_tile_config  = lv_tileview_add_tile(tv, 4, 0, LV_DIR_LEFT);

    dashboard_init(g_tile_dash);
    chart_screen_init(g_tile_chart);
    stats_screen_init(g_tile_stats);
    energy_profile_init(g_tile_energy);
    config_screen_init(g_tile_config);

    pagination_dots_init(g_main_screen, 5);

    lv_obj_add_event_cb(tv, [](lv_event_t* e) {
        lv_obj_t* tile = lv_tileview_get_tile_active(lv_event_get_target_obj(e));

        int idx = 0;
        lv_obj_t* tiles[] = {g_tile_dash, g_tile_chart,
                             g_tile_stats, g_tile_energy, g_tile_config};
        for (int i = 0; i < 5; i++) {
            if (tiles[i] == tile) { idx = i; break; }
        }
        pagination_dots_set(idx);

        chart_screen_set_active(tile == g_tile_chart);
        stats_screen_set_active(tile == g_tile_stats);
        energy_profile_set_active(tile == g_tile_energy);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // ── Servidor web ──────────────────────────────────────────────────────
    // Lo arrancamos sin WiFi — empezará a responder cuando haya conexión
    splash_update(SplashStep::WEBSERVER, SplashState::RUNNING);
    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) {
        DBGSERIAL.println("[Setup] ERROR: fallo al crear mutex — reiniciando");
        delay(500);
        ESP.restart();
    }
#if defined(BOARD_GUITION_JC1060P470)
    s_flash_display_mutex = xSemaphoreCreateMutex();
#endif
    webserver_set_data(g_mutex, &g_energy, &g_daily);
    webserver_begin();
    splash_update(SplashStep::WEBSERVER, SplashState::OK);

    // ── Telegram — solo si está configurado ───────────────────────────────
    splash_update(SplashStep::TELEGRAM, SplashState::RUNNING);
    TelegramConfig tgcfg = Storage.loadTelegramConfig();
    if (tgcfg.token[0] != '\0' && tgcfg.chat_id[0] != '\0') {
        Telegram.begin(tgcfg.token, tgcfg.chat_id);
        splash_update(SplashStep::TELEGRAM, SplashState::OK, tgcfg.chat_id);
    } else {
        splash_update(SplashStep::TELEGRAM, SplashState::WARN,
                      "No configurado");
    }

    // ── Tarea Solarman ────────────────────────────────────────────────────
    screen_banner_init();
    xTaskCreatePinnedToCore(solarmanTask, "solarman",
                             8192, nullptr, 1, nullptr, 0);

    // ── Transición al tileview ────────────────────────────────────────────
    // Breve pausa para que el usuario vea el estado final
    delay(800);
    lv_timer_handler();
    splash_finish();

    DBGSERIAL.println("[Setup] Completado");
    print_mem_stats("setup finalizado");
}

// ── Overlay OTA ───────────────────────────────────────────────────────────
// Llamar SOLO desde loop() (Core 1 — LVGL no es thread-safe)
static void show_ota_overlay() {
    // Fondo negro completo sobre cualquier pantalla activa
    lv_obj_t* ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ov, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x0D1117), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 0, 0);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_CLICKABLE);

    // Tarjeta central
    lv_obj_t* card = lv_obj_create(ov);
    lv_obj_set_size(card, SX(300), SY(152));
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x161B22), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x1F6FEB), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, SS(10), 0);
    lv_obj_set_style_pad_all(card, SX(14), 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);

    // Círculo azul con icono de engranaje — sin semántica de progreso
    lv_obj_t* circle = lv_obj_create(card);
    lv_obj_set_size(circle, SS(52), SS(52));
    lv_obj_set_style_bg_color(circle, lv_color_hex(0x1F6FEB), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* icon = lv_label_create(circle);
    lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(icon, &FONT_LARGE, 0);
    lv_obj_center(icon);

    // Título
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, "Actualizando firmware");
    lv_obj_set_style_text_color(title, lv_color_hex(0xEAEAEA), 0);
    lv_obj_set_style_text_font(title, &FONT_NORMAL, 0);
    lv_obj_align_to(title, circle, LV_ALIGN_OUT_BOTTOM_MID, 0, SY(12));

    // Subtítulo
    lv_obj_t* sub = lv_label_create(card);
    lv_label_set_text(sub, "No desconecte el dispositivo");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x4E5A6E), 0);
    lv_obj_set_style_text_font(sub, &FONT_SMALL, 0);
    lv_obj_align_to(sub, title, LV_ALIGN_OUT_BOTTOM_MID, 0, SY(7));
}

// ── Loop (Core 1) ─────────────────────────────────────────────────────────
void loop() {
    // Render congelado durante escritura flash OTA
    if (g_ota_active) { delay(10); return; }

    // Solicitud de overlay OTA: dibujar, renderizar y señalizar listo
    if (g_ota_request) {
        show_ota_overlay();
        lv_timer_handler();
        lv_timer_handler();
        g_ota_active  = true;
        g_ota_request = false;
        return;
    }

    lv_timer_handler();

    // ── Detectar toque: inactividad + doble-tap para volver al dashboard ──
    static bool     s_last_touch  = false;
    static uint32_t s_last_tap_ms = 0;

    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            bool pressed = (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
            if (pressed) Backlight.onTouch();
            if (!pressed && s_last_touch) {   // flanco de bajada = tap completo
                uint32_t now = millis();
                if (s_last_tap_ms && now - s_last_tap_ms < 400) {
                    lv_obj_t* tv = lv_obj_get_child(g_main_screen, 0);
                    if (tv) {
                        lv_obj_t* cur = lv_tileview_get_tile_active(tv);
                        if (cur && cur != g_tile_dash)
                            lv_tileview_set_tile(tv, g_tile_dash, LV_ANIM_ON);
                    }
                    s_last_tap_ms = 0;
                } else {
                    s_last_tap_ms = now;
                }
            }
            s_last_touch = pressed;
            break;
        }
        indev = lv_indev_get_next(indev);
    }
    screen_banner_tick();
    Backlight.tick();

    // Resto del loop igual
    if (xSemaphoreTake(g_mutex, 0) == pdTRUE) {
        if (g_energy_ready) { dashboard_update(g_energy); g_energy_ready = false; }
        if (g_daily_ready)  {
            stats_screen_update(g_daily);
            g_daily_ready = false;
        }
        xSemaphoreGive(g_mutex);
    }

    dashboard_tick();
    chart_screen_tick();
    energy_profile_tick();
    config_screen_tick();

    delay(5);
}