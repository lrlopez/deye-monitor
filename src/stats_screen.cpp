#include "stats_screen.h"
#include "storage.h"
#include <time.h>

// ── Paleta ────────────────────────────────────────────────────────────────
#define C_BG    lv_color_hex(0x0D1117)
#define C_CARD  lv_color_hex(0x161B22)
#define C_MUTED lv_color_hex(0x6E7681)
#define C_WHITE lv_color_hex(0xEAEAEA)
#define C_BAR_BG lv_color_hex(0x21262D)
#define C_GBTN  lv_color_hex(0x21262D)
#define C_CON_PV   lv_color_hex(0x2ECC71)
#define C_CON_DIS  lv_color_hex(0x4A9EFF)
#define C_CON_IMP  lv_color_hex(0xBB6BD9)
#define C_PRO_LOAD lv_color_hex(0xF5C518)
#define C_PRO_CHG  lv_color_hex(0x4A9EFF)
#define C_PRO_EXP  lv_color_hex(0xE88080)

// ── Geometría ─────────────────────────────────────────────────────────────
#define NAV_H    28
#define DONUT_R  55
#define ARC_W    18
#define COL_W   240
#define DONUT_CY (NAV_H + 75)   // 103
#define LEGEND_Y (NAV_H + 152)  // 180
#define LEG_ROW_H 30
#define DOT_SZ    8
#define PAD      10

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
        float f = (total > 0.01f) ? vals[i]/total : 0.0f;
        f = f < 0 ? 0 : (f > 1 ? 1 : f);
        if (f < 0.005f) {
            lv_arc_set_angles(arcs[i], 270, 270);
        } else {
            uint16_t a0 = (uint16_t)fmodf(cum*360.0f+270.0f, 360.0f);
            uint16_t a1 = (uint16_t)fmodf((cum+f)*360.0f+270.0f, 360.0f);
            lv_arc_set_angles(arcs[i], a0, a1);
        }
        cum += f;
    }
}

static void make_legend_row(lv_obj_t* p, int x, int y, lv_color_t col,
                             const char* name, lv_obj_t** val_out, lv_obj_t** pct_out) {
    lv_obj_t* dot = lv_obj_create(p);
    lv_obj_set_size(dot, DOT_SZ, DOT_SZ);
    lv_obj_set_pos(dot, x, y+4);
    lv_obj_set_style_bg_color(dot, col, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, 2, 0);

    lv_obj_t* ln = lv_label_create(p);
    lv_obj_set_pos(ln, x+DOT_SZ+5, y+1);
    lv_obj_set_style_text_font(ln, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ln, C_MUTED, 0);
    lv_label_set_text(ln, name);

    *pct_out = lv_label_create(p);
    lv_obj_set_pos(*pct_out, x+150, y+1);
    lv_obj_set_style_text_font(*pct_out, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(*pct_out, col, 0);
    lv_label_set_text(*pct_out, "--%");

    *val_out = lv_label_create(p);
    lv_obj_set_pos(*val_out, x+DOT_SZ+5, y+16);
    lv_obj_set_style_text_font(*val_out, &lv_font_montserrat_12, 0);
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
        if (s_live.valid)
            render_stats(s_live.pv_kwh, s_live.export_kwh, s_live.import_kwh,
                         s_live.load_kwh, s_live.batt_charge_kwh, s_live.batt_discharge_kwh);
        else
            lv_obj_remove_flag(s_no_data, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    uint32_t dep = day_epoch_from_offset(offset);
    DailyRecord rec{};
    if (Storage.getDayRecord(dep, rec)) {
        render_stats(rec.pv_kwh, rec.export_kwh, rec.import_kwh,
                     rec.load_kwh, rec.batt_charge_kwh, rec.batt_discharge_kwh);
    } else {
        // Limpiar donuts y mostrar "Sin datos"
        for (int i = 0; i < 3; i++) {
            lv_arc_set_angles(con_arcs[i], 270, 270);
            lv_arc_set_angles(pro_arcs[i], 270, 270);
        }
        lv_label_set_text(con_total_lbl, "--\nkWh");
        lv_label_set_text(pro_total_lbl, "--\nkWh");
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
    lv_obj_set_style_radius(btn, 5, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
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
    s_btn_prev = nav_btn(parent, 2,   2, 44, NAV_H-4, LV_SYMBOL_LEFT,  prev_cb);
    s_btn_next = nav_btn(parent, 434, 2, 44, NAV_H-4, LV_SYMBOL_RIGHT, next_cb);

    s_lbl_date = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_date, 48, 7); lv_obj_set_width(s_lbl_date, 384);
    lv_obj_set_style_text_align(s_lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_lbl_date, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_date, C_WHITE, 0);
    lv_label_set_text(s_lbl_date, "Hoy");

    // Separador y subtítulos de columna
    lv_obj_t* sep = lv_obj_create(parent);
    lv_obj_set_pos(sep, 0, NAV_H); lv_obj_set_size(sep, 480, 1);
    lv_obj_set_style_bg_color(sep, C_CARD, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    lv_obj_t* sep_v = lv_obj_create(parent);
    lv_obj_set_size(sep_v, 1, 272-NAV_H);
    lv_obj_set_pos(sep_v, COL_W, NAV_H);
    lv_obj_set_style_bg_color(sep_v, C_CARD, 0);
    lv_obj_set_style_bg_opa(sep_v, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep_v, 0, 0);

    const char* subs[2] = {"CONSUMO", "PRODUCCION"};
    for (int c = 0; c < 2; c++) {
        lv_obj_t* s = lv_label_create(parent);
        lv_obj_set_pos(s, c*COL_W, NAV_H+2);
        lv_obj_set_width(s, COL_W);
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(s, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s, C_MUTED, 0);
        lv_label_set_text(s, subs[c]);
    }

    // ── Donuts ────────────────────────────────────────────────────────────
    int cx[2] = {120, 360};
    lv_color_t cc[3] = {C_CON_PV, C_CON_DIS, C_CON_IMP};
    lv_color_t pc[3] = {C_PRO_LOAD, C_PRO_CHG, C_PRO_EXP};

    make_bg_ring(parent, cx[0], DONUT_CY);
    for (int i = 0; i < 3; i++) con_arcs[i] = make_arc_seg(parent, cx[0], DONUT_CY, cc[i]);

    con_total_lbl = lv_label_create(parent);
    lv_obj_set_pos(con_total_lbl, cx[0]-32, DONUT_CY-18);
    lv_obj_set_size(con_total_lbl, 64, 36);
    lv_obj_set_style_text_align(con_total_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(con_total_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(con_total_lbl, C_WHITE, 0);
    lv_label_set_text(con_total_lbl, "--\nkWh");

    make_bg_ring(parent, cx[1], DONUT_CY);
    for (int i = 0; i < 3; i++) pro_arcs[i] = make_arc_seg(parent, cx[1], DONUT_CY, pc[i]);

    pro_total_lbl = lv_label_create(parent);
    lv_obj_set_pos(pro_total_lbl, cx[1]-32, DONUT_CY-18);
    lv_obj_set_size(pro_total_lbl, 64, 36);
    lv_obj_set_style_text_align(pro_total_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(pro_total_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pro_total_lbl, C_WHITE, 0);
    lv_label_set_text(pro_total_lbl, "--\nkWh");

    // ── Leyendas ──────────────────────────────────────────────────────────
    const char* cn[3] = {"Solar directo", "Descarga bat.", "Importacion"};
    const char* pn[3] = {"Autoconsumo",   "Carga bat.",   "Exportacion"};
    for (int i = 0; i < 3; i++) {
        make_legend_row(parent, 4,       LEGEND_Y+i*LEG_ROW_H, cc[i], cn[i],
                        &con_leg_val[i], &con_leg_pct[i]);
        make_legend_row(parent, COL_W+4, LEGEND_Y+i*LEG_ROW_H, pc[i], pn[i],
                        &pro_leg_val[i], &pro_leg_pct[i]);
    }

    // ── Sin datos ─────────────────────────────────────────────────────────
    s_no_data = lv_label_create(parent);
    lv_obj_set_pos(s_no_data, 0, DONUT_CY-8);
    lv_obj_set_width(s_no_data, 480);
    lv_obj_set_style_text_align(s_no_data, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_no_data, &lv_font_montserrat_14, 0);
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