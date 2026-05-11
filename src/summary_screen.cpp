#include "summary_screen.h"
#include "storage.h"
#include <time.h>

// ── Paleta ────────────────────────────────────────────────────────────────
#define C_BG      lv_color_hex(0x0D1117)
#define C_SEP     lv_color_hex(0x21262D)
#define C_MUTED   lv_color_hex(0x6E7681)
#define C_WHITE   lv_color_hex(0xEAEAEA)

// CONSUMO: solar directo, descarga bat, importación
#define C_CON_PV  lv_color_hex(0x2ECC71)
#define C_CON_DIS lv_color_hex(0x4A9EFF)
#define C_CON_IMP lv_color_hex(0xBB6BD9)

// PRODUCCIÓN: autoconsumo, carga bat, exportación
#define C_PRO_LOD lv_color_hex(0xF5C518)
#define C_PRO_CHG lv_color_hex(0x1A56DB)
#define C_PRO_EXP lv_color_hex(0xE88080)

// ── Geometría ─────────────────────────────────────────────────────────────
#define NAV_H   28
#define LEG_H   24
#define CONT_H  (272 - NAV_H - LEG_H)   // 220
#define ROW_H   (CONT_H / 7)             // 31
#define DAY_W   44
#define BAR_X   DAY_W
#define BAR_W   (480 - BAR_X - 4)        // 432
#define BAR_H   10
#define BAR_GAP  4

// ── Datos de dibujo por fila (pool estático) ──────────────────────────────
struct RowDrawData {
    float con[3];    // pv_direct, discharge, import
    float pro[3];    // autoconsumo, charge, export
    float con_max;   // máximo consumo de la semana → escala de la barra
    float pro_max;
    bool  has_data;
};

static RowDrawData  s_rdd[7];
static DailyRecord  s_recs[7];
static int          s_offset = 0;
static bool         s_needs_reload = false;
static DailyStats   s_live_daily{};
static bool         s_live_valid = false;

// ── Widgets ───────────────────────────────────────────────────────────────
static lv_obj_t *s_btn_prev, *s_btn_next, *s_lbl_period;
static lv_obj_t *s_popup, *s_popup_title;
static lv_obj_t *s_popup_con_total, *s_popup_pro_total;
static lv_obj_t *s_popup_con_vals[3], *s_popup_con_pcts[3];
static lv_obj_t *s_popup_pro_vals[3], *s_popup_pro_pcts[3];
static lv_obj_t* s_rows[7];
static lv_obj_t* s_row_labels[7];

static const lv_color_t s_con_col[3] = {C_CON_PV, C_CON_DIS, C_CON_IMP};
static const lv_color_t s_pro_col[3] = {C_PRO_LOD, C_PRO_CHG, C_PRO_EXP};

static const char* DIAS[]   = {"Dom","Lun","Mar","Mie","Jue","Vie","Sab"};
static const char* MESES_S[]= {"Ene","Feb","Mar","Abr","May","Jun",
                                 "Jul","Ago","Sep","Oct","Nov","Dic"};
static const char* MESES_L[]= {"Enero","Febrero","Marzo","Abril","Mayo","Junio",
                                 "Julio","Agosto","Sept.","Octubre","Nov.","Dic."};

// ═════════════════════════════════════════════════════════════════════════
// Tiempo
// ═════════════════════════════════════════════════════════════════════════
static uint32_t monday_epoch(int week_offset) {
    time_t now; time(&now);
    struct tm tm; localtime_r(&now, &tm);
    int since_mon = (tm.tm_wday == 0) ? 6 : tm.tm_wday - 1;
    time_t mon = now - (time_t)since_mon * 86400
                     + (time_t)week_offset * 7 * 86400;
    struct tm mt; localtime_r(&mon, &mt);
    mt.tm_hour = 0; mt.tm_min = 0; mt.tm_sec = 0; mt.tm_isdst = -1;
    return (uint32_t)mktime(&mt);
}

// ═════════════════════════════════════════════════════════════════════════
// Carga de datos
// ═════════════════════════════════════════════════════════════════════════
static void load_data() {
    uint32_t mon = monday_epoch(s_offset);

    // Época de medianoche de hoy para comparar
    time_t now_t; time(&now_t);
    struct tm now_tm; localtime_r(&now_t, &now_tm);
    now_tm.tm_hour = 0; now_tm.tm_min = 0;
    now_tm.tm_sec  = 0; now_tm.tm_isdst = -1;
    uint32_t today_ep = (uint32_t)mktime(&now_tm);

    float con_max = 0.01f, pro_max = 0.01f;

    // Pasada 1: cargar registros
    for (int i = 0; i < 7; i++) {
        uint32_t dep = mon + (uint32_t)i * 86400;
        DailyRecord rec{}; rec.timestamp = dep;

        if (dep == today_ep && s_live_valid) {
            // Día actual: convertir DailyStats live a DailyRecord
            rec.pv_kwh             = s_live_daily.pv_kwh;
            rec.export_kwh         = s_live_daily.export_kwh;
            rec.import_kwh         = s_live_daily.import_kwh;
            rec.load_kwh           = s_live_daily.load_kwh;
            rec.batt_charge_kwh    = s_live_daily.batt_charge_kwh;
            rec.batt_discharge_kwh = s_live_daily.batt_discharge_kwh;
        } else {
            Storage.getDayRecord(dep, rec);
        }

        s_recs[i] = rec;
        if (rec.load_kwh > con_max) con_max = rec.load_kwh;
        if (rec.pv_kwh   > pro_max) pro_max = rec.pv_kwh;
    }

    // Pasada 2: calcular segmentos 
    for (int i = 0; i < 7; i++) {
        const DailyRecord& r = s_recs[i];
        bool has = (r.pv_kwh > 0.01f || r.load_kwh > 0.01f);

        float pv_direct  = r.load_kwh - r.batt_discharge_kwh - r.import_kwh;
        if (pv_direct  < 0) pv_direct  = 0;
        float pv_to_load = r.pv_kwh - r.export_kwh - r.batt_charge_kwh;
        if (pv_to_load < 0) pv_to_load = 0;

        s_rdd[i] = {
            {pv_direct, r.batt_discharge_kwh, r.import_kwh},
            {pv_to_load, r.batt_charge_kwh, r.export_kwh},
            con_max, pro_max, has
        };
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Draw callback — barras sin objetos hijo
// ═════════════════════════════════════════════════════════════════════════
static void row_draw_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;

    lv_obj_t*     obj   = (lv_obj_t*)lv_event_get_target(e);
    RowDrawData*  rd    = (RowDrawData*)lv_event_get_user_data(e);
    lv_layer_t*   layer = lv_event_get_layer(e);

    lv_area_t area; lv_obj_get_coords(obj, &area);
    int h = lv_obj_get_height(obj);

    int bars_h = BAR_H * 2 + BAR_GAP;
    int y0     = area.y1 + (h - bars_h) / 2;
    int y1     = y0 + BAR_H + BAR_GAP;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = 2; dsc.border_width = 0;

    // Fondos grises
    dsc.bg_color = lv_color_hex(0x21262D); dsc.bg_opa = LV_OPA_COVER;
    lv_area_t bg0 = {(lv_coord_t)(area.x1+BAR_X), (lv_coord_t)y0,
                     (lv_coord_t)(area.x1+BAR_X+BAR_W-1),(lv_coord_t)(y0+BAR_H-1)};
    lv_area_t bg1 = {(lv_coord_t)(area.x1+BAR_X), (lv_coord_t)y1,
                     (lv_coord_t)(area.x1+BAR_X+BAR_W-1),(lv_coord_t)(y1+BAR_H-1)};
    lv_draw_rect(layer, &dsc, &bg0);
    lv_draw_rect(layer, &dsc, &bg1);

    if (!rd->has_data) return;

    // Barra CONSUMO
    float con_tot = rd->con[0] + rd->con[1] + rd->con[2];
    int   bar0_w  = (rd->con_max > 0.01f)
                    ? (int)(con_tot / rd->con_max * BAR_W) : 0;
    if (bar0_w > 0) {
        int x = area.x1 + BAR_X;
        for (int s = 0; s < 3; s++) {
            int sw = (con_tot > 0.001f)
                     ? (int)(rd->con[s] / con_tot * bar0_w) : 0;
            if (sw < 1) continue;
            dsc.bg_color = s_con_col[s];
            lv_area_t seg = {(lv_coord_t)x, (lv_coord_t)y0,
                             (lv_coord_t)(x+sw-1),(lv_coord_t)(y0+BAR_H-1)};
            lv_draw_rect(layer, &dsc, &seg);
            x += sw;
        }
    }

    // Barra PRODUCCIÓN
    float pro_tot = rd->pro[0] + rd->pro[1] + rd->pro[2];
    int   bar1_w  = (rd->pro_max > 0.01f)
                    ? (int)(pro_tot / rd->pro_max * BAR_W) : 0;
    if (bar1_w > 0) {
        int x = area.x1 + BAR_X;
        for (int s = 0; s < 3; s++) {
            int sw = (pro_tot > 0.001f)
                     ? (int)(rd->pro[s] / pro_tot * bar1_w) : 0;
            if (sw < 1) continue;
            dsc.bg_color = s_pro_col[s];
            lv_area_t seg = {(lv_coord_t)x, (lv_coord_t)y1,
                             (lv_coord_t)(x+sw-1),(lv_coord_t)(y1+BAR_H-1)};
            lv_draw_rect(layer, &dsc, &seg);
            x += sw;
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Popup
// ═════════════════════════════════════════════════════════════════════════
static void popup_close_cb(lv_event_t*) {
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
}

static void show_popup(int idx) {
    if (idx < 0 || idx >= 7 || !s_rdd[idx].has_data) return;

    const DailyRecord& r = s_recs[idx];
    time_t t = (time_t)r.timestamp;
    struct tm ti; localtime_r(&t, &ti);

    char title[40];
    snprintf(title, sizeof(title), "%s, %d de %s",
             DIAS[ti.tm_wday], ti.tm_mday, MESES_L[ti.tm_mon]);
    lv_label_set_text(s_popup_title, title);

    // CONSUMO
    float con_tot = r.load_kwh;
    lv_label_set_text_fmt(s_popup_con_total, "Consumo  %.2f kWh", con_tot);
    for (int i = 0; i < 3; i++) {
        float v   = s_rdd[idx].con[i];
        int   pct = (con_tot > 0.01f) ? (int)(v / con_tot * 100.f) : 0;
        lv_label_set_text_fmt(s_popup_con_vals[i], "%.2f kWh", v);
        lv_label_set_text_fmt(s_popup_con_pcts[i], "%d%%", pct);
    }

    // PRODUCCIÓN
    float pro_tot = r.pv_kwh;
    lv_label_set_text_fmt(s_popup_pro_total, "Produccion  %.2f kWh", pro_tot);
    for (int i = 0; i < 3; i++) {
        float v   = s_rdd[idx].pro[i];
        int   pct = (pro_tot > 0.01f) ? (int)(v / pro_tot * 100.f) : 0;
        lv_label_set_text_fmt(s_popup_pro_vals[i], "%.2f kWh", v);
        lv_label_set_text_fmt(s_popup_pro_pcts[i], "%d%%", pct);
    }

    lv_obj_remove_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_popup);
}

static void row_click_cb(lv_event_t* e) {
    show_popup((int)(intptr_t)lv_event_get_user_data(e));
}

// ═════════════════════════════════════════════════════════════════════════
// Navegación
// ═════════════════════════════════════════════════════════════════════════
static void update_nav() {
    uint32_t mon = monday_epoch(s_offset);
    uint32_t sun = mon + 6 * 86400;
    time_t t0 = mon, t1 = sun;
    struct tm tm0, tm1;
    localtime_r(&t0, &tm0); localtime_r(&t1, &tm1);

    char buf[40];
    if (s_offset == 0)
        snprintf(buf, sizeof(buf), "Esta semana");
    else if (tm0.tm_mon == tm1.tm_mon)
        snprintf(buf, sizeof(buf), "%d - %d de %s",
                 tm0.tm_mday, tm1.tm_mday, MESES_S[tm0.tm_mon]);
    else
        snprintf(buf, sizeof(buf), "%d %s - %d %s",
                 tm0.tm_mday, MESES_S[tm0.tm_mon],
                 tm1.tm_mday, MESES_S[tm1.tm_mon]);
    lv_label_set_text(s_lbl_period, buf);

    (s_offset >= 0)   ? lv_obj_add_state(s_btn_next, LV_STATE_DISABLED)
                      : lv_obj_remove_state(s_btn_next, LV_STATE_DISABLED);
    (s_offset <= -11) ? lv_obj_add_state(s_btn_prev, LV_STATE_DISABLED)
                      : lv_obj_remove_state(s_btn_prev, LV_STATE_DISABLED);
}

static void render_content() {
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    update_nav();

    uint32_t mon = monday_epoch(s_offset);

    for (int i = 0; i < 7; i++) {
        // Actualizar datos de dibujo (el draw callback los lee en cada frame)
        // s_rdd[i] ya fue rellenado por load_data()

        // Actualizar etiqueta
        time_t t = (time_t)(mon + (uint32_t)i * 86400);
        struct tm ti; localtime_r(&t, &ti);
        char buf[10];
        snprintf(buf, sizeof(buf), "%s %d", DIAS[ti.tm_wday], ti.tm_mday);
        lv_label_set_text(s_row_labels[i], buf);
        lv_obj_set_style_text_color(s_row_labels[i],
            s_rdd[i].has_data ? C_WHITE : C_MUTED, 0);

        // Forzar redibujado de la fila (el draw callback pinta las barras)
        lv_obj_invalidate(s_rows[i]);
    }
}

static void load_and_render() { load_data(); render_content(); }

static void prev_cb(lv_event_t*) {
    if (s_offset > -11) { s_offset--; load_and_render(); }
}
static void next_cb(lv_event_t*) {
    if (s_offset < 0)  { s_offset++; load_and_render(); }
}

// ═════════════════════════════════════════════════════════════════════════
// Helpers de UI
// ═════════════════════════════════════════════════════════════════════════
static lv_obj_t* nav_btn(lv_obj_t* p, int x, const char* txt, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(p);
    lv_obj_set_pos(btn, x, 2); lv_obj_set_size(btn, 36, NAV_H-4);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x21262D), 0);
    lv_obj_set_style_radius(btn, 5, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t* l = lv_label_create(btn);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x4A9EFF), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

static void popup_row(lv_obj_t* p, int y, lv_color_t col, const char* name,
                       lv_obj_t** v, lv_obj_t** pct) {
    lv_obj_t* dot = lv_obj_create(p);
    lv_obj_set_pos(dot, 0, y+3); lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_bg_color(dot, col, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_radius(dot, 2, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ln = lv_label_create(p);
    lv_obj_set_pos(ln, 12, y);
    lv_obj_set_style_text_font(ln, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ln, C_MUTED, 0);
    lv_label_set_text(ln, name);

    *v = lv_label_create(p);
    lv_obj_set_pos(*v, 96, y);
    lv_obj_set_style_text_font(*v, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(*v, C_WHITE, 0);
    lv_label_set_text(*v, "--");

    *pct = lv_label_create(p);
    lv_obj_set_pos(*pct, 170, y);
    lv_obj_set_style_text_font(*pct, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(*pct, col, 0);
    lv_label_set_text(*pct, "--%");
}

// ═════════════════════════════════════════════════════════════════════════
// Inicialización
// ═════════════════════════════════════════════════════════════════════════
void summary_screen_init(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, C_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // ── Barra de navegación ───────────────────────────────────────────────
    s_btn_prev = nav_btn(parent, 2,   LV_SYMBOL_LEFT,  prev_cb);
    s_btn_next = nav_btn(parent, 440, LV_SYMBOL_RIGHT, next_cb);
    lv_obj_add_state(s_btn_next, LV_STATE_DISABLED);

    s_lbl_period = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_period, 42, 7);
    lv_obj_set_width(s_lbl_period, 396);
    lv_obj_set_style_text_align(s_lbl_period, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_lbl_period, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_period, C_WHITE, 0);
    lv_label_set_text(s_lbl_period, "Esta semana");

    // ── 7 filas fijas: se crean UNA VEZ y se reusan ───────────────────────
    for (int i = 0; i < 7; i++) {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_pos(row, 0, NAV_H + i * ROW_H);
        lv_obj_set_size(row, 480, ROW_H - 1);
        lv_obj_set_style_bg_color(row, C_BG, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x161B22), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0,        LV_PART_MAIN);
        lv_obj_set_style_border_color(row, C_SEP,    LV_PART_MAIN);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 1,        LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        // Draw callback apunta a s_rdd[i] — estático, siempre válido
        lv_obj_add_event_cb(row, row_draw_cb,  LV_EVENT_DRAW_MAIN,
                             &s_rdd[i]);
        lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED,
                             (void*)(intptr_t)i);

        // Etiqueta del día
        lv_obj_t* lbl = lv_label_create(row);
        lv_obj_set_pos(lbl, 2, (ROW_H - 14) / 2);
        lv_obj_set_width(lbl, DAY_W - 2);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, C_MUTED, 0);
        lv_label_set_text(lbl, "---");

        s_rows[i]       = row;
        s_row_labels[i] = lbl;
    }

    // ── Leyenda (igual que antes) ─────────────────────────────────────────
    struct LegItem { const char* name; lv_color_t col; int x; int dy; };
    LegItem legs[] = {
        {"PV",         C_CON_PV,   4,   0},
        {"Descarga",   C_CON_DIS,  50,  0},
        {"Import.",    C_CON_IMP,  130, 0},
        {"Autocon.",   C_PRO_LOD,  210, 0},
        {"Carga bat.", C_PRO_CHG,  286, 0},
        {"Export.",    C_PRO_EXP,  384, 0},
    };
    int leg_y0 = NAV_H + CONT_H + 1;
    for (auto& lg : legs) {
        lv_obj_t* dot = lv_obj_create(parent);
        lv_obj_set_pos(dot, lg.x, leg_y0 + lg.dy + 2);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_bg_color(dot, lg.col, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_radius(dot, 2, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(parent);
        lv_obj_set_pos(lbl, lg.x + 11, leg_y0 + lg.dy);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, C_MUTED, 0);
        lv_label_set_text(lbl, lg.name);
    }

    // ── Popup (igual que antes) ───────────────────────────────────────────
    s_popup = lv_obj_create(parent);
    lv_obj_set_pos(s_popup, (480-220)/2, (272-200)/2);
    lv_obj_set_size(s_popup, 220, 200);
    lv_obj_set_style_bg_color(s_popup, lv_color_hex(0x1C2128), 0);
    lv_obj_set_style_bg_opa(s_popup, 247, 0); // LV_OPA_97
    lv_obj_set_style_border_color(s_popup, lv_color_hex(0x30363D), 0);
    lv_obj_set_style_border_width(s_popup, 1, 0);
    lv_obj_set_style_radius(s_popup, 8, 0);
    lv_obj_set_style_pad_all(s_popup, 8, 0);
    lv_obj_set_scrollbar_mode(s_popup, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_popup, popup_close_cb, LV_EVENT_CLICKED, nullptr);

    s_popup_title = lv_label_create(s_popup);
    lv_obj_set_pos(s_popup_title, 0, 0);
    lv_obj_set_width(s_popup_title, 204);
    lv_obj_set_style_text_font(s_popup_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_popup_title, C_WHITE, 0);
    lv_label_set_text(s_popup_title, "");

    s_popup_con_total = lv_label_create(s_popup);
    lv_obj_set_pos(s_popup_con_total, 0, 20);
    lv_obj_set_style_text_font(s_popup_con_total, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_popup_con_total, C_MUTED, 0);
    lv_label_set_text(s_popup_con_total, "Consumo --");

    const char* cn[3] = {"Solar dir.", "Descarga", "Import."};
    for (int i = 0; i < 3; i++)
        popup_row(s_popup, 36 + i*18, s_con_col[i], cn[i],
                  &s_popup_con_vals[i], &s_popup_con_pcts[i]);

    s_popup_pro_total = lv_label_create(s_popup);
    lv_obj_set_pos(s_popup_pro_total, 0, 96);
    lv_obj_set_style_text_font(s_popup_pro_total, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_popup_pro_total, C_MUTED, 0);
    lv_label_set_text(s_popup_pro_total, "Produccion --");

    const char* pn[3] = {"Autocon.", "Carga bat.", "Export."};
    for (int i = 0; i < 3; i++)
        popup_row(s_popup, 112 + i*18, s_pro_col[i], pn[i],
                  &s_popup_pro_vals[i], &s_popup_pro_pcts[i]);

    lv_obj_t* hint = lv_label_create(s_popup);
    lv_obj_set_pos(hint, 0, 172);
    lv_obj_set_width(hint, 204);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, C_MUTED, 0);
    lv_label_set_text(hint, "Toca para cerrar");
}

// ═════════════════════════════════════════════════════════════════════════
// API pública
// ═════════════════════════════════════════════════════════════════════════
void summary_screen_set_active(bool active) {
    if (active) s_needs_reload = true;
    else        lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
}

void summary_screen_tick() {
    if (!s_needs_reload) return;
    s_needs_reload = false;
    load_and_render();
}

void summary_screen_set_live(const DailyStats& d) {
    s_live_daily  = d;
    s_live_valid  = d.valid;
    // Si estamos en la semana actual, refrescar la fila de hoy
    if (s_offset == 0) s_needs_reload = true;
}
