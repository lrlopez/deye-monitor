#include <time.h>
#include "energy_profile.h"
#include "ui_constants.h"
#include "config.h"
#include "psram_cache.h"
#include "data_store.h"

// ── Colores ───────────────────────────────────────────────────────────────
// Positivo (arriba): PV + Importación + Descarga bat.
#define EP_CC_PV    lv_color_hex(0xF5C518)   // producción FV (amarillo)
#define EP_CC_IMP   lv_color_hex(0xBB6BD9)   // importación red (violeta)
#define EP_CC_DIS   lv_color_hex(0x4A9EFF)   // descarga batería (azul)

// Negativo (abajo): Consumo + Exportación + Carga bat.
#define EP_CN_LOAD  lv_color_hex(0x2ECC71)   // consumo (verde)
#define EP_CN_EXP   lv_color_hex(0xE88080)   // exportación (salmón)
#define EP_CN_CHG   lv_color_hex(0x58C9A8)   // carga batería (teal)

// Auxiliares
#define EP_C_GLINE  lv_color_hex(0x21262D)   // gridlines
#define EP_C_ZERO   lv_color_hex(0x6E7A8E)   // línea cero

// ── Escala Y ──────────────────────────────────────────────────────────────
#define EP_GRID_STEP  20.0f    // kWh por gridline
#define EP_SCALE_DEF  60.0f    // escala por defecto (3 gridlines)

// ── Geometría ─────────────────────────────────────────────────────────────
#define EP_PAD_L     SX(36)
#define EP_PAD_R     SX(4)
#define EP_XLAB_H    (FONT_SMALL_SIZE + SY(2))
#define EP_LEG_ROW   (FONT_SMALL_SIZE + SY(5))
#define EP_LEG_H     (EP_LEG_ROW * 2 + SY(2))
#define EP_CHART_Y   NAV_H
#define EP_CHART_H   (SCREEN_HEIGHT - EP_CHART_Y)
#define EP_CHART_W   (SCREEN_WIDTH - EP_PAD_L - EP_PAD_R)

struct DayBar {
    float pv;       // producción FV
    float grid_imp; // importación red
    float batt_dis; // descarga batería
    float load;     // consumo
    float grid_exp; // exportación red
    float batt_chg; // carga batería
    bool  valid;
};

static DayBar   s_bars[31];
static int      s_ndays;
static int      s_year, s_month;
static float    s_max_kwh;
static int      s_offset = 0;

static lv_obj_t* s_tile      = nullptr;
static lv_obj_t* s_lbl_month = nullptr;
static lv_obj_t* s_btn_prev  = nullptr;
static lv_obj_t* s_btn_next  = nullptr;
static lv_obj_t* s_chart_obj = nullptr;
static lv_obj_t* s_no_data   = nullptr;
static bool      s_active       = false;
static bool      s_needs_reload = false;

static const char* const EP_MESES[] = {
    "Enero","Febrero","Marzo","Abril","Mayo","Junio",
    "Julio","Agosto","Sept.","Octubre","Nov.","Dic."
};

// ── Helpers ───────────────────────────────────────────────────────────────
static int days_in_month(int y, int m) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return days[m - 1];
}

static uint32_t month_start_epoch(int y, int m) {
    struct tm t = {};
    t.tm_year  = y - 1900;
    t.tm_mon   = m - 1;
    t.tm_mday  = 1;
    t.tm_isdst = -1;
    return (uint32_t)mktime(&t);
}

static void load_month_data() {
    time_t now; time(&now);
    struct tm tnow; localtime_r(&now, &tnow);
    int cur_y = tnow.tm_year + 1900;
    int cur_m = tnow.tm_mon + 1;

    int total_m = (cur_y - 1970) * 12 + (cur_m - 1) + s_offset;
    if (total_m < 0) total_m = 0;
    s_year  = 1970 + total_m / 12;
    s_month = total_m % 12 + 1;

    s_ndays = days_in_month(s_year, s_month);
    bool is_cur   = (s_year == cur_y && s_month == cur_m);
    int  last_day = is_cur ? tnow.tm_mday : s_ndays;

    uint32_t ep0  = month_start_epoch(s_year, s_month);
    float    mx   = 0.0f;

    for (int d = 0; d < s_ndays; d++) {
        s_bars[d] = {};
        if (d >= last_day) continue;

        DailyRecord dr;
        if (!Cache.getDaily(ep0 + (uint32_t)d * 86400, dr)) continue;

        float pv   = dr.pv_10wh     * 0.1f;
        float imp  = dr.import_10wh * 0.1f;
        float bdis = dr.bdis_10wh   * 0.1f;
        float load = dr.load_10wh   * 0.1f;
        float exprt= dr.export_10wh * 0.1f;
        float bchg = dr.bchg_10wh   * 0.1f;

        s_bars[d] = { pv, imp, bdis, load, exprt, bchg, true };

        float upper = pv + imp + bdis;
        float lower = load + exprt + bchg;
        float day_mx = upper > lower ? upper : lower;
        if (day_mx > mx) mx = day_mx;
    }

    // Escala: mínimo EP_SCALE_DEF kWh; si supera, redondear al múltiplo de EP_GRID_STEP
    if (mx <= EP_SCALE_DEF) {
        s_max_kwh = EP_SCALE_DEF;
    } else {
        int steps = (int)(mx / EP_GRID_STEP);
        if (mx > steps * EP_GRID_STEP) steps++;
        s_max_kwh = steps * EP_GRID_STEP;
    }
}

static void update_nav() {
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d", EP_MESES[s_month - 1], s_year);
    lv_label_set_text(s_lbl_month, buf);

    (s_offset <= -24) ? lv_obj_add_state(s_btn_prev, LV_STATE_DISABLED)
                      : lv_obj_remove_state(s_btn_prev, LV_STATE_DISABLED);
    (s_offset >= 0)   ? lv_obj_add_state(s_btn_next, LV_STATE_DISABLED)
                      : lv_obj_remove_state(s_btn_next, LV_STATE_DISABLED);
}

static void ep_reload();

// ── Callback de dibujo ────────────────────────────────────────────────────
static void chart_draw_cb(lv_event_t* e) {
    if (s_ndays <= 0 || s_max_kwh <= 0.0f) return;

    lv_layer_t* layer = lv_event_get_layer(e);
    lv_obj_t*   obj   = lv_event_get_target_obj(e);

    lv_area_t box;
    lv_obj_get_coords(obj, &box);

    int32_t total_h = box.y2 - box.y1;
    int32_t bar_ch  = total_h - EP_XLAB_H - EP_LEG_H;
    int32_t half_ch = bar_ch / 2;

    int32_t bar_x0  = box.x1 + EP_PAD_L;
    int32_t bar_x1  = bar_x0 + EP_CHART_W;
    int32_t zero_y  = box.y1 + half_ch;
    int32_t xlab_y  = zero_y + half_ch;
    int32_t leg_y   = xlab_y + EP_XLAB_H;

    // ── Gridlines + etiquetas Y ───────────────────────────────────────────
    int n_steps = (int)(s_max_kwh / EP_GRID_STEP + 0.5f);

    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.base.layer = layer;
    ldsc.width = 1;
    ldsc.opa   = LV_OPA_COVER;
    ldsc.color = EP_C_GLINE;

    lv_draw_label_dsc_t ydsc;
    lv_draw_label_dsc_init(&ydsc);
    ydsc.base.layer  = layer;
    ydsc.font        = &FONT_SMALL;
    ydsc.color       = C_MUTED;
    ydsc.opa         = LV_OPA_COVER;
    ydsc.align       = LV_TEXT_ALIGN_RIGHT;
    ydsc.text_local  = 1;

    char lbuf[8];
    for (int si = 1; si <= n_steps; si++) {
        float   val = si * EP_GRID_STEP;
        int32_t off = (int32_t)(val / s_max_kwh * half_ch);

        int32_t gy_pos = zero_y - off;
        int32_t gy_neg = zero_y + off;

        ldsc.p1 = { bar_x0, gy_pos }; ldsc.p2 = { bar_x1, gy_pos };
        lv_draw_line(layer, &ldsc);
        ldsc.p1 = { bar_x0, gy_neg }; ldsc.p2 = { bar_x1, gy_neg };
        lv_draw_line(layer, &ldsc);

        snprintf(lbuf, sizeof(lbuf), "%d", (int)val);
        ydsc.text = lbuf;
        lv_area_t la = { box.x1, gy_pos - FONT_SMALL_SIZE / 2 - 1,
                         bar_x0 - SX(3), gy_pos + FONT_SMALL_SIZE / 2 + 1 };
        lv_draw_label(layer, &ydsc, &la);

        snprintf(lbuf, sizeof(lbuf), "-%d", (int)val);
        la = { box.x1, gy_neg - FONT_SMALL_SIZE / 2 - 1,
                         bar_x0 - SX(3), gy_neg + FONT_SMALL_SIZE / 2 + 1 };
        lv_draw_label(layer, &ydsc, &la);
    }

    // Etiqueta unidad
    ydsc.text = "kWh";
    lv_area_t ua = { box.x1, box.y1, bar_x0 - SX(1), box.y1 + FONT_SMALL_SIZE + 2 };
    lv_draw_label(layer, &ydsc, &ua);

    // Línea cero
    ldsc.color = EP_C_ZERO;
    ldsc.p1 = { box.x1, zero_y }; ldsc.p2 = { bar_x1, zero_y };
    lv_draw_line(layer, &ldsc);

    // ── Barras ───────────────────────────────────────────────────────────
    float slot_f = (float)EP_CHART_W / s_ndays;
    float b_frac = (slot_f > 4.0f) ? 0.75f : 0.9f;
    int   bar_w  = (int)(slot_f * b_frac);
    if (bar_w < 2) bar_w = 2;

    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.base.layer   = layer;
    rdsc.bg_opa       = LV_OPA_COVER;
    rdsc.radius       = 0;
    rdsc.border_width = 0;

    lv_draw_label_dsc_t xdsc;
    lv_draw_label_dsc_init(&xdsc);
    xdsc.base.layer  = layer;
    xdsc.font        = &FONT_SMALL;
    xdsc.color       = C_MUTED;
    xdsc.opa         = LV_OPA_COVER;
    xdsc.align       = LV_TEXT_ALIGN_CENTER;
    xdsc.text_local  = 1;

    char dbuf[4];
    for (int d = 0; d < s_ndays; d++) {
        int32_t bx0 = bar_x0 + (int32_t)(d * slot_f + (slot_f - bar_w) / 2.0f);
        int32_t bx1 = bx0 + bar_w - 1;

        if (s_bars[d].valid) {
            // Positivo: PV (base) → Importación → Descarga, crece hacia arriba
            struct { float val; lv_color_t col; } pos[3] = {
                { s_bars[d].pv,       EP_CC_PV  },
                { s_bars[d].grid_imp, EP_CC_IMP },
                { s_bars[d].batt_dis, EP_CC_DIS },
            };
            float y_cur = 0.0f;
            for (int si = 0; si < 3; si++) {
                if (pos[si].val < 0.001f) continue;
                float   y_top = y_cur + pos[si].val;
                int32_t ry0   = zero_y - (int32_t)(y_top / s_max_kwh * half_ch);
                int32_t ry1   = zero_y - (int32_t)(y_cur / s_max_kwh * half_ch) - 1;
                if (ry0 > ry1) ry0 = ry1;
                rdsc.bg_color = pos[si].col;
                lv_area_t ra  = { bx0, ry0, bx1, ry1 };
                lv_draw_rect(layer, &rdsc, &ra);
                y_cur = y_top;
            }

            // Negativo: Consumo (base) → Exportación → Carga, crece hacia abajo
            struct { float val; lv_color_t col; } neg[3] = {
                { s_bars[d].load,     EP_CN_LOAD },
                { s_bars[d].grid_exp, EP_CN_EXP  },
                { s_bars[d].batt_chg, EP_CN_CHG  },
            };
            y_cur = 0.0f;
            for (int si = 0; si < 3; si++) {
                if (neg[si].val < 0.001f) continue;
                float   y_bot = y_cur + neg[si].val;
                int32_t ry0   = zero_y + (int32_t)(y_cur / s_max_kwh * half_ch) + 1;
                int32_t ry1   = zero_y + (int32_t)(y_bot / s_max_kwh * half_ch);
                if (ry0 > ry1) ry1 = ry0;
                rdsc.bg_color = neg[si].col;
                lv_area_t ra  = { bx0, ry0, bx1, ry1 };
                lv_draw_rect(layer, &rdsc, &ra);
                y_cur = y_bot;
            }
        }

        // Etiqueta día: 1, cada 5, último
        bool show = (d == 0) || ((d + 1) % 5 == 0) || (d == s_ndays - 1);
        if (show) {
            snprintf(dbuf, sizeof(dbuf), "%d", d + 1);
            xdsc.text = dbuf;
            int32_t lx = bar_x0 + (int32_t)((d + 0.5f) * slot_f);
            lv_area_t da = { lx - SX(10), xlab_y,
                             lx + SX(10), xlab_y + FONT_SMALL_SIZE };
            lv_draw_label(layer, &xdsc, &da);
        }
    }

    // ── Leyenda 2 filas (positivo fila 0, negativo fila 1) ────────────────
    struct { const char* lbl; lv_color_t col; } legs[6] = {
        { "FV",           EP_CC_PV   },
        { "Importacion",  EP_CC_IMP  },
        { "Bat. desc.",   EP_CC_DIS  },
        { "Consumo",      EP_CN_LOAD },
        { "Exportacion",  EP_CN_EXP  },
        { "Carga bat.",   EP_CN_CHG  },
    };

    lv_draw_rect_dsc_t dotdsc;
    lv_draw_rect_dsc_init(&dotdsc);
    dotdsc.base.layer   = layer;
    dotdsc.bg_opa       = LV_OPA_COVER;
    dotdsc.border_width = 0;
    dotdsc.radius       = 2;

    lv_draw_label_dsc_t llbl;
    lv_draw_label_dsc_init(&llbl);
    llbl.base.layer  = layer;
    llbl.font        = &FONT_SMALL;
    llbl.color       = C_MUTED;
    llbl.opa         = LV_OPA_COVER;
    llbl.align       = LV_TEXT_ALIGN_LEFT;
    llbl.text_local  = 1;

    int32_t dot_sz = UI_DOT_SZ;
    int32_t gap    = SX(3);
    int32_t item_w = SCREEN_WIDTH / 3;

    for (int i = 0; i < 6; i++) {
        int col = i % 3;
        int row = i / 3;
        int32_t ix = box.x1 + col * item_w + SX(4);
        int32_t iy = leg_y + row * EP_LEG_ROW + (EP_LEG_ROW - dot_sz) / 2;

        dotdsc.bg_color = legs[i].col;
        lv_area_t da = { ix, iy, ix + dot_sz - 1, iy + dot_sz - 1 };
        lv_draw_rect(layer, &dotdsc, &da);

        llbl.text = legs[i].lbl;
        lv_area_t la = { ix + dot_sz + gap,
                         leg_y + row * EP_LEG_ROW,
                         ix + item_w - SX(2),
                         leg_y + row * EP_LEG_ROW + EP_LEG_ROW };
        lv_draw_label(layer, &llbl, &la);
    }
}

// ── Recarga ───────────────────────────────────────────────────────────────
static void ep_reload() {
    load_month_data();
    update_nav();
    lv_obj_invalidate(s_chart_obj);

    bool has_data = false;
    for (int d = 0; d < s_ndays; d++) {
        if (s_bars[d].valid) { has_data = true; break; }
    }
    has_data ? lv_obj_add_flag(s_no_data, LV_OBJ_FLAG_HIDDEN)
             : lv_obj_remove_flag(s_no_data, LV_OBJ_FLAG_HIDDEN);
}

// ── Helpers de UI ─────────────────────────────────────────────────────────
static lv_obj_t* make_nav_btn(lv_obj_t* parent, const char* sym) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_set_size(btn, NAV_BTN_W, NAV_BTN_H);
    lv_obj_set_style_bg_color(btn, C_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, sym);
    lv_obj_set_style_text_color(lbl, C_WHITE, 0);
    lv_obj_center(lbl);
    return btn;
}

// ── API pública ───────────────────────────────────────────────────────────
void energy_profile_init(lv_obj_t* tile) {
    s_tile   = tile;
    s_offset = 0;

    lv_obj_set_style_bg_color(tile, C_BG, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    // ── Nav bar ───────────────────────────────────────────────────────────
    lv_obj_t* nav = lv_obj_create(tile);
    lv_obj_set_size(nav, SCREEN_WIDTH, NAV_H);
    lv_obj_set_pos(nav, 0, 0);
    lv_obj_set_style_bg_color(nav, C_BG, 0);
    lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 0, 0);
    lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    s_btn_prev = make_nav_btn(nav, LV_SYMBOL_LEFT);
    lv_obj_set_pos(s_btn_prev, 0, (NAV_H - NAV_BTN_H) / 2);
    lv_obj_add_event_cb(s_btn_prev, [](lv_event_t*) {
        s_offset--;
        ep_reload();
    }, LV_EVENT_CLICKED, nullptr);

    s_lbl_month = lv_label_create(nav);
    lv_obj_set_style_text_color(s_lbl_month, C_WHITE, 0);
    lv_obj_set_style_text_font(s_lbl_month, &FONT_NORMAL, 0);
    lv_obj_center(s_lbl_month);

    s_btn_next = make_nav_btn(nav, LV_SYMBOL_RIGHT);
    lv_obj_set_pos(s_btn_next, SCREEN_WIDTH - NAV_BTN_W, (NAV_H - NAV_BTN_H) / 2);
    lv_obj_add_event_cb(s_btn_next, [](lv_event_t*) {
        if (s_offset < 0) { s_offset++; ep_reload(); }
    }, LV_EVENT_CLICKED, nullptr);

    // ── Área de gráfica ───────────────────────────────────────────────────
    s_chart_obj = lv_obj_create(tile);
    lv_obj_set_pos(s_chart_obj, 0, EP_CHART_Y);
    lv_obj_set_size(s_chart_obj, SCREEN_WIDTH, EP_CHART_H);
    lv_obj_set_style_bg_color(s_chart_obj, C_BG, 0);
    lv_obj_set_style_bg_opa(s_chart_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chart_obj, 0, 0);
    lv_obj_set_style_pad_all(s_chart_obj, 0, 0);
    lv_obj_remove_flag(s_chart_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_chart_obj, chart_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);

    s_no_data = lv_label_create(s_chart_obj);
    lv_label_set_text(s_no_data, "Sin datos para este mes");
    lv_obj_set_style_text_color(s_no_data, C_MUTED, 0);
    lv_obj_set_style_text_font(s_no_data, &FONT_NORMAL, 0);
    lv_obj_align(s_no_data, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_no_data, LV_OBJ_FLAG_HIDDEN);

    ep_reload();
}

void energy_profile_tick() {
    if (s_needs_reload && s_active) {
        s_needs_reload = false;
        ep_reload();
    }
}

void energy_profile_set_active(bool active) {
    s_active = active;
    if (active) s_needs_reload = true;
}
