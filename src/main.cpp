#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <time.h>

#include "config.h"
#include "storage.h"
#include "solarman.h"
#include "dashboard.h"
#include "stats_screen.h"
#include "config_screen.h"
#include "chart_screen.h"
#include "web_server.h"

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

static EnergyData        g_energy;
static DailyStats        g_daily;
static SemaphoreHandle_t g_mutex;
static volatile bool     g_energy_ready = false;
static volatile bool     g_daily_ready  = false;
static lv_obj_t*         g_tile_chart = nullptr;
static lv_obj_t*         g_tile_stats = nullptr;

// Config en RAM cargada desde NVS al arrancar
static AppConfig g_cfg;

// Función delta con protección de rollover de medianoche
static int16_t delta_wh(float cur, float prev) {
    float d = cur - prev;
    if (d < 0.0f) d = cur;           // reset de medianoche
    int v = (int)(d * 1000.0f + 0.5f);
    return (int16_t)(v > 32000 ? 32000 : v);
}

// ── Tarea de red (Core 0) ─────────────────────────────────────────────────
static void solarmanTask(void* /*pv*/) {
    // Usamos g_cfg que ya fue rellenado en setup() desde NVS
    SolarmanClient client(g_cfg.logger_ip, LOGGER_PORT,
                          g_cfg.logger_serial, MODBUS_UNIT_ID);
    EnergyData  local_e;
    DailyStats  local_d;
    uint32_t    last_daily = millis() - POLL_DAILY_MS + POLL_INTERVAL_MS;

    static DailyStats s_hour_snap;
    static int        s_last_hour = -1;
    static DailyStats s_prev_daily;   // snapshot del poll anterior
    static int        s_last_day = -1;

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial0.println("[WiFi] Reconectando...");
            WiFi.begin(g_cfg.wifi_ssid, g_cfg.wifi_pass);
            uint32_t t = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t < 15000)
                vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (WiFi.status() == WL_CONNECTED) {
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

            if (millis() - last_daily >= POLL_DAILY_MS) {
                if (client.fetchDailyStats(local_d)) {
                    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        g_daily       = local_d;
                        g_daily_ready = true;
                        xSemaphoreGive(g_mutex);
                    }
                }
                // Muestreo horario
                struct tm tt;
                if (local_d.valid && getLocalTime(&tt, 500)) {
                    int cur_day = tt.tm_mday;

                    if (s_last_day == -1) {
                        // Primera lectura del dispositivo
                        s_last_day   = cur_day;
                        s_prev_daily = local_d;
                    } else if (cur_day != s_last_day) {
                        // El día cambió: s_prev_daily tiene los últimos valores de ayer
                        if (s_prev_daily.valid) {
                            // Calcular medianoche de ayer
                            struct tm yday = tt;
                            yday.tm_mday -= 1;
                            yday.tm_hour = 0; yday.tm_min = 0; yday.tm_sec = 0; yday.tm_isdst = -1;

                            DailyRecord rec{};
                            rec.timestamp          = (uint32_t)mktime(&yday);
                            rec.pv_kwh             = s_prev_daily.pv_kwh;
                            rec.export_kwh         = s_prev_daily.export_kwh;
                            rec.import_kwh         = s_prev_daily.import_kwh;
                            rec.load_kwh           = s_prev_daily.load_kwh;
                            rec.batt_charge_kwh    = s_prev_daily.batt_charge_kwh;
                            rec.batt_discharge_kwh = s_prev_daily.batt_discharge_kwh;

                            Storage.pushDailyRecord(rec);
                            Serial0.printf("[Daily] Dia guardado: PV:%.2f Exp:%.2f Imp:%.2f "
                                        "Load:%.2f BatC:%.2f BatD:%.2f kWh\n",
                                        rec.pv_kwh, rec.export_kwh, rec.import_kwh,
                                        rec.load_kwh, rec.batt_charge_kwh, rec.batt_discharge_kwh);
                        }
                        s_last_day = cur_day;
                    }
                    s_prev_daily = local_d;   // actualizar siempre
                }
                if (getLocalTime(&tt, 500)) {
                    int cur = tt.tm_hour;
                    if (s_last_hour == -1) {
                        // Primera lectura: tomar snapshot
                        s_hour_snap = local_d;
                        s_last_hour = cur;
                    } else if (cur != s_last_hour && local_d.valid) {
                        // La hora ha cambiado: construir y guardar registro
                        HourlyRecord rec{};
                        rec.pv_wh             = delta_wh(local_d.pv_kwh,             s_hour_snap.pv_kwh);
                        rec.export_wh         = delta_wh(local_d.export_kwh,         s_hour_snap.export_kwh);
                        rec.import_wh         = delta_wh(local_d.import_kwh,         s_hour_snap.import_kwh);
                        rec.batt_charge_wh    = delta_wh(local_d.batt_charge_kwh,    s_hour_snap.batt_charge_kwh);
                        rec.batt_discharge_wh = delta_wh(local_d.batt_discharge_kwh, s_hour_snap.batt_discharge_kwh);
                        rec.load_wh           = delta_wh(local_d.load_kwh,           s_hour_snap.load_kwh);
                        rec.soc               = (uint8_t)local_e.batt_soc;
                        rec.valid             = 1;

                        // Medianoche local del día en curso (que es s_last_hour, no cur)
                        struct tm mid = tt;
                        mid.tm_hour = 0; mid.tm_min = 0; mid.tm_sec = 0; mid.tm_isdst = -1;
                        // Si la hora que termina es 23 y ahora es 0, ya estamos en otro día:
                        // mktime con el tm actual y hora 0 ya da la medianoche correcta
                        uint32_t day_ep = (uint32_t)mktime(&mid);
                        if (s_last_hour == 23 && cur == 0)
                            day_ep -= 86400;   // la hora 23 pertenece al día anterior

                        Storage.saveHourlyRecord(day_ep, (uint8_t)s_last_hour, rec);
                        Serial0.printf("[Hourly] %02d:00 PV:%dWh Grid:%+dWh Bat:%+dWh Load:%dWh SOC:%d%%\n",
                            s_last_hour,
                            rec.pv_wh, (int)rec.import_wh - rec.export_wh,
                            (int)rec.batt_charge_wh - rec.batt_discharge_wh,
                            rec.load_wh, rec.soc);

                        s_hour_snap = local_d;
                        s_last_hour = cur;
                    }
                }
                last_daily = millis();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
    Serial0.begin(115200);
    
    // ── Tu código de display/touch aquí (ya configurado) ─────────────────
    touch_init();

    // Init Display
    gfx->begin();

#ifdef TFT2_BL
    pinMode(TFT2_BL, OUTPUT);
    digitalWrite(TFT2_BL, HIGH);
#endif
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

    // NVS → config en RAM
    Storage.loadConfig(g_cfg);

    // WiFi con valores de NVS
    WiFi.begin(g_cfg.wifi_ssid, g_cfg.wifi_pass);
    Serial0.print("[WiFi] Conectando");
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        delay(500); Serial0.print('.');
    }
    if (WiFi.status() == WL_CONNECTED)
        Serial0.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    else
        Serial0.println("\n[WiFi] Sin conexion – se reintentará en la tarea");

    // NTP
    configTime(0, 0, "es.pool.ntp.org", "time.cloudflare.com");
    setenv("TZ", TIMEZONE, 1);
    tzset();
    Serial0.print("[NTP] Sincronizando");
    struct tm ntp_t;
    for (int i = 0; i < 20 && !getLocalTime(&ntp_t, 500); i++) Serial0.print('.');
    Serial0.println(getLocalTime(&ntp_t, 0) ? " OK" : " TIMEOUT");

    // ── Tileview: 4 pantallas horizontales ───────────────────────────────
    lv_obj_t* tv = lv_tileview_create(lv_scr_act());
    lv_obj_set_size(tv, 480, 272);
    lv_obj_set_pos(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x0D1117), 0);

    lv_obj_t* tile_dash   = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    g_tile_stats          = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    g_tile_chart          = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
    lv_obj_t* tile_config = lv_tileview_add_tile(tv, 3, 0, LV_DIR_LEFT);

    dashboard_init(tile_dash);
    stats_screen_init(g_tile_stats);
    chart_screen_init(g_tile_chart);
    config_screen_init(tile_config);

    // Evento de cambio de tile → activa/desactiva refresco del chart
    lv_obj_add_event_cb(tv, [](lv_event_t* e) {
        lv_obj_t* tile = lv_tileview_get_tile_active(lv_event_get_target_obj(e));
        chart_screen_set_active(tile == g_tile_chart);
        stats_screen_set_active(tile == g_tile_stats);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Mutex + tarea de red en Core 0
    g_mutex = xSemaphoreCreateMutex();

    webserver_set_data(g_mutex, &g_energy, &g_daily);
    webserver_begin();   // crea su propia tarea en Core 0

    xTaskCreatePinnedToCore(solarmanTask, "solarman",
                             8192, nullptr, 1, nullptr, 0);
}

// ── Loop (Core 1) ─────────────────────────────────────────────────────────
void loop() {
    lv_timer_handler();

    if (xSemaphoreTake(g_mutex, 0) == pdTRUE) {
        if (g_energy_ready) { dashboard_update(g_energy);    g_energy_ready = false; }
        if (g_daily_ready)  { stats_screen_update(g_daily);  g_daily_ready  = false; }
        xSemaphoreGive(g_mutex);
    }

    dashboard_tick();
    chart_screen_tick();
    config_screen_tick();   // refresca IP/RSSI cada 5 s, coste mínimo

    delay(5);
}