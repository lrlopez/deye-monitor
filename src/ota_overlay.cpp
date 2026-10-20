#include "ota_overlay.h"
#include <lvgl.h>
#include "config.h"
#include "ui_constants.h"

static lv_obj_t* s_ota_overlay = nullptr;   // para poder retirarlo si la actualización falla

void ota_overlay_hide() {
    if (s_ota_overlay) { lv_obj_delete(s_ota_overlay); s_ota_overlay = nullptr; }
}

void ota_overlay_show() {
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
    lv_obj_set_size(card, SX(300), SY(196));
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

    // Aviso: las escrituras en flash interrumpen el refresco de la pantalla
    lv_obj_t* warn = lv_label_create(card);
    lv_label_set_text(warn, "La pantalla puede parpadear\ndurante el proceso");
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(warn, C_MUTED, 0);
    lv_obj_set_style_text_font(warn, &FONT_SMALL, 0);
    lv_obj_align_to(warn, sub, LV_ALIGN_OUT_BOTTOM_MID, 0, SY(7));
}
