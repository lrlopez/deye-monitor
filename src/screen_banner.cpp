#include "screen_banner.h"
#include "ui_constants.h"
#include "config.h"
#include <freertos/queue.h>

struct BannerMsg { AlertType type; int32_t value; };
static QueueHandle_t s_queue      = nullptr;
static lv_obj_t*     s_banner_obj = nullptr;

void screen_banner_init() {
    s_queue = xQueueCreate(8, sizeof(BannerMsg));
}

void screen_banner_enqueue(AlertType type, int32_t value) {
    if (!s_queue) return;
    BannerMsg msg{type, value};
    xQueueSend(s_queue, &msg, 0);
}

static void banner_close_cb(lv_timer_t* /*t*/) {
    if (s_banner_obj) { lv_obj_delete(s_banner_obj); s_banner_obj = nullptr; }
}

static void show_banner(const char* text, lv_color_t bg) {
    if (s_banner_obj) { lv_obj_delete(s_banner_obj); s_banner_obj = nullptr; }

    lv_obj_t* b = lv_obj_create(lv_layer_top());
    lv_obj_set_size(b, SCREEN_WIDTH, SY(26));
    lv_obj_set_pos(b, 0, TOP_BAR_H);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* lbl = lv_label_create(b);
    lv_obj_set_width(lbl, SCREEN_WIDTH - SX(16));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(lbl, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);

    s_banner_obj = b;
    lv_timer_t* t = lv_timer_create(banner_close_cb, 5000, nullptr);
    lv_timer_set_repeat_count(t, 1);
}

static void format_and_show(const BannerMsg& m) {
    char text[72];
    lv_color_t bg;
    switch (m.type) {
    case AlertType::BATT_LOW:
        snprintf(text, sizeof(text), LV_SYMBOL_BATTERY_1 " Bater\xC3\xAD\x61 baja: %d%%", (int)m.value);
        bg = lv_color_hex(0xB7770D);  // naranja oscuro
        break;
    case AlertType::BATT_CRITICAL:
        snprintf(text, sizeof(text), LV_SYMBOL_BATTERY_EMPTY " Bater\xC3\xAD\x61 cr\xC3\xAD\x74ica: %d%%", (int)m.value);
        bg = lv_color_hex(0x8B1A1A);  // rojo oscuro
        break;
    case AlertType::BATT_RECOVERED:
        snprintf(text, sizeof(text), LV_SYMBOL_BATTERY_FULL " Bater\xC3\xAD\x61 recuperada: %d%%", (int)m.value);
        bg = lv_color_hex(0x1A5C2E);  // verde oscuro
        break;
    case AlertType::SOLAR_START:
        snprintf(text, sizeof(text), LV_SYMBOL_SUN " Producci\xC3\xB3n solar activa: %d W", (int)m.value);
        bg = lv_color_hex(0x7A6010);  // amarillo oscuro
        break;
    case AlertType::SOLAR_STOP:
        snprintf(text, sizeof(text), LV_SYMBOL_MOON " Producci\xC3\xB3n solar parada");
        bg = lv_color_hex(0x21262D);
        break;
    case AlertType::LOGGER_FAIL:
        snprintf(text, sizeof(text), LV_SYMBOL_WARNING " Sin datos del inversor");
        bg = lv_color_hex(0x8B1A1A);
        break;
    case AlertType::GRID_OUTAGE:
        snprintf(text, sizeof(text), LV_SYMBOL_WARNING " Corte de red el\xC3\xA9\x63trica");
        bg = lv_color_hex(0x8B1A1A);
        break;
    case AlertType::GRID_RESTORED:
        snprintf(text, sizeof(text), LV_SYMBOL_PLUG " Red el\xC3\xA9\x63trica restaurada");
        bg = lv_color_hex(0x1A5C2E);
        break;
    default:
        snprintf(text, sizeof(text), LV_SYMBOL_BELL " Alerta del sistema");
        bg = lv_color_hex(0x21262D);
        break;
    }
    show_banner(text, bg);
}

void screen_banner_tick() {
    if (!s_queue) return;
    BannerMsg msg{};
    if (xQueueReceive(s_queue, &msg, 0) == pdTRUE)
        format_and_show(msg);
}
