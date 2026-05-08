#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include "config.h"
#include "solarman.h"
#include "dashboard.h"

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

// ── Datos compartidos entre tareas ────────────────────────────────────────
static EnergyData       g_data;
static SemaphoreHandle_t g_mutex;
static bool              g_dataReady = false;

// ── Tarea de red (Core 0) ─────────────────────────────────────────────────
// Aislada del loop LVGL para no bloquear la pantalla durante el TCP
static void solarmanTask(void* /*pv*/) {
    SolarmanClient client(LOGGER_IP, LOGGER_PORT, LOGGER_SERIAL, MODBUS_UNIT_ID);
    EnergyData local;

    for (;;) {
        // Reconexión WiFi si es necesario
        if (WiFi.status() != WL_CONNECTED) {
            Serial0.println("[WiFi] Reconectando...");
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            uint32_t t = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t < 15000)
                vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (WiFi.status() == WL_CONNECTED) {
            if (client.fetchEnergyData(local)) {
                Serial0.printf("[Data] PV:%dW  Grid:%dW  Batt:%dW(%d%%)  Load:%dW\n",
                    (int)local.pv_power, (int)local.grid_power,
                    (int)local.batt_power, (int)local.batt_soc,
                    (int)local.load_power);

                if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    g_data      = local;
                    g_dataReady = true;
                    xSemaphoreGive(g_mutex);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
    Serial0.begin(115200);
    Serial0.println("Inicializando");

    // ── Tu código de display aquí (ya configurado) ────────────────────────
    // lv_init();
    // setup_display();   // driver de tu pantalla
    // lv_tick_set_cb([]() -> uint32_t { return millis(); });
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
    }
    else
    {
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

        // WiFi
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        Serial0.print("[WiFi] Conectando");
        while (WiFi.status() != WL_CONNECTED) { delay(500); Serial0.print('.'); }
        Serial0.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());

        // Dashboard LVGL
        dashboard_init();

        // Mutex + tarea de red en Core 0
        g_mutex = xSemaphoreCreateMutex();
        xTaskCreatePinnedToCore(
            solarmanTask, "solarman",
            8192, nullptr, 1,
            nullptr, 0          // Core 0
        );
      }
}

// ── Loop (Core 1 – mismo que LVGL) ───────────────────────────────────────
void loop() {
    lv_timer_handler();

    // Publicar nuevos datos al dashboard si los hay
    if (g_dataReady) {
        if (xSemaphoreTake(g_mutex, 0) == pdTRUE) {
            if (g_dataReady) {
                dashboard_update(g_data);
                g_dataReady = false;
            }
            xSemaphoreGive(g_mutex);
        }
    }

    delay(5);
}