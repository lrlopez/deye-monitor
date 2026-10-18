#include "home_button.h"
#include "ui_constants.h"

static lv_obj_t*        s_btn = nullptr;
static home_button_cb_t s_cb  = nullptr;

static void home_click_cb(lv_event_t*) {
    if (s_cb) s_cb();
}

void home_button_init(lv_obj_t* parent, home_button_cb_t on_tap) {
    s_cb = on_tap;
    // En el extremo izquierdo de la barra de navegación (el «‹» va a su derecha)
    s_btn = lv_button_create(parent);
    lv_obj_set_pos(s_btn, NAV_HOME_X, SY(2));
    lv_obj_set_size(s_btn, NAV_BTN_W, NAV_BTN_H);
    lv_obj_set_style_bg_color(s_btn, C_GBTN, 0);
    lv_obj_set_style_radius(s_btn, SS(5), 0);
    lv_obj_set_style_border_width(s_btn, 0, 0);
    lv_obj_set_style_shadow_width(s_btn, 0, 0);

    lv_obj_t* lbl = lv_label_create(s_btn);
    lv_label_set_text(lbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_font(lbl, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x4A9EFF), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(s_btn, home_click_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s_btn, LV_OBJ_FLAG_HIDDEN);   // se empieza en el dashboard
}

void home_button_set_dashboard(bool on_dashboard) {
    if (!s_btn) return;
    if (on_dashboard) lv_obj_add_flag(s_btn, LV_OBJ_FLAG_HIDDEN);
    else              lv_obj_remove_flag(s_btn, LV_OBJ_FLAG_HIDDEN);
}
