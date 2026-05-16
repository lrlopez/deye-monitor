#include <time.h>
#include "chart_screen.h"
#include "data_store.h"
#include "storage.h"
#include "ui_constants.h"
#include "config.h"
#include "psram_cache.h"

// ── Paleta ────────────────────────────────────────────────────────────────
#define C_BG    lv_color_hex(0x0D1117)
#define C_CARD  lv_color_hex(0x161B22)
#define C_MUTED lv_color_hex(0x6E7681)
#define C_WHITE lv_color_hex(0xEAEAEA)
#define C_PV    lv_color_hex(0xF5C518)
#define C_GRID  lv_color_hex(0x4A9EFF)
#define C_BATT  lv_color_hex(0x2ECC71)
#define C_LOAD  lv_color_hex(0xBB6BD9)
#define C_SOC   lv_color_hex(0x1A56DB)
#define C_GBTN  lv_color_hex(0x21262D)
#define C_GRID_LINE lv_color_hex(0x21262D)

// ── Estado ────────────────────────────────────────────────────────────────
static int      s_offset      = 0;    // 0=hoy, -1=ayer, …, -6
static bool     s_active      = false;
static uint32_t s_last_tick   = 0;
static bool s_chart_updating = false;

// s_day_recs ya no existe: leemos directo desde la cache PSRAM
// s_hours también viene de la cache
static HourAgg  s_hours[24];   // copia local para acceso sin mutex
static uint32_t s_day_epoch_loaded = 0;

// ── Widgets ───────────────────────────────────────────────────────────────
static lv_obj_t           *s_lbl_date;
static lv_obj_t           *s_btn_prev, *s_btn_next;
static lv_obj_t           *s_chart_pwr, *s_chart_soc;
static lv_chart_series_t  *s_spv, *s_sgrid, *s_sbatt, *s_sload, *s_ssoc;
static lv_obj_t           *s_ylabels_container = nullptr;
static lv_obj_t           *s_no_data;

// Popup y línea vertical
static lv_obj_t           *s_popup       = nullptr;
static lv_obj_t           *s_vline       = nullptr;  // línea vertical
static int32_t             s_selected_h  = -1;       // hora seleccionada (-1 = ninguna)

static lv_obj_t           *s_popup_vals[5];   // labels de valor de cada serie
static lv_obj_t           *s_popup_title;     // label del título

// ── Helpers de tiempo ─────────────────────────────────────────────────────
static uint32_t day_epoch_from_offset(int offset) {
    time_t now; time(&now);
    now += (time_t)offset * 86400;
    struct tm t; localtime_r(&now, &t);
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
    return (uint32_t)mktime(&t);
}

static const char* MESES[] = {
    "Enero","Febrero","Marzo","Abril","Mayo","Junio",
    "Julio","Agosto","Septiembre","Octubre","Noviembre","Diciembre"
};
static const char* DIASEM[] = {
    "Domingo","Lunes","Martes","Miercoles","Jueves","Viernes","Sabado"
};

static void update_date_label() {
    time_t t = (time_t)day_epoch_from_offset(s_offset);
    struct tm ti; localtime_r(&t, &ti);

    char buf[48];
    if      (s_offset ==  0) snprintf(buf,sizeof(buf),"Hoy, %d de %s",   ti.tm_mday, MESES[ti.tm_mon]);
    else if (s_offset == -1) snprintf(buf,sizeof(buf),"Ayer, %d de %s",  ti.tm_mday, MESES[ti.tm_mon]);
    else                     snprintf(buf,sizeof(buf),"%s, %d de %s",
                                 DIASEM[ti.tm_wday], ti.tm_mday, MESES[ti.tm_mon]);
    lv_label_set_text(s_lbl_date, buf);

    if (s_offset >= 0)  lv_obj_add_state(s_btn_next, LV_STATE_DISABLED);
    else                lv_obj_remove_state(s_btn_next, LV_STATE_DISABLED);
    if (s_offset <= -6) lv_obj_add_state(s_btn_prev, LV_STATE_DISABLED);
    else                lv_obj_remove_state(s_btn_prev, LV_STATE_DISABLED);

    lv_obj_set_style_text_color(s_lbl_date,
        s_offset == 0 ? C_WHITE : lv_color_hex(0x4A9EFF), 0);
}

// Devuelve la coordenada X absoluta (en pantalla) del punto de hora h
// dentro del área de contenido del chart.
static int hour_to_screen_x(int h) {
    return CHART_HOUR_X(h);
}

// ── Etiquetas eje Y izquierda ─────────────────────────────────────────────
// Dibuja N etiquetas equidistantes en el margen izquierdo del chart.
// y_lo / y_hi en las mismas unidades que lv_chart_set_range.
// unit: cadena añadida al valor (ej. "k" para kWh, "%" para SOC)
static void add_y_labels(lv_obj_t* parent, int chart_y, int chart_h,
                          int y_lo, int y_hi, int steps, const char* unit) {
    for (int i = 0; i <= steps; i++) {
        // valor en unidades del chart
        int val = y_lo + (y_hi - y_lo) * i / steps;

        // posición Y en pantalla: top = y_hi, bottom = y_lo
        int py = chart_y + CH_PAD_TV
               + (int)((float)(steps - i) / steps * (chart_h - 2 * CH_PAD_TV))
               - FONT_SMALL_SIZE / 2;

        char buf[12];
        // Para potencias (unidad "k"): mostrar como entero de kW
        if (strcmp(unit, "k") == 0)
            snprintf(buf, sizeof(buf), "%dk", val / 1000);
        else
            snprintf(buf, sizeof(buf), "%d%s", val, unit);

        lv_obj_t* lbl = lv_label_create(parent);
        lv_obj_set_pos(lbl, 0, py);
        lv_obj_set_width(lbl, CH_PAD_L - 2);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(lbl, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl, C_MUTED, 0);
        lv_label_set_text(lbl, buf);
    }
}

// ── Rango del chart de potencias ──────────────────────────────────────────
static void apply_range() {
    ChartConfig cfg = Storage.loadChartConfig();
    if (cfg.autoscale) {
        int32_t mn = 0, mx = 500;
        for (int h = 0; h < 24; h++) {
            if (!s_hours[h].valid) continue;
            float vals[] = {s_hours[h].pv_w, s_hours[h].grid_w,
                            s_hours[h].batt_w, s_hours[h].load_w};
            for (float v : vals) {
                if ((int32_t)v > mx) mx = (int32_t)v;
                if ((int32_t)v < mn) mn = (int32_t)v;
            }
        }
        int32_t margin = (mx - mn) / 8;
        if (margin < 200) margin = 200;
        lv_chart_set_range(s_chart_pwr, LV_CHART_AXIS_PRIMARY_Y,
                           mn - margin, mx + margin);
        lv_obj_clean(s_ylabels_container);
        add_y_labels(s_ylabels_container, 0, CHART_PWR_H,
                     (int)(mn - margin), (int)(mx + margin), 4, "k");
    } else {
        int32_t m = (int32_t)cfg.max_kw * 1000;
        lv_chart_set_range(s_chart_pwr, LV_CHART_AXIS_PRIMARY_Y, -m, m);
        lv_obj_clean(s_ylabels_container);
        add_y_labels(s_ylabels_container, 0, CHART_PWR_H, (int)-m, (int)m, 4, "k");
    }
}

// ── Rellena los charts con s_day ──────────────────────────────────────────
static void update_charts() {
    s_chart_updating = true;

    int cur_hour = 24;
    if (s_offset == 0) {
        time_t now; time(&now);
        struct tm t; localtime_r(&now, &t);
        cur_hour = t.tm_hour;
    }

    bool has_any = false;
    for (int h = 0; h < 24; h++) {
        const HourAgg& a = s_hours[h];
        bool valid = a.valid && (s_offset < 0 || h <= cur_hour);
        if (!valid) {
            lv_chart_set_value_by_id(s_chart_pwr, s_spv,   h, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_pwr, s_sgrid, h, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_pwr, s_sbatt, h, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_pwr, s_sload, h, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_soc, s_ssoc,  h, LV_CHART_POINT_NONE);
        } else {
            has_any = true;
            // Valores en W (medias del periodo)
            lv_chart_set_value_by_id(s_chart_pwr, s_spv,   h, (int32_t)a.pv_w);
            lv_chart_set_value_by_id(s_chart_pwr, s_sgrid, h, (int32_t)a.grid_w);
            lv_chart_set_value_by_id(s_chart_pwr, s_sbatt, h, (int32_t)a.batt_w);
            lv_chart_set_value_by_id(s_chart_pwr, s_sload, h, (int32_t)a.load_w);
            lv_chart_set_value_by_id(s_chart_soc, s_ssoc,  h, (int32_t)a.soc);
        }
    }

    if (has_any) {
        lv_obj_add_flag(s_no_data, LV_OBJ_FLAG_HIDDEN);
        apply_range();
    } else {
        lv_obj_remove_flag(s_no_data, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    s_chart_updating = false;
}

static void load_day() {
    s_selected_h = -1;
    if (s_popup) lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    if (s_vline)  lv_obj_add_flag(s_vline,  LV_OBJ_FLAG_HIDDEN);

    uint32_t dep = day_epoch_from_offset(s_offset);

    // Leer directamente las 24 horas pre-agregadas desde PSRAM
    const HourlyRecord* hr = Cache.getHourly(dep);
    if (hr) {
        // Convertir HourlyRecord → HourAgg para compatibilidad con el resto
        for (int h = 0; h < 24; h++) {
            s_hours[h].valid  = (hr[h].flags & 0x01) && hr[h].sample_count > 0;
            s_hours[h].pv_w   = hr[h].avg_pv_w;
            s_hours[h].grid_w = hr[h].avg_grid_w;
            s_hours[h].batt_w = hr[h].avg_batt_w;
            s_hours[h].load_w = hr[h].avg_load_w;
            s_hours[h].soc    = hr[h].soc_end;
        }
    } else {
        memset(s_hours, 0, sizeof(s_hours));
    }

    update_date_label();
    update_charts();
}

// ── Popup con valores del instante clicado ────────────────────────────────
static void show_popup(int32_t h) {
    if (h < 0 || h >= 24 || !s_hours[h].valid) {
        lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_vline, LV_OBJ_FLAG_HIDDEN);
        s_selected_h = -1;
        return;
    }
    if (h == s_selected_h) {
        lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_vline, LV_OBJ_FLAG_HIDDEN);
        s_selected_h = -1;
        return;
    }
    s_selected_h = h;

    const HourAgg& a = s_hours[h];

    // Línea vertical
    int line_x = hour_to_screen_x((int)h);
    lv_obj_set_pos(s_vline, line_x, CHART_PWR_Y);
    lv_obj_set_size(s_vline, 1, CHART_SOC_Y + CHART_SOC_H - CHART_PWR_Y);
    lv_obj_remove_flag(s_vline, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_vline);

    // Título
    char title[24];
    snprintf(title, sizeof(title), "%02d:00 - %02d:00",
             (int)h, (int)(h + 1) % 24);
    lv_label_set_text(s_popup_title, title);

    // Valores medios W de la hora
    // batt_w: positivo = descargando, negativo = cargando (RAW)
    struct Row { const char* name; float w; bool is_signed; };
    Row rows[5] = {
        {"PV",    a.pv_w,   false},
        {"Red",   a.grid_w, true },
        {"Bat",   a.batt_w, true },
        {"Carga", a.load_w, false},
        {"SOC",   (float)a.soc, false},
    };
    for (int i = 0; i < 5; i++) {
        char buf[24];
        if (i < 4) {
            if (rows[i].is_signed) snprintf(buf,sizeof(buf),"%+.0f W", rows[i].w);
            else                   snprintf(buf,sizeof(buf),"%.0f W",  rows[i].w);
        } else {
            snprintf(buf, sizeof(buf), "%d%%", (int)rows[i].w);
        }
        lv_label_set_text(s_popup_vals[i], buf);
    }

    // Posición del popup
    int px = line_x + SX(6);
    if (px + POPUP_CHART_W > SCREEN_WIDTH - SX(4))
        px = line_x - POPUP_CHART_W - SX(4);
    if (px < SX(2)) px = SX(2);
    lv_obj_set_pos(s_popup, px, PWR_Y + SY(4));
    lv_obj_set_size(s_popup, POPUP_CHART_W, POPUP_CHART_H);
    lv_obj_remove_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_popup);
}

// ── Callbacks ─────────────────────────────────────────────────────────────
static void prev_cb(lv_event_t*) { if (s_offset > -6) { s_offset--; load_day(); } }
static void next_cb(lv_event_t*) { if (s_offset <  0) { s_offset++; load_day(); } }

static void chart_click_cb(lv_event_t* e) {
    if (s_chart_updating) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    if (lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) return;

    // lv_chart_get_pressed_point necesita el chart correcto
    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* chart  = (target == s_chart_soc) ? s_chart_soc : s_chart_pwr;
    uint32_t  idx    = lv_chart_get_pressed_point(chart);

    if (idx == LV_CHART_POINT_NONE) return;
    show_popup((int32_t)idx);
}

static void popup_click_cb(lv_event_t*) {
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_vline,  LV_OBJ_FLAG_HIDDEN);
    s_selected_h = -1;
}

// ── Helpers de UI ─────────────────────────────────────────────────────────
static lv_obj_t* nav_btn(lv_obj_t* p, int x, int y, int w, int h, const char* txt,
                          lv_event_cb_t cb) {
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

static lv_obj_t* legend_dot(lv_obj_t* p, int x, int y, lv_color_t c, const char* txt) {
    lv_obj_t* dot = lv_obj_create(p);
    lv_obj_set_pos(dot, x, y + 4); lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_bg_color(dot, c, 0); lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, 4, 0);   lv_obj_set_style_border_width(dot, 0, 0);

    lv_obj_t* lbl = lv_label_create(p);
    lv_obj_set_pos(lbl, x + 11, y + 1);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, C_MUTED, 0);
    lv_label_set_text(lbl, txt);
    return lbl;
}

// ── make_chart() — eliminar la línea del axis_tick ────────────────────────
static lv_obj_t* make_chart(lv_obj_t* parent, int y, int h, int y_lo, int y_hi, int hdiv) {
    lv_obj_t* c = lv_chart_create(parent);
    lv_obj_set_pos(c, 0, y); lv_obj_set_size(c, 480, h);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, 24);
    lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, y_lo, y_hi);
    lv_chart_set_div_line_count(c, hdiv, 0);

    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_line_color(c, C_GRID_LINE, LV_PART_MAIN);
    lv_obj_set_style_line_opa(c, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_left(c,   CH_PAD_L, 0);
    lv_obj_set_style_pad_right(c,  CH_PAD_R, 0);
    lv_obj_set_style_pad_top(c,    CH_PAD_TV, 0);
    lv_obj_set_style_pad_bottom(c, CH_PAD_TV, 0);
    lv_obj_set_style_line_width(c, SS(2), LV_PART_ITEMS);
    lv_obj_set_style_width(c,  0, LV_PART_INDICATOR);
    lv_obj_set_style_height(c, 0, LV_PART_INDICATOR);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    // ── Línea de cero destacada ───────────────────────────────────────────
    // Se dibuja como un objeto separado sobre el chart
    // (solo relevante para el chart de potencias; el de SOC no tiene negativos)
    return c;
}

// ── Inicialización ────────────────────────────────────────────────────────
void chart_screen_init(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, C_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    // ── Barra de navegación ───────────────────────────────────────────────
    s_btn_prev = nav_btn(parent, SX(2), SY(2), NAV_BTN_W, NAV_BTN_H,
                          LV_SYMBOL_LEFT, prev_cb);
    s_btn_next = nav_btn(parent, SCREEN_WIDTH - NAV_BTN_W - SX(2), SY(2),
                          NAV_BTN_W, NAV_BTN_H, LV_SYMBOL_RIGHT, next_cb);

    s_lbl_date = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_date, NAV_BTN_W + SX(6), SY(7));
    lv_obj_set_width(s_lbl_date, SCREEN_WIDTH - 2*(NAV_BTN_W + SX(6)));
    lv_obj_set_style_text_align(s_lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_lbl_date, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(s_lbl_date, C_WHITE, 0);
    lv_label_set_text(s_lbl_date, "---");
    lv_obj_add_flag(s_lbl_date, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lbl_date, [](lv_event_t*) {
        if (s_offset == 0) return;
        s_offset = 0;
        load_day();
    }, LV_EVENT_CLICKED, nullptr);

    // ── Chart de potencias ────────────────────────────────────────────────
    s_chart_pwr = make_chart(parent, PWR_Y, PWR_H, -6000, 6000, 5);
    s_ylabels_container = lv_obj_create(parent);
    lv_obj_set_pos(s_ylabels_container, 0, PWR_Y);
    lv_obj_set_size(s_ylabels_container, CH_PAD_L, PWR_H);
    lv_obj_set_style_bg_opa(s_ylabels_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ylabels_container, 0, 0);
    lv_obj_set_style_pad_all(s_ylabels_container, 0, 0);         
    lv_obj_set_style_clip_corner(s_ylabels_container, false, 0); 
    lv_obj_set_scrollbar_mode(s_ylabels_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s_ylabels_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ylabels_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    add_y_labels(s_ylabels_container, 0, PWR_H, -6000, 6000, 4, YAXIS_UNIT_PWR);
 
    s_sload = lv_chart_add_series(s_chart_pwr, C_LOAD, LV_CHART_AXIS_PRIMARY_Y);
    s_sbatt = lv_chart_add_series(s_chart_pwr, C_BATT, LV_CHART_AXIS_PRIMARY_Y);
    s_sgrid = lv_chart_add_series(s_chart_pwr, C_GRID, LV_CHART_AXIS_PRIMARY_Y);
    s_spv   = lv_chart_add_series(s_chart_pwr, C_PV,   LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_add_flag(s_chart_pwr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_chart_pwr, chart_click_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_remove_flag(s_chart_pwr, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // ── Leyenda ───────────────────────────────────────────────────────────
    struct LegDef { int x; lv_color_t c; const char* txt; };
    LegDef ldefs[] = {
        {SX(5),  C_PV,   "PV"},
        {SX(65), C_GRID, "Red"},
        {SX(120),C_BATT, "Bat"},
        {SX(170),C_LOAD, "Carga"},
        {SX(240),C_SOC,  "SOC"},
    };
    for (auto& ld : ldefs) {
        legend_dot(parent, ld.x, LEG_Y, ld.c, ld.txt);
    }

    lv_obj_t* u = lv_label_create(parent);
    lv_obj_set_pos(u, SCREEN_WIDTH - SX(90), LEG_Y + SY(1));
    lv_obj_set_style_text_color(u, C_MUTED, 0);
    lv_obj_set_style_text_font(u, &FONT_SMALL, 0);
    lv_label_set_text(u, "W | %");

    // ── Chart SOC ─────────────────────────────────────────────────────────
    s_chart_soc = make_chart(parent, SOC_Y, SOC_H, 0, 100, 2);
    add_y_labels(parent, SOC_Y, SOC_H, 0, 100, 2, YAXIS_UNIT_SOC);
    s_ssoc = lv_chart_add_series(s_chart_soc, C_SOC, LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_add_flag(s_chart_soc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_chart_soc, chart_click_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_remove_flag(s_chart_soc, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // ── Etiquetas eje X ───────────────────────────────────────────────────
    const struct { int h; const char* s; } xlabs[] = {
        {0,"00"},{6,"06"},{12,"12"},{18,"18"},{21,"21"}
    };
    for (auto& xl : xlabs) {
        int px = CHART_HOUR_X(xl.h) - SX(8);
        lv_obj_t* l = lv_label_create(parent);
        lv_obj_set_pos(l, px, XLAB_Y);
        lv_obj_set_style_text_font(l, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(l, C_MUTED, 0);
        lv_label_set_text(l, xl.s);
    }

    // ── Sin datos ─────────────────────────────────────────────────────────
    s_no_data = lv_label_create(parent);
    lv_obj_set_pos(s_no_data, 0, PWR_Y + PWR_H/2 - FONT_NORMAL_SIZE/2);
    lv_obj_set_width(s_no_data, SCREEN_WIDTH);
    lv_obj_set_style_text_align(s_no_data, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_no_data, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(s_no_data, C_MUTED, 0);
    lv_label_set_text(s_no_data, "Sin datos para este dia");

    // ── Línea vertical ────────────────────────────────────────────────────
    s_vline = lv_obj_create(parent);
    lv_obj_set_style_bg_color(s_vline, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_vline, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_vline, 0, 0);
    lv_obj_set_style_radius(s_vline, 0, 0);
    lv_obj_remove_flag(s_vline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_vline, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_vline, LV_OBJ_FLAG_HIDDEN);

    // ── Popup ─────────────────────────────────────────────────────────────
    s_popup = lv_obj_create(parent);
    lv_obj_set_style_bg_color(s_popup, lv_color_hex(0x1C2128), 0);
    lv_obj_set_style_bg_opa(s_popup, 247, 0);
    lv_obj_set_style_border_color(s_popup, lv_color_hex(0x30363D), 0);
    lv_obj_set_style_border_width(s_popup, UI_BORDER, 0);
    lv_obj_set_style_radius(s_popup, SS(6), 0);
    lv_obj_set_style_pad_all(s_popup, POPUP_PAD, 0);
    lv_obj_set_scrollbar_mode(s_popup, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_popup, popup_click_cb, LV_EVENT_CLICKED, nullptr);

    s_popup_title = lv_label_create(s_popup);
    lv_obj_set_pos(s_popup_title, 0, 0);
    lv_obj_set_width(s_popup_title, POPUP_CHART_W - POPUP_PAD*2);
    lv_obj_set_style_text_font(s_popup_title, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_popup_title, C_MUTED, 0);
    lv_label_set_text(s_popup_title, "");

    struct PopupRow { const char* name; lv_color_t color; };
    PopupRow prows[5] = {
        {"PV",    C_PV  }, {"Red",  C_GRID},
        {"Bat",   C_BATT}, {"Carga",C_LOAD}, {"SOC", C_SOC}
    };
    for (int i = 0; i < 5; i++) {
        int ry = FONT_SMALL_SIZE + SY(4) + i * POPUP_ROW_H;

        lv_obj_t* dot = lv_obj_create(s_popup);
        lv_obj_set_pos(dot, 0, ry + SY(3));
        lv_obj_set_size(dot, UI_DOT_SZ, UI_DOT_SZ);
        lv_obj_set_style_bg_color(dot, prows[i].color, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_radius(dot, SS(2), 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl_name = lv_label_create(s_popup);
        lv_obj_set_pos(lbl_name, UI_DOT_SZ + SX(4), ry);
        lv_obj_set_style_text_font(lbl_name, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_name, C_MUTED, 0);
        lv_label_set_text(lbl_name, prows[i].name);

        s_popup_vals[i] = lv_label_create(s_popup);
        lv_obj_set_pos(s_popup_vals[i], SX(54), ry);
        lv_obj_set_style_text_font(s_popup_vals[i], &FONT_SMALL, 0);
        lv_obj_set_style_text_color(s_popup_vals[i], prows[i].color, 0);
        lv_label_set_text(s_popup_vals[i], "--");
    }
}

// ── API pública ───────────────────────────────────────────────────────────
void chart_screen_set_active(bool active) {
    s_active = active;
    if (active) { load_day(); s_last_tick = millis(); }
}

void chart_screen_tick() {
    if (!s_active || s_offset != 0) return;
    if (millis() - s_last_tick >= 300000UL) {   // refrescar hoy cada 5 min
        load_day();
        s_last_tick = millis();
    }
}
