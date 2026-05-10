#include "chart_screen.h"
#include "storage.h"
#include <time.h>

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

// ── Layout 480×272 ────────────────────────────────────────────────────────
#define NAV_H  28
#define PWR_Y  NAV_H
#define PWR_H  138
#define LEG_Y  (PWR_Y + PWR_H)      // 166
#define LEG_H  16
#define SOC_Y  (LEG_Y + LEG_H)      // 182
#define SOC_H  54
#define XLAB_Y (SOC_Y + SOC_H + 2)  // 238

// Padding horizontal idéntico en ambos charts → x-labels alineadas
#define CH_PAD_L  38
#define CH_PAD_R   4
#define CH_PAD_TV  4

// ── Estado ────────────────────────────────────────────────────────────────
static DayData  s_day;
static int      s_offset      = 0;    // 0=hoy, -1=ayer, …, -6
static bool     s_active      = false;
static uint32_t s_last_tick   = 0;
static bool s_chart_updating = false;

// ── Widgets ───────────────────────────────────────────────────────────────
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
}

// Devuelve la coordenada X absoluta (en pantalla) del punto de hora h
// dentro del área de contenido del chart.
static int hour_to_screen_x(int h) {
    const int content_w = 480 - CH_PAD_L - CH_PAD_R;
    return CH_PAD_L + (int)roundf((float)h * content_w / 23.0f);
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
               - 6;   // -6 para centrar el label de 12px

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
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
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
            if (!s_day.hours[h].valid) continue;
            const HourlyRecord& r = s_day.hours[h];
            int32_t vals[] = {
                (int32_t)r.pv_wh,
                (int32_t)r.import_wh,
                -(int32_t)r.export_wh,
                (int32_t)r.batt_charge_wh - r.batt_discharge_wh,
                (int32_t)r.load_wh,
            };
            for (auto v : vals) { if (v > mx) mx = v; if (v < mn) mn = v; }
        }
        int32_t margin = (mx - mn) / 8;
        if (margin < 200) margin = 200;
        lv_chart_set_range(s_chart_pwr, LV_CHART_AXIS_PRIMARY_Y, mn - margin, mx + margin);
        lv_chart_set_range(s_chart_pwr, LV_CHART_AXIS_PRIMARY_Y, mn, mx);

        // Regenerar etiquetas Y con el nuevo rango
        lv_obj_clean(s_ylabels_container);
        add_y_labels(s_ylabels_container, 0, PWR_H, (int)mn, (int)mx, 4, "k");
    } else {
        int32_t m = (int32_t)cfg.max_kw * 1000;
        lv_chart_set_range(s_chart_pwr, LV_CHART_AXIS_PRIMARY_Y, -m, m);
    }
}

// ── Rellena los charts con s_day ──────────────────────────────────────────
static void update_charts() {
    // Si mostramos hoy, no dibujar horas futuras
    int cur_hour = 24;
    if (s_offset == 0) {
        time_t now; time(&now);
        struct tm t; localtime_r(&now, &t);
        cur_hour = t.tm_hour;
    }

    s_chart_updating = true;
    bool has_any = false;
    for (int h = 0; h < 24; h++) {
        const HourlyRecord& r = s_day.hours[h];
        bool valid = r.valid && (s_offset < 0 || h < cur_hour);

        if (!valid) {
            lv_chart_set_value_by_id(s_chart_pwr, s_spv,   h, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_pwr, s_sgrid, h, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_pwr, s_sbatt, h, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_pwr, s_sload, h, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_soc, s_ssoc,  h, LV_CHART_POINT_NONE);
        } else {
            has_any = true;
            lv_chart_set_value_by_id(s_chart_pwr, s_spv,   h, (int32_t)r.pv_wh);
            lv_chart_set_value_by_id(s_chart_pwr, s_sgrid, h, (int32_t)r.import_wh - r.export_wh);
            lv_chart_set_value_by_id(s_chart_pwr, s_sbatt, h, (int32_t)r.batt_charge_wh - r.batt_discharge_wh);
            lv_chart_set_value_by_id(s_chart_pwr, s_sload, h, (int32_t)r.load_wh);
            lv_chart_set_value_by_id(s_chart_soc, s_ssoc,  h, (int32_t)r.soc);
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
    // Resetear selección al cambiar de día
    s_selected_h = -1;
    if (s_popup) lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    if (s_vline)  lv_obj_add_flag(s_vline,  LV_OBJ_FLAG_HIDDEN);

    uint32_t dep = day_epoch_from_offset(s_offset);
    if (!Storage.getDayData(dep, s_day)) {
        s_day = DayData{};
        s_day.day_epoch = dep;
    }
    update_date_label();
    update_charts();
}

// ── Popup con valores del instante clicado ────────────────────────────────
static void show_popup(int32_t h) {
    if (h < 0 || h >= 24 || !s_day.hours[h].valid) {
        // Ocultar todo si la hora no tiene datos
        if (s_popup) lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
        if (s_vline)  lv_obj_add_flag(s_vline,  LV_OBJ_FLAG_HIDDEN);
        s_selected_h = -1;
        return;
    }

    // Si se pulsa la misma hora, actúa como toggle
    if (h == s_selected_h) {
        lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_vline,  LV_OBJ_FLAG_HIDDEN);
        s_selected_h = -1;
        return;
    }
    s_selected_h = h;

    const HourlyRecord& r = s_day.hours[h];
    int net_grid = (int)r.import_wh  - (int)r.export_wh;
    int net_batt = (int)r.batt_charge_wh - (int)r.batt_discharge_wh;

    // ── Línea vertical ────────────────────────────────────────────────────
    int line_x = hour_to_screen_x((int)h);
    // La línea cubre chart_pwr + leyenda + chart_soc
    lv_obj_set_pos(s_vline, line_x, PWR_Y);
    lv_obj_set_size(s_vline, 1, SOC_Y + SOC_H - PWR_Y);
    lv_obj_remove_flag(s_vline, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_vline);

    // ── Contenido del popup ───────────────────────────────────────────────
    // Fila 0: título con hora
    // Filas 1-4: serie con cuadro color + nombre + valor
    // Fila 5: SOC

    // Título
    char title_buf[24];
    snprintf(title_buf, sizeof(title_buf), "%02d:00 - %02d:00",
             (int)h, (int)(h + 1) % 24);
    lv_label_set_text(s_popup_title, title_buf);

    // Valores de las 4 series + SOC
    struct Row { const char* name; lv_color_t color; int wh; bool is_signed; };
    Row rows[5] = {
        { "PV",    C_PV,   (int)r.pv_wh,  false },
        { "Red",   C_GRID, net_grid,       true  },
        { "Bat",   C_BATT, net_batt,       true  },
        { "Carga", C_LOAD, (int)r.load_wh, false },
        { "SOC",   C_SOC,  (int)r.soc,     false },
    };

    // Los hijos 1–5 son los labels de valor (creados en init)
    for (int i = 0; i < 5; i++) {
        char buf[32];
        if (i < 4) {   // potencias en kWh
            if (rows[i].is_signed)
                snprintf(buf, sizeof(buf), "%+.2f kWh", rows[i].wh / 1000.0f);
            else
                snprintf(buf, sizeof(buf), "%.2f kWh",  rows[i].wh / 1000.0f);
        } else {       // SOC en %
            snprintf(buf, sizeof(buf), "%d%%", rows[i].wh);
        }
        lv_label_set_text(s_popup_vals[i], buf);
    }

    // ── Posición del popup: izquierda o derecha según espacio ─────────────
    const int POPUP_W = 168;
    const int POPUP_H = 112;
    int px = line_x + 6;
    if (px + POPUP_W > 476) px = line_x - POPUP_W - 4;
    if (px < 2)             px = 2;
    lv_obj_set_pos(s_popup, px, PWR_Y + 4);
    lv_obj_set_size(s_popup, POPUP_W, POPUP_H);

    lv_obj_remove_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_popup);
}

// ── Callbacks ─────────────────────────────────────────────────────────────
static void prev_cb(lv_event_t*) { if (s_offset > -6) { s_offset--; load_day(); } }
static void next_cb(lv_event_t*) { if (s_offset <  0) { s_offset++; load_day(); } }

static void chart_click_cb(lv_event_t* e) {
    if (s_chart_updating) return;

    // Verificar que hay un toque real activo
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;   // evento disparado por refresco de datos, ignorar

    // Verificar además que el indev tiene estado PRESSED
    // (descarta eventos residuales tras soltar)
    lv_indev_state_t state = lv_indev_get_state(indev);
    if (state != LV_INDEV_STATE_PRESSED) return;

    // Detectar qué chart fue pulsado y obtener el índice correspondiente
    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
    uint32_t idx;

    if (target == s_chart_pwr) {
        idx = lv_chart_get_pressed_point(s_chart_pwr);
    } else {
        idx = lv_chart_get_pressed_point(s_chart_soc);
    }

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
    lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
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
    s_btn_prev = nav_btn(parent, 2,   2, 44, NAV_H - 4, LV_SYMBOL_LEFT,  prev_cb);
    s_btn_next = nav_btn(parent, 434, 2, 44, NAV_H - 4, LV_SYMBOL_RIGHT, next_cb);

    s_lbl_date = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_date, 48, 7); lv_obj_set_width(s_lbl_date, 384);
    lv_obj_set_style_text_align(s_lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_lbl_date,  &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_date, C_WHITE, 0);
    lv_label_set_text(s_lbl_date, "---");

    // ── Chart de potencias ────────────────────────────────────────────────
    s_ylabels_container = lv_obj_create(parent);
    lv_obj_set_pos(s_ylabels_container, 0, PWR_Y);
    lv_obj_set_size(s_ylabels_container, CH_PAD_L, PWR_H);
    lv_obj_set_style_bg_opa(s_ylabels_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ylabels_container, 0, 0);
    lv_obj_set_scrollbar_mode(s_ylabels_container, LV_SCROLLBAR_MODE_OFF);

    s_chart_pwr = make_chart(parent, PWR_Y, PWR_H, -6000, 6000, 5);    
    lv_obj_add_flag(s_chart_pwr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_chart_pwr, chart_click_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_remove_flag(s_chart_pwr, LV_OBJ_FLAG_GESTURE_BUBBLE);
    add_y_labels(parent, PWR_Y, PWR_H, -6000, 6000, 4, "k");

    // Series (la última en añadirse dibuja encima)
    s_sload = lv_chart_add_series(s_chart_pwr, C_LOAD, LV_CHART_AXIS_PRIMARY_Y);
    s_sbatt = lv_chart_add_series(s_chart_pwr, C_BATT, LV_CHART_AXIS_PRIMARY_Y);
    s_sgrid = lv_chart_add_series(s_chart_pwr, C_GRID, LV_CHART_AXIS_PRIMARY_Y);
    s_spv   = lv_chart_add_series(s_chart_pwr, C_PV,   LV_CHART_AXIS_PRIMARY_Y);

    // ── Leyenda ───────────────────────────────────────────────────────────
    legend_dot(parent,   5, LEG_Y, C_PV,   "PV");
    legend_dot(parent,  65, LEG_Y, C_GRID, "Red");
    legend_dot(parent, 120, LEG_Y, C_BATT, "Bat");
    legend_dot(parent, 170, LEG_Y, C_LOAD, "Carga");
    legend_dot(parent, 240, LEG_Y, C_SOC,  "SOC");

    lv_obj_t* u = lv_label_create(parent);
    lv_obj_set_pos(u, 390, LEG_Y + 1); lv_obj_set_style_text_color(u, C_MUTED, 0);
    lv_obj_set_style_text_font(u, &lv_font_montserrat_12, 0);
    lv_label_set_text(u, "kWh/h | %");

    // ── Chart de SOC ──────────────────────────────────────────────────────
    s_chart_soc = make_chart(parent, SOC_Y, SOC_H, 0, 100, 2);
    lv_obj_add_flag(s_chart_soc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_chart_soc, chart_click_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_remove_flag(s_chart_soc, LV_OBJ_FLAG_GESTURE_BUBBLE);
    add_y_labels(parent, SOC_Y, SOC_H, 0, 100, 2, "%");
    s_ssoc = lv_chart_add_series(s_chart_soc, C_SOC, LV_CHART_AXIS_PRIMARY_Y);

    // ── Etiquetas eje X ───────────────────────────────────────────────────
    // content_w = 480 - CH_PAD_L - CH_PAD_R = 438
    // px(h) = CH_PAD_L + round(h * 438 / 23)
    const int cw = 480 - CH_PAD_L - CH_PAD_R;
    const struct { int h; const char* s; } xlabs[] = {
        {0,"00"}, {6,"06"}, {12,"12"}, {18,"18"}, {21,"21"}
    };
    for (auto& xl : xlabs) {
        int px = CH_PAD_L + (int)(xl.h * cw / 23.0f + 0.5f) - 8;
        lv_obj_t* l = lv_label_create(parent);
        lv_obj_set_pos(l, px, XLAB_Y);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, C_MUTED, 0);
        lv_label_set_text(l, xl.s);
    }

    // ── "Sin datos" ───────────────────────────────────────────────────────
    s_no_data = lv_label_create(parent);
    lv_obj_set_pos(s_no_data, 0, PWR_Y + PWR_H / 2 - 8);
    lv_obj_set_width(s_no_data, 480);
    lv_obj_set_style_text_align(s_no_data, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_no_data, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_no_data, C_MUTED, 0);
    lv_label_set_text(s_no_data, "Sin datos para este dia");

    // ── Popup de detalle ──────────────────────────────────────────────────
// ── Línea vertical de selección ───────────────────────────────────────
    s_vline = lv_obj_create(parent);
    lv_obj_set_style_bg_color(s_vline, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_vline, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_vline, 0, 0);
    lv_obj_set_style_radius(s_vline, 0, 0);
    lv_obj_remove_flag(s_vline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_vline, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_vline, LV_OBJ_FLAG_HIDDEN);

    // ── Popup de detalle ──────────────────────────────────────────────────
    // Estructura fija: hijo 0 = título, hijos 1-5 = filas de datos
    s_popup = lv_obj_create(parent);
    lv_obj_set_style_bg_color(s_popup, lv_color_hex(0x1C2128), 0);
    lv_obj_set_style_bg_opa(s_popup, 248, 0); // LV_OPA_97
    lv_obj_set_style_border_color(s_popup, lv_color_hex(0x30363D), 0);
    lv_obj_set_style_border_width(s_popup, 1, 0);
    lv_obj_set_style_radius(s_popup, 6, 0);
    lv_obj_set_style_pad_all(s_popup, 6, 0);
    lv_obj_set_style_pad_row(s_popup, 0, 0);
    lv_obj_set_scrollbar_mode(s_popup, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_popup, popup_click_cb, LV_EVENT_CLICKED, nullptr);

    // Hijo 0: título (hora)
    lv_obj_t* p_title = lv_label_create(s_popup);
    s_popup_title = p_title;
    lv_obj_set_pos(p_title, 0, 0);
    lv_obj_set_width(p_title, 156);
    lv_obj_set_style_text_font(p_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(p_title, C_MUTED, 0);
    lv_label_set_text(p_title, "");

    // Hijos 1-5: una fila por serie  [■ Nombre   valor]
    struct PopupRow { const char* name; lv_color_t color; };
    PopupRow prows[5] = {
        { "PV",    C_PV   },
        { "Red",   C_GRID },
        { "Bat",   C_BATT },
        { "Carga", C_LOAD },
        { "SOC",   C_SOC  },
    };
    const int ROW_Y0 = 16;
    const int ROW_DY = 18;

    for (int i = 0; i < 5; i++) {
        int ry = ROW_Y0 + i * ROW_DY;

        // Cuadro de color
        lv_obj_t* dot = lv_obj_create(s_popup);
        lv_obj_set_pos(dot, 0, ry + 2);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_bg_color(dot, prows[i].color, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_radius(dot, 2, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

        // Nombre de la serie
        lv_obj_t* lbl_name = lv_label_create(s_popup);
        lv_obj_set_pos(lbl_name, 12, ry);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_name, C_MUTED, 0);
        lv_label_set_text(lbl_name, prows[i].name);

        // Valor (es el hijo indexado en show_popup como hijo 1+i)
        lv_obj_t* lbl_val = lv_label_create(s_popup);
        s_popup_vals[i] = lbl_val;
        lv_obj_set_pos(lbl_val, 54, ry);
        lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_val, prows[i].color, 0);
        lv_label_set_text(lbl_val, "--");
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
