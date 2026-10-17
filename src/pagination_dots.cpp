#include "pagination_dots.h"
#include "ui_constants.h"
#include <Arduino.h>

// DEYE-56: antes 0x2D3748 al 50 % (casi invisibles sobre el fondo oscuro); ahora
// se distinguen bien y se leen como algo tocable
#define C_ACTIVE  lv_color_hex(0x4A9EFF)
#define C_PASSIVE lv_color_hex(0x6B7A92)

#define DOT_BIG   SS(8)    // diámetro del punto activo
#define DOT_SMALL SS(5)    // diámetro de los puntos inactivos
#define DOT_SPACING SX(14) // separación entre centros

const int BAR_H = SS(16);

static lv_obj_t* s_dots[10] = {};
static int       s_n        = 0;
// Versión mejorada — guardar centros en init() y usarlos en set()
static int s_center_x[10] = {};

void pagination_dots_init(lv_obj_t* parent, int n) {
    s_n = n;

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCREEN_WIDTH, BAR_H);
    lv_obj_set_pos(bar, 0, SCREEN_HEIGHT - BAR_H);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    // Centro X de cada punto
    int total_w = (n - 1) * DOT_SPACING + DOT_BIG;
    int start_x = (SCREEN_WIDTH - total_w) / 2 + DOT_BIG / 2;

    for (int i = 0; i < n; i++) {
        s_center_x[i] = start_x + i * DOT_SPACING;

        lv_obj_t* dot = lv_obj_create(bar);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

        // Tamaño y posición inicial (todos inactivos)
        int  d      = i == 0 ? DOT_BIG : DOT_SMALL;
        lv_obj_set_size(dot, d, d);
        lv_obj_set_pos(dot, s_center_x[i] - d/2, (BAR_H - d)/2);
        lv_obj_set_style_bg_color(dot, C_PASSIVE, 0);
        lv_obj_set_style_radius(dot, d/2, 0);
        lv_obj_set_style_opa(dot, LV_OPA_COVER, 0);

        s_dots[i] = dot;
    }

    // Activar el primero
    pagination_dots_set(0);
}

// ── Toque sobre los puntos (DEYE-56) ──────────────────────────────────────
static pagination_dots_cb_t s_cb = nullptr;

// Índice del punto más cercano al toque, o -1 si cae fuera de la franja inferior o
// a más de un espaciado de cualquier punto. La franja es la barra más un margen
// para el dedo (los puntos miden pocos píxeles).
static int dots_hit(const lv_point_t& p) {
    if (s_n <= 0 || p.y < SCREEN_HEIGHT - BAR_H - SY(10)) return -1;
    int best = -1, best_d = DOT_SPACING;
    for (int i = 0; i < s_n; i++) {
        int d = p.x > s_center_x[i] ? p.x - s_center_x[i] : s_center_x[i] - p.x;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

static void indev_click_cb(lv_event_t* e) {
    if (!s_cb) return;
    lv_indev_t* indev = lv_event_get_indev(e);
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int idx = dots_hit(p);
    if (idx >= 0) s_cb(idx);
}

void pagination_dots_set_handler(pagination_dots_cb_t cb) {
    s_cb = cb;
    static bool registered = false;
    if (registered) return;
    lv_indev_t* indev = lv_indev_get_next(nullptr);
    if (!indev) return;
    registered = true;
    // CLICKED del indev: solo se emite en toques que no han sido un arrastre
    lv_indev_add_event_cb(indev, indev_click_cb, LV_EVENT_CLICKED, nullptr);
}

void pagination_dots_set(int idx) {
    const int BAR_H = SS(16);

    for (int i = 0; i < s_n; i++) {
        bool active = (i == idx);
        int  d      = active ? DOT_BIG : DOT_SMALL;

        lv_obj_set_size(s_dots[i], d, d);
        lv_obj_set_pos(s_dots[i],
                       s_center_x[i] - d/2,
                       (BAR_H - d) / 2);
        lv_obj_set_style_bg_color(s_dots[i],
                                   active ? C_ACTIVE : C_PASSIVE, 0);
        lv_obj_set_style_radius(s_dots[i], d/2, 0);
        lv_obj_set_style_opa(s_dots[i], LV_OPA_COVER, 0);
    }
}