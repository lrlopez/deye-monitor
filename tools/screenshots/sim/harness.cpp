// Arnés de preview: renderiza pantallas LVGL del proyecto en el PC y las vuelca a PPM.
#include <lvgl.h>
#include "Arduino.h"
#include "WiFi.h"
#include "config.h"
#include "ui_constants.h"
#include "solarman.h"
#include "dashboard.h"

uint32_t g_now_ms = 0;
WiFiMock WiFi;
SafeSerial DbgSerial;

static uint16_t* fb;
static uint32_t tick_cb() { return g_now_ms; }
static void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    const uint16_t* src = (const uint16_t*)px;
    int w = a->x2 - a->x1 + 1;
    for (int y = a->y1; y <= a->y2; y++)
        memcpy(&fb[y * SCREEN_WIDTH + a->x1], src + (y - a->y1) * w, w * 2);
    lv_display_flush_ready(d);
}
static void run(int ms) { for (int i = 0; i < ms / 20; i++) { g_now_ms += 20; lv_timer_handler(); } }

static void save(const char* path) {
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

#include "scenes.inc"

int main(int argc, char** argv) {
    const char* scene = argc > 1 ? argv[1] : "dashboard";
    const char* out   = argc > 2 ? argv[2] : "out/shot.ppm";
    lv_init();
    lv_tick_set_cb(tick_cb);
    fb = (uint16_t*)calloc(SCREEN_WIDTH * SCREEN_HEIGHT, 2);
    lv_display_t* d = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    static uint16_t* buf = (uint16_t*)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * 2);
    lv_display_set_buffers(d, buf, nullptr, SCREEN_WIDTH * SCREEN_HEIGHT * 2, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D1117), 0);
    lv_screen_load(scr);
    build_scene(scene, scr);
    save(out);
    return 0;
}
