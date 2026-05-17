#include <time.h>
#include "stats_screen.h"
#include "storage.h"
#include "data_store.h"
#include "config.h"
#include "ui_constants.h"
#include "psram_cache.h"
#include "calendar_popup.h"

// ── Paleta ────────────────────────────────────────────────────────────────
#define C_BAR_BG lv_color_hex(0x21262D)
#define C_GBTN  lv_color_hex(0x21262D)
#define C_CON_PV   lv_color_hex(0x2ECC71)
#define C_CON_DIS  lv_color_hex(0x4A9EFF)
#define C_CON_IMP  lv_color_hex(0xBB6BD9)
#define C_PRO_LOAD lv_color_hex(0xF5C518)
#define C_PRO_CHG  lv_color_hex(0x4A9EFF)
#define C_PRO_EXP  lv_color_hex(0xE88080)

// ── Estado interno ────────────────────────────────────────────────────────
static DailyStats s_live;         // última actualización live
static int        s_offset = 0;   // 0=hoy, -1=ayer, …, -6
static bool       s_active = false;

// ── Widgets actualizables ─────────────────────────────────────────────────
static lv_obj_t *s_btn_prev, *s_btn_next, *s_lbl_date;
static lv_obj_t *con_arcs[3], *con_total_lbl, *con_leg_val[3], *con_leg_pct[3];
static lv_obj_t *pro_arcs[3], *pro_total_lbl, *pro_leg_val[3], *pro_leg_pct[3];
static lv_obj_t *s_no_data;

// ── Helpers de tiempo ─────────────────────────────────────────────────────
static uint32_t day_epoch_from_offset(int off) {
    time_t now; time(&now);
    now += (time_t)off * 86400;
    struct tm t; localtime_r(&now, &t);
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
    return (uint32_t)mktime(&t);
}

static const char* MESES[] = {
    "Enero","Febrero","Marzo","Abril","Mayo","Junio",
    "Julio","Agosto","Sept.","Octubre","Nov.","Dic."
};
static const char* DIASEM[] = {
    "Dom","Lun","Mar","Mie","Jue","Vie","Sab"
};

static void update_nav_label() {
    time_t t = (time_t)day_epoch_from_offset(s_offset);
    struct tm ti; localtime_r(&t, &ti);
    char buf[40];
    if      (s_offset ==  0) snprintf(buf,sizeof(buf),"Hoy, %d %s",  ti.tm_mday, MESES[ti.tm_mon]);
    else if (s_offset == -1) snprintf(buf,sizeof(buf),"Ayer, %d %s", ti.tm_mday, MESES[ti.tm_mon]);
    else                     snprintf(buf,sizeof(buf),"%s %d %s",
                                 DIASEM[ti.tm_wday], ti.tm_mday, MESES[ti.tm_mon]);
    lv_label_set_text(s_lbl_date, buf);

    (s_offset >= 0)  ? lv_obj_add_state(s_btn_next, LV_STATE_DISABLED)
                     : lv_obj_remove_state(s_btn_next, LV_STATE_DISABLED);
    (s_offset <= -6) ? lv_obj_add_state(s_btn_prev, LV_STATE_DISABLED)
                     : lv_obj_remove_state(s_btn_prev, LV_STATE_DISABLED);

    lv_obj_set_style_text_color(s_lbl_date,
        s_offset == 0 ? C_WHITE : lv_color_hex(0x4A9EFF), 0);
    lv_obj_set_style_text_color(s_lbl_date,
    s_offset == 0 ? C_WHITE : C_ACCENT, 0);
}

// ── Donut helpers ─────────────────────────────────────────────────────────
static void make_bg_ring(lv_obj_t* p, int cx, int cy) {
    lv_obj_t* a = lv_arc_create(p);
    lv_obj_set_size(a, DONUT_R*2, DONUT_R*2);
    lv_obj_set_pos(a, cx-DONUT_R, cy-DONUT_R);
    lv_arc_set_bg_angles(a, 0, 359);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, C_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, ARC_W,    LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP,  LV_PART_KNOB);
    lv_obj_set_style_border_width(a, 0, LV_PART_MAIN);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t* make_arc_seg(lv_obj_t* p, int cx, int cy, lv_color_t col) {
    lv_obj_t* a = lv_arc_create(p);

    lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    lv_obj_set_size(a, DONUT_R*2, DONUT_R*2);
    lv_obj_set_pos(a, cx-DONUT_R, cy-DONUT_R);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(a, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(a, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_color(a, col, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(a, ARC_W, LV_PART_INDICATOR);
    lv_arc_set_angles(a, 270, 270);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}

static void update_donut(lv_obj_t* arcs[], const float vals[], int n, float total) {
    float cum = 0.0f;
    for (int i = 0; i < n; i++) {
        float f = (total > 0.01f) ? vals[i] / total : 0.0f;
        f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);

        if (f < 0.005f) {
            // Segmento invisible
            lv_arc_set_angles(arcs[i], 270, 270);

        } else if (f > 0.995f) {
            // Segmento del 100%: círculo completo
            // lv_arc no acepta 0→360, usar 0→359
            lv_arc_set_angles(arcs[i], 0, 359);

        } else {
            uint16_t a0 = (uint16_t)fmodf(cum * 360.0f + 270.0f, 360.0f);
            uint16_t a1 = (uint16_t)fmodf((cum + f) * 360.0f + 270.0f, 360.0f);
            lv_arc_set_angles(arcs[i], a0, a1);
        }
        cum += f;
    }
}

static void make_legend_row(lv_obj_t* p, int x, int y, lv_color_t col,
                             const char* name, lv_obj_t** val_out, lv_obj_t** pct_out) {
    lv_obj_t* dot = lv_obj_create(p);
    lv_obj_set_size(dot, UI_DOT_SZ, UI_DOT_SZ);
    lv_obj_set_pos(dot, x, y + SY(4));
    lv_obj_set_style_bg_color(dot, col, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, 2, 0);

    lv_obj_t* ln = lv_label_create(p);
    lv_obj_set_pos(ln, x + UI_DOT_SZ + SX(5), y + SY(1));
    lv_obj_set_style_text_font(ln, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(ln, C_MUTED, 0);
    lv_label_set_text(ln, name);

    *pct_out = lv_label_create(p);
    lv_obj_set_pos(*pct_out, x + SX(150), y + SY(1));
    lv_obj_set_style_text_font(*pct_out, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(*pct_out, col, 0);
    lv_label_set_text(*pct_out, "--%");

    *val_out = lv_label_create(p);
    lv_obj_set_pos(*val_out, x + UI_DOT_SZ + SX(5), y + SY(16));
    lv_obj_set_style_text_font(*val_out, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(*val_out, C_WHITE, 0);
    lv_label_set_text(*val_out, "-.-- kWh");
}

// ── Renderizado central ───────────────────────────────────────────────────
static void render_stats(float pv, float exp_k, float imp,
                          float load, float bchg, float bdis) {
    float pv_direct = load - bdis - imp;
    if (pv_direct < 0) pv_direct = 0;
    float con_vals[3] = {pv_direct, bdis,  imp};
    float pro_vals[3] = {load - bdis - imp > 0 ? load - bdis - imp : 0,
                         bchg, exp_k};
    // Corregir pro: autoconsumo = pv - export - bchg
    float pv_to_load = pv - exp_k - bchg;
    if (pv_to_load < 0) pv_to_load = 0;
    pro_vals[0] = pv_to_load;

    float con_total = load;
    float pro_total = pv;

    update_donut(con_arcs, con_vals, 3, con_total);
    update_donut(pro_arcs, pro_vals, 3, pro_total);
    lv_label_set_text_fmt(con_total_lbl, "%.1f\nkWh", con_total);
    lv_label_set_text_fmt(pro_total_lbl, "%.1f\nkWh", pro_total);

    for (int i = 0; i < 3; i++) {
        float cp = con_total > 0.01f ? con_vals[i]/con_total*100.0f : 0;
        float pp = pro_total > 0.01f ? pro_vals[i]/pro_total*100.0f : 0;
        lv_label_set_text_fmt(con_leg_val[i], "%.2f kWh", con_vals[i]);
        lv_label_set_text_fmt(con_leg_pct[i], "%.1f%%",   cp);
        lv_label_set_text_fmt(pro_leg_val[i], "%.2f kWh", pro_vals[i]);
        lv_label_set_text_fmt(pro_leg_pct[i], "%.1f%%",   pp);
    }

    lv_obj_add_flag(s_no_data, LV_OBJ_FLAG_HIDDEN);
}

static void load_and_render(int offset) {
    update_nav_label();

    if (offset == 0) {
        // Día actual: usar datos live
        if (s_live.valid)
            render_stats(s_live.pv_kwh, s_live.export_kwh, s_live.import_kwh,
                         s_live.load_kwh, s_live.batt_charge_kwh,
                         s_live.batt_discharge_kwh);
        else
            lv_obj_remove_flag(s_no_data, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Días anteriores: último registro del día en DataStore
    uint32_t dep = day_epoch_from_offset(offset);
    Record5Min rec{};
    DailyRecord dr{};
    if (Cache.getDaily(dep, dr)) {
        DailyStats d = daily_record_to_stats(dr);
        render_stats(d.pv_kwh, d.export_kwh, d.import_kwh,
                    d.load_kwh, d.batt_charge_kwh, d.batt_discharge_kwh);
    } else {
        lv_obj_remove_flag(s_no_data, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Callbacks navegación ──────────────────────────────────────────────────
static void prev_cb(lv_event_t*) { if (s_offset > -6) { s_offset--; load_and_render(s_offset); } }
static void next_cb(lv_event_t*) { if (s_offset <  0) { s_offset++; load_and_render(s_offset); } }

// ── Helper botón nav ──────────────────────────────────────────────────────
static lv_obj_t* nav_btn(lv_obj_t* p, int x, int y, int w, int h,
                          const char* txt, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(p);
    lv_obj_set_pos(btn, x, y); lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, C_GBTN, 0);
    lv_obj_set_style_radius(btn, SS(5), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x4A9EFF), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

// ── Inicialización ────────────────────────────────────────────────────────
void stats_screen_init(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, C_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    // ── Barra de navegación ───────────────────────────────────────────────
    s_btn_prev = nav_btn(parent, SX(2), SY(2), NAV_BTN_W, NAV_BTN_H,
                        LV_SYMBOL_LEFT, prev_cb);

    // Botón →
    s_btn_next = nav_btn(parent, SCREEN_WIDTH-NAV_BTN_W-SX(2), SY(2),
                        NAV_BTN_W, NAV_BTN_H, LV_SYMBOL_RIGHT, next_cb);

    // Botón calendario — entre label y →
    lv_obj_t* btn_cal = nav_btn(parent,
        SCREEN_WIDTH - NAV_BTN_W - SX(4) - SX(34), SY(2),
        SX(34), NAV_BTN_H, LV_SYMBOL_LIST, nullptr);
    lv_obj_add_event_cb(btn_cal, [](lv_event_t*) {
        calendar_show(
            day_epoch_from_offset(s_offset),
            Cache.getOldestDailyEpoch(),
            [](uint32_t dep) {
                // Calcular offset a partir del epoch seleccionado
                time_t now; time(&now);
                struct tm tm_now; localtime_r(&now, &tm_now);
                tm_now.tm_hour=0;tm_now.tm_min=0;
                tm_now.tm_sec=0;tm_now.tm_isdst=-1;
                uint32_t today = (uint32_t)mktime(&tm_now);
                s_offset = (int)((int64_t)dep-(int64_t)today) / 86400;
                load_and_render(s_offset);
            });
    }, LV_EVENT_CLICKED, nullptr);

    // Label de fecha — ancho reducido para dejar sitio al botón cal
    s_lbl_date = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_date, NAV_BTN_W+SX(4), SY(7));
    lv_obj_set_width(s_lbl_date,
        SCREEN_WIDTH - 2*NAV_BTN_W - SX(34) - SX(10));
    lv_obj_set_style_text_align(s_lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_lbl_date, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(s_lbl_date, C_WHITE, 0);
    lv_label_set_text(s_lbl_date, "Hoy");
    // Tap en el título → volver a hoy
    lv_obj_add_flag(s_lbl_date, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lbl_date, [](lv_event_t*) {
        if (s_offset == 0) return;
        s_offset = 0;
        load_and_render(0);
    }, LV_EVENT_CLICKED, nullptr);

    // Separadores
    lv_obj_t* sep_h = lv_obj_create(parent);
    lv_obj_set_pos(sep_h, 0, STATS_NAV_H);
    lv_obj_set_size(sep_h, SCREEN_WIDTH, UI_SEP_H);
    lv_obj_set_style_bg_color(sep_h, lv_color_hex(0x161B22), 0);
    lv_obj_set_style_bg_opa(sep_h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep_h, 0, 0);

    lv_obj_t* sep_v = lv_obj_create(parent);
    lv_obj_set_size(sep_v, UI_SEP_H, SCREEN_HEIGHT - STATS_NAV_H);
    lv_obj_set_pos(sep_v, STATS_COL_W, STATS_NAV_H);
    lv_obj_set_style_bg_color(sep_v, lv_color_hex(0x161B22), 0);
    lv_obj_set_style_bg_opa(sep_v, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep_v, 0, 0);

    // Subtítulos de columna
    const char* subs[2] = {"CONSUMO", "PRODUCCION"};
    for (int c = 0; c < 2; c++) {
        lv_obj_t* s = lv_label_create(parent);
        lv_obj_set_pos(s, c*STATS_COL_W, STATS_NAV_H + SY(2));
        lv_obj_set_width(s, STATS_COL_W);
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(s, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(s, C_MUTED, 0);
        lv_label_set_text(s, subs[c]);
    }

    // ── Donuts ────────────────────────────────────────────────────────────
    int cx[2] = {STATS_COL_W/2, STATS_COL_W + STATS_COL_W/2};
    lv_color_t cc[3] = {C_CON_PV, C_CON_DIS, C_CON_IMP};
    lv_color_t pc[3] = {C_PRO_LOAD, C_PRO_CHG, C_PRO_EXP};

    make_bg_ring(parent, cx[0], STATS_DONUT_CY);
    for (int i = 0; i < 3; i++)
        con_arcs[i] = make_arc_seg(parent, cx[0], STATS_DONUT_CY, cc[i]);

    con_total_lbl = lv_label_create(parent);
    lv_obj_set_pos(con_total_lbl, cx[0] - DONUT_R/2, STATS_DONUT_CY - FONT_NORMAL_SIZE);
    lv_obj_set_size(con_total_lbl, DONUT_R, FONT_NORMAL_SIZE*2 + SY(4));
    lv_obj_set_style_text_align(con_total_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(con_total_lbl, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(con_total_lbl, C_WHITE, 0);
    lv_label_set_text(con_total_lbl, "--\nkWh");

    make_bg_ring(parent, cx[1], STATS_DONUT_CY);
    for (int i = 0; i < 3; i++)
        pro_arcs[i] = make_arc_seg(parent, cx[1], STATS_DONUT_CY, pc[i]);

    pro_total_lbl = lv_label_create(parent);
    lv_obj_set_pos(pro_total_lbl, cx[1] - DONUT_R/2, STATS_DONUT_CY - FONT_NORMAL_SIZE);
    lv_obj_set_size(pro_total_lbl, DONUT_R, FONT_NORMAL_SIZE*2 + SY(4));
    lv_obj_set_style_text_align(pro_total_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(pro_total_lbl, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(pro_total_lbl, C_WHITE, 0);
    lv_label_set_text(pro_total_lbl, "--\nkWh");

    // ── Leyendas ──────────────────────────────────────────────────────────
    const char* cn[3] = {"Solar directo", "Descarga bat.", "Importacion"};
    const char* pn[3] = {"Autoconsumo",   "Carga bat.",   "Exportacion"};
    for (int i = 0; i < 3; i++) {
        make_legend_row(parent, SX(4),
                        STATS_LEGEND_Y + i*STATS_LEG_ROW_H,
                        cc[i], cn[i],
                        &con_leg_val[i], &con_leg_pct[i]);
        make_legend_row(parent, STATS_COL_W + SX(4),
                        STATS_LEGEND_Y + i*STATS_LEG_ROW_H,
                        pc[i], pn[i],
                        &pro_leg_val[i], &pro_leg_pct[i]);
    }

    // ── Sin datos ─────────────────────────────────────────────────────────
    s_no_data = lv_label_create(parent);
    lv_obj_set_pos(s_no_data, 0, STATS_DONUT_CY - FONT_NORMAL_SIZE/2);
    lv_obj_set_width(s_no_data, SCREEN_WIDTH);
    lv_obj_set_style_text_align(s_no_data, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_no_data, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(s_no_data, C_MUTED, 0);
    lv_label_set_text(s_no_data, "Sin datos para este dia");
    lv_obj_add_flag(s_no_data, LV_OBJ_FLAG_HIDDEN);
}

// ── API pública ───────────────────────────────────────────────────────────
void stats_screen_update(const DailyStats& d) {
    s_live = d;
    if (s_offset == 0 && d.valid)
        render_stats(d.pv_kwh, d.export_kwh, d.import_kwh,
                     d.load_kwh, d.batt_charge_kwh, d.batt_discharge_kwh);
}

void stats_screen_set_active(bool active) {
    if (active) load_and_render(s_offset);
}