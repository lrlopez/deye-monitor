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
#include "web_alerts.h"
#include "app_state.h"
#include "solarman_task.h"
#include "ui_toast.h"
#include "clock_mode.h"


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
#elif defined(BOARD_GUITION_JC4880P443C)
#define TFT2_BL 23 // Backlight GPIO for Guition JC4880P443C

// Panel: 480x800 nativo (vertical), controlador ST7701 vía MIPI-DSI, 2 lanes.
// Timings verificados por la comunidad (ESPHome mipi_dsi/models/guition.py,
// modelo "JC4880P443") — los del datasheet oficial daban un vsync_front_porch
// 10x menor y colgaban el panel a mitad de la secuencia de init.
Arduino_ESP32DSIPanel *dsipanel = new Arduino_ESP32DSIPanel(
    12 /* hsync_pulse_width */, 42 /* hsync_back_porch */, 42  /* hsync_front_porch */,
    2  /* vsync_pulse_width */, 8  /* vsync_back_porch */, 166 /* vsync_front_porch */,
    34000000 /* prefer_speed */, 500 /* lane_bit_rate Mbps */
);
// rotation=1 gira el panel nativo (480x800) a horizontal (800x480 lógico).
// NOTA: sentido de giro sin verificar en hardware — si la imagen aparece al
// revés o volcada, cambiar a rotation=3 (y ajustar el mapeo táctil de touch.h).
// rst=GPIO5: pin de reset físico del panel (confirmado por ESPHome), antes
// se dejaba sin definir y el panel podía arrancar en estado inconsistente.
Arduino_GFX *gfx = new Arduino_DSI_Display(
    480, 800, dsipanel, 1 /* rotation */, true /* auto_flush */,
    5 /* rst = GPIO5 */,
    st7701_dsi_init_operations, sizeof(st7701_dsi_init_operations) / sizeof(lcd_init_cmd_t));
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
#ifndef LVGL_BUF_LINES
#define LVGL_BUF_LINES 27           // franjas de ~1/10 de pantalla (Sunton, 270 px de alto)
#endif
static size_t disp_buf_bytes = 0;    // tamaño de cada buffer parcial (solo Sunton)
static lv_display_t *disp_drv;
static unsigned long last_ms;

SemaphoreHandle_t g_flash_display_mutex = nullptr;   // declarado en app_state.h; se crea en setup() (solo P4)

/* Display flushing */
void my_disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
#if defined(BOARD_GUITION_JC1060P470)
    // esp_lcd_panel_draw_bitmap usa DMA2D para copiar el frame al back-buffer y
    // programa el swap en el siguiente vsync → sin tearing. También bloquea hasta
    // que DMA2D termina, por lo que px_map es libre al salir.
    // El mutex serializa con las escrituras flash de Core 0.
    // Válido solo con rotation=0: el framebuffer nativo del panel tiene el
    // mismo stride que el buffer de LVGL (panel nativo ya horizontal).
    if (g_flash_display_mutex) xSemaphoreTake(g_flash_display_mutex, portMAX_DELAY);
    esp_lcd_panel_draw_bitmap(dsipanel->getPanelHandle(),
                              area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    if (g_flash_display_mutex) xSemaphoreGive(g_flash_display_mutex);
#elif defined(BOARD_GUITION_JC4880P443C)
    // Panel nativo vertical (480x800) girado 90° por software (rotation=1):
    // el framebuffer nativo del panel tiene stride de 480px, distinto del
    // buffer de LVGL (800px). No se puede copiar en bruto con
    // esp_lcd_panel_draw_bitmap (produce la imagen entrelazada/rota) — hay
    // que pasar por draw16bitRGBBitmap(), que aplica la rotación correcta.
    if (g_flash_display_mutex) xSemaphoreTake(g_flash_display_mutex, portMAX_DELAY);
    {
        uint32_t w = (area->x2 - area->x1 + 1);
        uint32_t h = (area->y2 - area->y1 + 1);
        gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*) px_map, w, h);
    }
    if (g_flash_display_mutex) xSemaphoreGive(g_flash_display_mutex);
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
// DEYE-11: contadores de secuencia en lugar de flags "ready". Se incrementan
// SIEMPRE bajo g_mutex al publicar un dato nuevo; loop() recuerda el último que
// vio y copia el dato bajo el mutex, liberándolo ANTES de tocar LVGL (antes
// se mantenía durante dashboard_update() y podía bloquear/descartar lecturas
// de solarmanTask).
uint32_t                 g_energy_seq   = 0;
uint32_t                 g_daily_seq    = 0;
static lv_obj_t*         g_tileview     = nullptr;
static lv_obj_t*         g_tile_dash    = nullptr;
static lv_obj_t*         g_tile_chart   = nullptr;
static lv_obj_t*         g_tile_stats   = nullptr;
static lv_obj_t*         g_tile_energy  = nullptr;
static lv_obj_t*         g_tile_config  = nullptr;

// Pistas de uso ya mostradas (DEYE-56): máscara de bits persistida en NVS
#define HINT_DOUBLE_TAP  0x01   // "toca dos veces para volver al inicio"
#define HINT_DASH_VIEW   0x02   // "toca la barra superior para cambiar de vista"
static uint8_t s_hints = 0;

// Config en RAM cargada desde NVS al arrancar
AppConfig g_cfg;
// Flag: WiFi conectado (escrito por solarmanTask, leído por loop)
volatile bool g_wifi_connected = false;
// Flags OTA (protocolo Core0→Core1):
//   g_ota_request: webserver pide mostrar overlay
//   g_ota_active:  loop confirma que el overlay está pintado y el render está congelado
//   g_ota_cancel:  la actualización falló: loop retira el overlay y descongela el render
volatile bool g_ota_request = false;
volatile bool g_ota_active  = false;
volatile bool g_ota_cancel  = false;

// Pantalla principal (tileview vive aquí)
lv_obj_t* g_main_screen = nullptr;

// La tarea de red (solarmanTask) vive en solarman_task.cpp; el histórico en
// recorder.cpp y las alertas en alert_engine.cpp (DEYE-41).
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
    // ESP32-P4 (Guition JC1060P470 / JC4880P443C): buffer completo en PSRAM. LVGL renderiza aquí y
    // esp_lcd_panel_draw_bitmap lo copia al buffer trasero DSI via DMA2D + swap en vsync.
    // ESP32-S3 (esp32s3box): DOS buffers parciales de LVGL_BUF_LINES líneas en SRAM
    // interna (DEYE-44), ver abajo.
#if defined(BOARD_GUITION_JC1060P470) || defined(BOARD_GUITION_JC4880P443C)
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(screenWidth * screenHeight * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    // DEYE-44: antes eran dos buffers de PANTALLA COMPLETA en PSRAM (2 × 259 KB). Con
    // el panel RGB, el framebuffer del propio panel ya vive en PSRAM y su DMA la
    // está leyendo continuamente; renderizar LVGL también en PSRAM compite por ese
    // ancho de banda. Ahora LVGL renderiza por franjas (≈1/10 de pantalla, 2 × ~26 KB)
    // en SRAM interna, que además libera ~490 KB de PSRAM. Si no hubiera SRAM
    // suficiente se vuelve al comportamiento anterior (pantalla completa en PSRAM).
    disp_buf_bytes = 2 * screenWidth * LVGL_BUF_LINES;
    disp_draw_buf  = (lv_color_t *)heap_caps_malloc(disp_buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(disp_buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!disp_draw_buf || !disp_draw_buf2) {
        DBGSERIAL.println("[LVGL] Sin SRAM para buffers parciales: usando PSRAM completa");
        if (disp_draw_buf)  heap_caps_free(disp_draw_buf);
        if (disp_draw_buf2) heap_caps_free(disp_draw_buf2);
        disp_buf_bytes = 2 * screenWidth * screenHeight;
        disp_draw_buf  = (lv_color_t *)heap_caps_malloc(disp_buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(disp_buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
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
#if defined(BOARD_GUITION_JC1060P470) || defined(BOARD_GUITION_JC4880P443C)
    // FULL mode: LVGL acumula el frame completo en disp_draw_buf.
    // flush_cb llama a esp_lcd_panel_draw_bitmap → DMA2D copia al buffer trasero DSI
    // y el swap se hace en el siguiente vsync → sin tearing.
    lv_display_set_buffers(disp_drv, disp_draw_buf, NULL,
                           screenWidth * screenHeight * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_FULL);
#else
    if (!disp_draw_buf2)
    {
        DBGSERIAL.println("LVGL disp_draw_buf2 allocate failed!");
        while (true)
            delay(1);
    }
    lv_display_set_buffers(disp_drv, disp_draw_buf, disp_draw_buf2, disp_buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
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

    // DEYE-10: un fallo transitorio de montaje NO debe borrar el histórico.
    //  1) Se reintenta el montaje 3 veces.
    //  2) Si NVS dice que este dispositivo ya montó bien alguna vez (fs_ok) y aun
    //     así falla, no se formatea solo: hay que mantener pulsada la pantalla 2 s
    //     en los 10 s siguientes. Sin confirmación se arranca sin almacenamiento
    //     (los datos siguen en flash por si se recuperan al reiniciar).
    //  3) Si nunca se había montado (primer arranque) se formatea como siempre.
    bool fs_mounted = false;
    for (int attempt = 0; attempt < 3 && !fs_mounted; attempt++) {
        fs_mounted = LittleFS.begin(false, "/littlefs", 10, "spiffs");
        if (!fs_mounted) delay(300);
    }
    if (!fs_mounted) {
        bool do_format = true;
        if (Storage.loadFsKnownGood()) {
            do_format = false;
            splash_update(SplashStep::LITTLEFS, SplashState::WARN,
                          "Flash ilegible. Pulsa 2 s para FORMATEAR");
            uint32_t t_start = millis(), t_press = 0;
            while (millis() - t_start < 10000 && !do_format) {
                lv_timer_handler();
                esp_task_wdt_reset();
                if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
                    if (!t_press) t_press = millis();
                    else if (millis() - t_press >= 2000) do_format = true;
                } else {
                    t_press = 0;
                }
                delay(10);
            }
        }
        if (do_format) {
            // Primer arranque o formateo confirmado → formatear en Core 0 para que
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
            fs_mounted = LittleFS.begin(false, "/littlefs", 10, "spiffs");
        }
    }
    if (!fs_mounted) {
        splash_update(SplashStep::LITTLEFS, SplashState::ERROR, "Sin almacenamiento (LittleFS)");
        delay(3000);
    } else {
        Storage.saveFsKnownGood();   // no escribe si ya estaba marcado
        char detail[48];
        snprintf(detail, sizeof(detail), "%u KB libres",
                 (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024);
        splash_update(SplashStep::LITTLEFS, SplashState::OK, detail);
    }

    // ── Aplicar copia de seguridad de datos pendiente (si la hay) ──────────
    // Los ficheros ".new" los deja /api/data/import (web_server.cpp) tras
    // validar la copia. Se intercambian aquí, ANTES de Store.begin(), porque
    // en este punto ningún fichero de datos está abierto todavía — el
    // intercambio es seguro. Ver DataStore::getBackupPaths().
    if (LittleFS.exists("/data_import_pending")) {
        DBGSERIAL.println("[Setup] Aplicando copia de seguridad de datos pendiente...");
        const char* paths[DataStore::BACKUP_FILE_COUNT];
        Store.getBackupPaths(paths);
        for (uint8_t i = 0; i < DataStore::BACKUP_FILE_COUNT; i++) {
            String tmp = String(paths[i]) + ".new";
            if (LittleFS.exists(tmp)) {
                LittleFS.remove(paths[i]);
                LittleFS.rename(tmp, paths[i]);
                DBGSERIAL.printf("[Setup]   %s restaurado\n", paths[i]);
            }
        }
        LittleFS.remove("/data_import_pending");
        DBGSERIAL.println("[Setup] Copia de seguridad de datos aplicada");
    }

    // ── Borrar todo el historial si hay una solicitud pendiente ────────────
    // La deja /api/data/wipe (web_server.cpp) o la opción de borrado de la
    // pantalla táctil. Debe hacerse ANTES de Store.begin() por el mismo
    // motivo que la restauración de arriba: ningún fichero está abierto aún.
    if (LittleFS.exists("/data_wipe_pending")) {
        DBGSERIAL.println("[Setup] Borrando todo el historial (solicitado)...");
        const char* paths[DataStore::BACKUP_FILE_COUNT];
        Store.getBackupPaths(paths);
        for (uint8_t i = 0; i < DataStore::BACKUP_FILE_COUNT; i++) {
            LittleFS.remove(paths[i]);
        }
        LittleFS.remove("/data_wipe_pending");
        DBGSERIAL.println("[Setup] Historial borrado");
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
    s_hints = Storage.loadHints();
    clock_mode_init();

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
    g_tileview = tv;
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

    // DEYE-56: tocar un punto de paginación salta a esa pantalla
    pagination_dots_set_handler([](int idx) {
        lv_obj_t* tiles[] = {g_tile_dash, g_tile_chart,
                             g_tile_stats, g_tile_energy, g_tile_config};
        if (g_tileview && idx >= 0 && idx < 5)
            lv_tileview_set_tile(g_tileview, tiles[idx], LV_ANIM_ON);
    });

    lv_obj_add_event_cb(tv, [](lv_event_t* e) {
        lv_obj_t* tile = lv_tileview_get_tile_active(lv_event_get_target_obj(e));

        int idx = 0;
        lv_obj_t* tiles[] = {g_tile_dash, g_tile_chart,
                             g_tile_stats, g_tile_energy, g_tile_config};
        for (int i = 0; i < 5; i++) {
            if (tiles[i] == tile) { idx = i; break; }
        }
        pagination_dots_set(idx);

        // DEYE-56: la primera vez que se sale del dashboard, explicar cómo volver
        if (idx != 0 && !(s_hints & HINT_DOUBLE_TAP)) {
            s_hints |= HINT_DOUBLE_TAP;
            Storage.saveHints(s_hints);
            ui_toast_show("Toca dos veces la pantalla para volver al inicio");
        }

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
#if defined(BOARD_GUITION_JC1060P470) || defined(BOARD_GUITION_JC4880P443C)
    g_flash_display_mutex = xSemaphoreCreateMutex();
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
    WebAlerts_init();
    xTaskCreatePinnedToCore(solarmanTask, "solarman",
                             8192, nullptr, 1, nullptr, 0);

    // ── Transición al tileview ────────────────────────────────────────────
    // Breve pausa para que el usuario vea el estado final
    delay(800);
    lv_timer_handler();
    splash_finish();

    // DEYE-12: a partir de aquí loop() debe alimentar el watchdog (feedLoopWDT).
    // Se activa al final de setup() porque el arranque (formateo, carga de
    // índices) puede tardar más que el timeout del watchdog.
    enableLoopWDT();

    DBGSERIAL.println("[Setup] Completado");
    print_mem_stats("setup finalizado");
}

// ── Overlay OTA ───────────────────────────────────────────────────────────
// Llamar SOLO desde loop() (Core 1 — LVGL no es thread-safe)
static lv_obj_t* s_ota_overlay = nullptr;   // para poder retirarlo si la actualización falla

static void show_ota_overlay() {
    // Fondo negro completo sobre cualquier pantalla activa
    lv_obj_t* ov = lv_obj_create(lv_layer_top());
    s_ota_overlay = ov;
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
    lv_obj_set_style_text_color(sub, C_MUTED, 0);
    lv_obj_set_style_text_font(sub, &FONT_SMALL, 0);
    lv_obj_align_to(sub, title, LV_ALIGN_OUT_BOTTOM_MID, 0, SY(7));
}

// ── Loop (Core 1) ─────────────────────────────────────────────────────────
void loop() {
    // DEYE-12: alimentar el watchdog de la tarea loop (suscrita en setup()). Si
    // LVGL o un mutex dejan de responder, el chip se reinicia en vez de quedarse
    // con la pantalla congelada. Va el primero para cubrir también el modo OTA.
    feedLoopWDT();

    // La actualización falló (subida manual o desde GitHub): retirar el overlay y
    // descongelar el render. Antes quedaba la pantalla congelada hasta reiniciar.
    if (g_ota_cancel) {
        g_ota_cancel = false;
        g_ota_request = false;
        if (s_ota_overlay) { lv_obj_delete(s_ota_overlay); s_ota_overlay = nullptr; }
        g_ota_active = false;
    }

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
    // Se mide desde el primer press hasta el segundo press (< 600 ms).
    static bool     s_was_pressed  = false;
    static uint32_t s_first_tap_ms = 0;

    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            bool pressed = (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);
            if (pressed) {
                Backlight.onTouch();
                if (!s_was_pressed) {   // flanco de subida = nuevo press
                    uint32_t now = millis();
                    if (s_first_tap_ms && now - s_first_tap_ms < 600) {
                        // Segundo press dentro de la ventana → doble tap
                        if (g_tileview) {
                            lv_obj_t* cur = lv_tileview_get_tile_active(g_tileview);
                            if (cur && cur != g_tile_dash)
                                lv_tileview_set_tile(g_tileview, g_tile_dash, LV_ANIM_ON);
                        }
                        s_first_tap_ms = 0;
                    } else {
                        s_first_tap_ms = now;
                    }
                }
            }
            s_was_pressed = pressed;
            break;
        }
        indev = lv_indev_get_next(indev);
    }
    screen_banner_tick();
    Backlight.tick();
    // DEYE-54: en reposo (misma inactividad que atenúa el brillo) se muestra el reloj;
    // el toque que lo retira ya llegó a Backlight.onTouch() más arriba en esta vuelta
    clock_mode_tick(Backlight.isIdle());

    // DEYE-11: copiar los datos nuevos bajo el mutex y soltarlo ANTES de tocar
    // LVGL, para no bloquear a solarmanTask mientras se redibuja.
    static uint32_t s_seen_energy_seq = 0, s_seen_daily_seq = 0;
    EnergyData e_copy;
    DailyStats d_copy;
    bool new_energy = false, new_daily = false;
    if (xSemaphoreTake(g_mutex, 0) == pdTRUE) {
        if (g_energy_seq != s_seen_energy_seq) {
            e_copy = g_energy; s_seen_energy_seq = g_energy_seq; new_energy = true;
        }
        if (g_daily_seq != s_seen_daily_seq) {
            d_copy = g_daily; s_seen_daily_seq = g_daily_seq; new_daily = true;
        }
        xSemaphoreGive(g_mutex);
    }
    if (new_energy) { dashboard_update(e_copy); clock_mode_set_energy(e_copy); }
    if (new_daily)  stats_screen_update(d_copy);

    dashboard_tick();

    // DEYE-56: una sola vez, ya en marcha y con el dashboard a la vista, avisar de que
    // la barra superior cambia entre tarjetas y flujo de energía
    static bool s_dash_hint_done = false;
    if (!s_dash_hint_done && millis() - g_uptime_start > 8000) {
        s_dash_hint_done = true;
        if (!(s_hints & HINT_DASH_VIEW) && g_tileview &&
            lv_tileview_get_tile_active(g_tileview) == g_tile_dash) {
            s_hints |= HINT_DASH_VIEW;
            Storage.saveHints(s_hints);
            ui_toast_show("Toca la barra superior para cambiar de vista");
        }
    }

    chart_screen_tick();
    energy_profile_tick();
    config_screen_tick();

    delay(5);
}