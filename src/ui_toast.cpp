#include "ui_toast.h"
#include "ui_constants.h"

static lv_obj_t*   s_toast = nullptr;
static lv_timer_t* s_timer = nullptr;

static void toast_close() {
    if (s_timer) { lv_timer_delete(s_timer); s_timer = nullptr; }
    if (s_toast) { lv_obj_delete(s_toast); s_toast = nullptr; }
}

static void toast_timer_cb(lv_timer_t*) {
    s_timer = nullptr;            // el timer es de un solo disparo y se borra solo
    if (s_toast) { lv_obj_delete(s_toast); s_toast = nullptr; }
}

void ui_toast_show(const char* text, uint32_t duration_ms) {
    toast_close();               // uno solo a la vez

    lv_obj_t* p = lv_obj_create(lv_layer_top());
    lv_obj_set_size(p, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(p, SCREEN_WIDTH - 2 * UI_MARGIN, 0);
    lv_obj_set_style_bg_color(p, C_LIST, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, C_BORDER, 0);
    lv_obj_set_style_border_width(p, UI_BORDER, 0);
    lv_obj_set_style_radius(p, UI_RADIUS, 0);
    lv_obj_set_style_pad_hor(p, SX(10), 0);
    lv_obj_set_style_pad_ver(p, SY(6), 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE);       // no intercepta toques
    lv_obj_align(p, LV_ALIGN_BOTTOM_MID, 0, -(SS(16) + SY(8)));   // por encima de los puntos de paginación

    lv_obj_t* l = lv_label_create(p);
    lv_obj_set_style_text_font(l, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, C_WHITE, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);

    s_toast = p;
    s_timer = lv_timer_create(toast_timer_cb, duration_ms, nullptr);
    lv_timer_set_repeat_count(s_timer, 1);
}
