#pragma once
#include <lvgl.h>
#include "ui_constants.h"

// Escala un cuadro de diálogo modal (lv_msgbox) a la resolución de la placa, como el resto de la
// interfaz. Por defecto LVGL lo dimensiona con su tema (fuente de 14 px y un ancho de unos cientos de px
// pensados para una pantalla pequeña), y en la Guition JC1060P470 (1024×600) quedaba diminuto.
// Llamar DESPUÉS de añadir título, texto y botones: el tamaño de los botones se aplica a los existentes.
inline void ui_msgbox_scale(lv_obj_t* mbox) {
    lv_obj_set_style_text_font(mbox, &FONT_NORMAL, 0);      // título, texto y botones heredan la fuente
    lv_obj_set_width(mbox, SX(300));
    // LVGL fija la altura de cabecera y pie en función del DPI (≈43 px): con fuentes y botones mayores se
    // recortaban; que sigan al contenido.
    if (lv_obj_t* header = lv_msgbox_get_header(mbox)) {
        lv_obj_set_height(header, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(header, SX(8), 0);
    }
    if (lv_obj_t* content = lv_msgbox_get_content(mbox)) lv_obj_set_style_pad_all(content, SX(10), 0);
    if (lv_obj_t* footer = lv_msgbox_get_footer(mbox)) {
        // Los botones llevan altura propia (si no, se dimensionan respecto al pie y este colapsa) y el pie
        // sigue al contenido; si no caben en una fila (textos largos en una pantalla pequeña), pasan a otra.
        const int btn_h = FONT_NORMAL_SIZE + SY(14);
        lv_obj_set_height(footer, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(footer, SX(8), 0);
        lv_obj_set_style_pad_gap(footer, SX(8), 0);
        const uint32_t n = lv_obj_get_child_count(footer);
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_t* b = lv_obj_get_child(footer, i);
            lv_obj_set_height(b, btn_h);
            lv_obj_set_style_min_width(b, SX(90), 0);
            lv_obj_set_style_radius(b, UI_RADIUS, 0);
        }
    }
}
