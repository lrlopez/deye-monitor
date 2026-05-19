#include <Arduino.h>
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
#include "splash_screen.h"
#include "web_server.h"
#include "telegram.h"
#include "psram_alloc.h"
#include "psram_cache.h"
#include "backlight.h"
#include "pagination_dots.h"


/* Change to your screen resolution */
static uint32_t screenWidth = 480;
static uint32_t screenHeight = 272;

#include <Arduino_GFX_Library.h>

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

/*******************************************************************************
 * End of Arduino_GFX setting
 ******************************************************************************/

#include "touch.h"

static lv_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf;
static lv_display_t *disp_drv;
static unsigned long last_ms;

/* Display flushing */
void my_disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*) px_map, w, h);

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
            //Serial0.printf("%d, %d\n", data->point.x, data->point.y);
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
static lv_obj_t*         g_tile_stats   = nullptr;
static lv_obj_t*         g_tile_chart   = nullptr;
static lv_obj_t*         g_tile_config  = nullptr;

// Config en RAM cargada desde NVS al arrancar
static AppConfig g_cfg;
// Flag: WiFi conectado (escrito por solarmanTask, leído por loop)
static volatile bool g_wifi_connected = false;

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

    static uint8_t  s_soc_start_of_day = 0;

    for (;;) {
        // ── Reconexión WiFi ───────────────────────────────────────────────
        if (WiFi.status() != WL_CONNECTED) {
            g_wifi_connected = false;
            static uint32_t last_reconnect = 0;
            if (millis() - last_reconnect > 15000) {
                Serial0.println("[WiFi] Reconectando...");
                WiFi.disconnect();
                WiFi.begin(g_cfg.wifi_ssid, g_cfg.wifi_pass);
                last_reconnect = millis();
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // ── WiFi recién conectado ─────────────────────────────────────────
        if (!g_wifi_connected) {
            g_wifi_connected = true;
            Serial0.printf("[WiFi] Conectado: %s\n",
                          WiFi.localIP().toString().c_str());
            uint32_t ntp_wait = millis();
            while (time(nullptr) < 1700000000UL &&
                   millis() - ntp_wait < 10000)
                vTaskDelay(pdMS_TO_TICKS(500));
            if (time(nullptr) > 1700000000UL) {
                Serial0.println("[NTP] Sincronizado");
                // Corregir los slots del cache que se inicializaron en begin()
                // con el reloj en 1970 (antes de tener WiFi/NTP).
                Cache.reinitAfterNtp();
            } else {
                Serial0.println("[NTP] Timeout");
            }
            if (MDNS.begin(g_cfg.mdns_hostname)) {
                MDNS.addService("http", "tcp", 80);
                Serial0.printf("[mDNS] Activo: %s.local\n", g_cfg.mdns_hostname);
            } else {
                Serial0.println("[mDNS] Error al iniciar");
            }
        }

        // ── 1. Datos instantáneos ─────────────────────────────────────────
        if (client.fetchEnergyData(local_e)) {
            Serial0.printf("[Live] PV:%dW Grid:%dW Bat:%dW(%d%%) Load:%dW\n",
                (int)local_e.pv_power, (int)local_e.grid_power,
                (int)local_e.batt_power, (int)local_e.batt_soc,
                (int)local_e.load_power);
            if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_energy       = local_e;
                g_energy_ready = true;
                xSemaphoreGive(g_mutex);
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
            }
        }

        // ── 3. Grabación ──────────────────────────────────────────────────
        uint32_t now_ep    = (uint32_t)time(nullptr);
        uint32_t slot_5min = now_ep / 300;

        if (now_ep < 1700000000UL) goto end_record;   // NTP no listo

        // Startup: inicializar estado desde la primera lectura válida
        if (!s_startup_done) {
            if (!local_e.valid || !local_d.valid) goto end_record;
            s_startup_done  = true;
            s_cur_5min_slot = (int32_t)slot_5min;

            struct tm now_tm; getLocalTime(&now_tm, 500);
            s_cur_hour = now_tm.tm_hour;
            s_cur_day  = now_tm.tm_mday;

            s_snap_day_pv   = (uint16_t)(local_d.pv_kwh             * 10.0f + 0.5f);
            s_snap_day_exp  = (uint16_t)(local_d.export_kwh          * 10.0f + 0.5f);
            s_snap_day_imp  = (uint16_t)(local_d.import_kwh          * 10.0f + 0.5f);
            s_snap_day_load = (uint16_t)(local_d.load_kwh            * 10.0f + 0.5f);
            s_snap_day_bchg = (uint16_t)(local_d.batt_charge_kwh     * 10.0f + 0.5f);
            s_snap_day_bdis = (uint16_t)(local_d.batt_discharge_kwh  * 10.0f + 0.5f);
            s_snap_valid    = true;
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
                Cache.pushDaily(dr);   // disponible para el web server inmediatamente
            }

            Serial0.printf("[Record] Startup ok: slot=%lu h=%d d=%d\n",
                (unsigned long)slot_5min, s_cur_hour, s_cur_day);
            goto end_record;
        }

        if (!local_e.valid || !local_d.valid) goto end_record;

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
                Serial0.printf("[Record] Slot: ts=%lu\n", (unsigned long)record_ts);

                Record5Min r{};
                r.timestamp  = record_ts;
                r.pv_w       = (int16_t)constrain(local_e.pv_power,  -32767, 32767);
                r.grid_w     = (int16_t)constrain(local_e.grid_power, -32767, 32767);
                r.batt_w     = (int16_t)constrain(local_e.batt_power, -32767, 32767);
                r.load_w     = (int16_t)constrain(local_e.load_power, -32767, 32767);
                r.soc        = (uint8_t)local_e.batt_soc;
                r.flags      = 0x01;

                if (Store.push(r))
                    Cache.pushRaw(r);

                Storage.saveSessionState({record_ts, true});
            }

            // ── Cambio de hora ────────────────────────────────────────────
            if (new_hour != s_cur_hour) {
                if (s_acc_n > 0 && s_snap_valid) {
                    struct tm mid = now_tm;
                    if (new_hour == 0) mid.tm_mday--;
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
                    Cache.pushHourly(hr);

                    s_snap_day_pv   = cur_pv;   s_snap_day_exp  = cur_exp;
                    s_snap_day_imp  = cur_imp;  s_snap_day_load = cur_load;
                    s_snap_day_bchg = cur_bchg; s_snap_day_bdis = cur_bdis;
                }

                s_acc_pv = s_acc_grid = s_acc_batt = s_acc_load = 0;
                s_acc_n  = 0;
                s_cur_hour = new_hour;

                // Actualizar DailyRecord parcial al cambio de hora
                {
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
                    Cache.pushDaily(dr);
                }
            }

            // ── Cambio de día ─────────────────────────────────────────────
            if (new_day != s_cur_day) {
                struct tm yesterday = now_tm;
                yesterday.tm_mday--;
                yesterday.tm_hour = 0; yesterday.tm_min = 0;
                yesterday.tm_sec  = 0; yesterday.tm_isdst = -1;
                uint32_t dep_yesterday = (uint32_t)mktime(&yesterday);

                HourlyRecord last_hr{};
                if (Store.getLastHourly(dep_yesterday, last_hr)) {
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
                    Cache.pushDaily(dr);
                }

                s_soc_start_of_day = (uint8_t)local_e.batt_soc;
                s_cur_day = new_day;
            }
        }

        end_record:;

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));   // ← dentro del bucle
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
    Serial0.begin(115200);
    g_uptime_start = millis();

    // ── Tu código de display/touch aquí (ya configurado) ─────────────────
    touch_init();

    // Init Display
    gfx->begin();

    Backlight.begin(TFT2_BL);

    lv_init();

    screenWidth = gfx->width();
    screenHeight = gfx->height();
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(2 * screenWidth * screenHeight / 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!disp_draw_buf)
    {
        Serial0.println("LVGL disp_draw_buf allocate failed!");
        while (true)
            delay(1);
    }

    /* Initialize the display */
    disp_drv = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp_drv, my_disp_flush);
    /* Change the following line to your display resolution */
    lv_display_set_buffers(disp_drv, disp_draw_buf, NULL, 2 * screenWidth * screenHeight / 4, LV_DISPLAY_RENDER_MODE_PARTIAL);

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
    if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
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

    g_tile_dash    = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    g_tile_stats   = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    g_tile_chart   = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
    g_tile_config  = lv_tileview_add_tile(tv, 3, 0, LV_DIR_LEFT);

    dashboard_init(g_tile_dash);
    stats_screen_init(g_tile_stats);
    chart_screen_init(g_tile_chart);
    config_screen_init(g_tile_config);
    
    pagination_dots_init(g_main_screen, 4);

    lv_obj_add_event_cb(tv, [](lv_event_t* e) {
        lv_obj_t* tile = lv_tileview_get_tile_active(lv_event_get_target_obj(e));
       
        // Alternativa si lv_tileview_get_tile_act no compila:
        int idx = 0;
        lv_obj_t* tiles[] = {g_tile_dash, g_tile_stats,
                             g_tile_chart, g_tile_config};
        for (int i = 0; i < 4; i++) {
            if (tiles[i] == tile) { idx = i; break; }
        }
        pagination_dots_set(idx);

        chart_screen_set_active(tile == g_tile_chart);
        stats_screen_set_active(tile == g_tile_stats);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // ── Servidor web ──────────────────────────────────────────────────────
    // Lo arrancamos sin WiFi — empezará a responder cuando haya conexión
    splash_update(SplashStep::WEBSERVER, SplashState::RUNNING);
    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) {
        Serial0.println("[Setup] ERROR: fallo al crear mutex — reiniciando");
        delay(500);
        ESP.restart();
    }
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
    xTaskCreatePinnedToCore(solarmanTask, "solarman",
                             8192, nullptr, 1, nullptr, 0);

    // ── Transición al tileview ────────────────────────────────────────────
    // Breve pausa para que el usuario vea el estado final
    delay(800);
    lv_timer_handler();
    splash_finish();

    Serial0.println("[Setup] Completado");
    print_mem_stats("setup finalizado");
}

// ── Loop (Core 1) ─────────────────────────────────────────────────────────
void loop() {
    lv_timer_handler();

    // ── Detectar toque para resetear inactividad ──────────────────────────
    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev) {
        if (lv_indev_get_type(indev)  == LV_INDEV_TYPE_POINTER &&
            lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
            Backlight.onTouch();
            break;
        }
        indev = lv_indev_get_next(indev);
    }
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
    config_screen_tick();

    delay(5);
}