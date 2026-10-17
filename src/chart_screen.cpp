#include <time.h>
#include "time_utils.h"
#include "chart_screen.h"
#include "data_store.h"
#include "storage.h"
#include "ui_constants.h"
#include "config.h"
#include "psram_cache.h"
#include "calendar_popup.h"

// ── Layout: SOC superpuesto al chart de potencias ─────────────────────────
// El área del chart de potencias absorbe el espacio de leyenda + SOC chart.
#define MERGED_H      (CHART_PWR_H + CHART_LEG_H + CHART_SOC_H)
#define MERGED_XLAB_Y (CHART_PWR_Y + MERGED_H)
#define MERGED_LEG_Y  (MERGED_XLAB_Y + FONT_SMALL_SIZE + SY(2))
#define MERGED_PAD_R  CH_PAD_L  // padding derecho simétrico al izquierdo
#define MERGED_CW     (SCREEN_WIDTH - CH_PAD_L - MERGED_PAD_R)
#define CHART_HOUR_X_M(h) (CH_PAD_L + (int)((h) * MERGED_CW / 23.0f + 0.5f))

// ── Paleta ────────────────────────────────────────────────────────────────
#define C_PV    lv_color_hex(0xF5C518)
#define C_GRID  lv_color_hex(0x4A9EFF)
#define C_BATT  lv_color_hex(0x2ECC71)
#define C_LOAD  lv_color_hex(0xBB6BD9)
#define C_SOC   lv_color_hex(0x1A56DB)
#define C_GRID_LINE lv_color_hex(0x21262D)

// ── Rango de tiempo (DEYE-52) ─────────────────────────────────────────────
// Vista "24h": un día (24 puntos, uno por hora). Vista "7d": los 7 días que
// terminan en el día seleccionado (168 puntos). Los valores son medias horarias
// de la caché PSRAM en ambos casos.
#define PTS_DAY   24
#define PTS_WEEK  (7 * 24)
#define RANGE_BTN_W  SX(40)

// ── Estado ────────────────────────────────────────────────────────────────
static int      s_offset      = 0;    // día en que TERMINA el rango: 0=hoy, -1=ayer, …
static bool     s_week        = false;   // false = 24h, true = 7 días
static int      s_npts        = PTS_DAY;
static bool     s_active      = false;
static uint32_t s_last_tick   = -270000UL;
static bool s_chart_updating = false;

// Lectura directa desde la cache PSRAM; copia local para acceso sin mutex
static HourAgg  s_hours[PTS_WEEK];
static uint32_t s_day_epoch_loaded = 0;

// ── Widgets ───────────────────────────────────────────────────────────────
static lv_obj_t           *s_lbl_date;
static lv_obj_t           *s_btn_prev, *s_btn_next, *s_btn_range, *s_lbl_range;
static lv_obj_t           *s_chart_pwr;
static lv_chart_series_t  *s_spv, *s_sgrid, *s_sbatt, *s_sload, *s_ssoc;
static lv_obj_t           *s_ylabels_container = nullptr;
static lv_obj_t           *s_ylabels_right     = nullptr;
static lv_obj_t           *s_xlabels           = nullptr;
static lv_obj_t           *s_no_data;

// Series en el orden de la leyenda: PV, Red, Bat, Carga, SOC. La leyenda es
// tocable (DEYE-52): tocar una entrada oculta/muestra esa serie.
static lv_chart_series_t  *s_ser[5];
static bool                s_hidden[5] = {};

// Popup y línea vertical
static lv_obj_t           *s_popup       = nullptr;
static lv_obj_t           *s_vline       = nullptr;  // línea vertical
static int32_t             s_selected    = -1;       // punto seleccionado (-1 = ninguno)

static lv_obj_t           *s_popup_vals[5];   // labels de valor de cada serie
static lv_obj_t           *s_popup_title;     // label del título

// ── Helpers de tiempo ─────────────────────────────────────────────────────
static uint32_t day_epoch_from_offset(int offset) {
    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
    t.tm_mday += offset;   // mktime normaliza correctamente días negativos y cambio de hora DST
    return (uint32_t)mktime(&t);
}

static const char* MESES[] = {
    "Enero","Febrero","Marzo","Abril","Mayo","Junio",
    "Julio","Agosto","Septiembre","Octubre","Noviembre","Diciembre"
};
static const char* MESES_C[] = {
    "Ene","Feb","Mar","Abr","May","Jun","Jul","Ago","Sep","Oct","Nov","Dic"
};
static const char* DIASEM[] = {
    "Domingo","Lunes","Martes","Miercoles","Jueves","Viernes","Sabado"
};
static const char* DIASEM_C[] = { "Dom","Lun","Mar","Mie","Jue","Vie","Sab" };

// Paso y límite de la navegación: 1 día en vista 24h (como siempre); 7 días en la semanal
static int step_days()   { return s_week ? 7 : 1; }
static int min_offset()  { return s_week ? -364 : -6; }

static void update_date_label() {
    time_t t = (time_t)day_epoch_from_offset(s_offset);
    struct tm ti; localtime_r(&t, &ti);

    char buf[48];
    if (s_week) {
        time_t t0 = (time_t)day_epoch_from_offset(s_offset - 6);
        struct tm t0i; localtime_r(&t0, &t0i);
        if (s_offset == 0) snprintf(buf, sizeof(buf), "Ultima semana");
        else               snprintf(buf, sizeof(buf), "%d %s - %d %s",
                                    t0i.tm_mday, MESES_C[t0i.tm_mon], ti.tm_mday, MESES_C[ti.tm_mon]);
    } else if (s_offset ==  0) snprintf(buf,sizeof(buf),"Hoy, %d de %s",   ti.tm_mday, MESES[ti.tm_mon]);
    else if (s_offset == -1)   snprintf(buf,sizeof(buf),"Ayer, %d de %s",  ti.tm_mday, MESES[ti.tm_mon]);
    else                       snprintf(buf,sizeof(buf),"%s, %d de %s",
                                 DIASEM[ti.tm_wday], ti.tm_mday, MESES[ti.tm_mon]);
    lv_label_set_text(s_lbl_date, buf);

    if (s_offset >= 0)  lv_obj_add_state(s_btn_next, LV_STATE_DISABLED);
    else                lv_obj_remove_state(s_btn_next, LV_STATE_DISABLED);
    if (s_offset - step_days() < min_offset()) lv_obj_add_state(s_btn_prev, LV_STATE_DISABLED);
    else                                       lv_obj_remove_state(s_btn_prev, LV_STATE_DISABLED);

    lv_obj_set_style_text_color(s_lbl_date,
        s_offset == 0 ? C_WHITE : C_ACCENT, 0);

    // Botón de rango: muestra el rango actual y se resalta en la vista semanal
    lv_label_set_text(s_lbl_range, s_week ? "7d" : "24h");
    lv_obj_set_style_bg_color(s_btn_range, s_week ? C_ACCENT : C_GBTN, 0);
}

// X en pantalla del punto idx (0..s_npts-1) dentro del área de contenido del chart
static int idx_to_x(int idx) {
    return CH_PAD_L + (int)((float)idx * MERGED_CW / (s_npts - 1) + 0.5f);
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
        // Para potencias (unidad "k"): entero de kW; con un rango corto (p.ej. al
        // ocultar series y autoescalar) un decimal, o saldrían "0k, 0k, 0k" repetidos
        if (strcmp(unit, "k") == 0) {
            if (y_hi - y_lo < 6000) snprintf(buf, sizeof(buf), "%.1fk", val / 1000.0f);
            else                    snprintf(buf, sizeof(buf), "%dk", val / 1000);
        } else
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

// ── Etiquetas eje X (dependen del rango) ──────────────────────────────────
static void rebuild_xlabels() {
    lv_obj_clean(s_xlabels);
    if (!s_week) {
        const struct { int h; const char* s; } xlabs[] = {
            {0,"00"},{6,"06"},{12,"12"},{18,"18"},{21,"21"}
        };
        for (auto& xl : xlabs) {
            lv_obj_t* l = lv_label_create(s_xlabels);
            lv_obj_set_pos(l, CHART_HOUR_X_M(xl.h) - SX(8), 0);
            lv_obj_set_style_text_font(l, &FONT_SMALL, 0);
            lv_obj_set_style_text_color(l, C_MUTED, 0);
            lv_label_set_text(l, xl.s);
        }
    } else {
        // Un rótulo por día, centrado en su tramo (mediodía)
        for (int d = 0; d < 7; d++) {
            time_t t = (time_t)day_epoch_from_offset(s_offset - 6 + d);
            struct tm ti; localtime_r(&t, &ti);
            const bool is_today = (s_offset - 6 + d == 0);
            lv_obj_t* l = lv_label_create(s_xlabels);
            lv_obj_set_width(l, SX(32));
            lv_obj_set_pos(l, idx_to_x(d * 24 + 12) - SX(16), 0);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_font(l, &FONT_SMALL, 0);
            lv_obj_set_style_text_color(l, is_today ? C_WHITE : C_MUTED, 0);
            lv_label_set_text(l, DIASEM_C[ti.tm_wday]);
        }
    }
}

// ── Rango del chart de potencias ──────────────────────────────────────────
// El autoescalado solo tiene en cuenta las series VISIBLES: ocultar las que no
// interesan (leyenda) amplía de hecho la señal que sí se quiere ver.
static void apply_range() {
    ChartConfig cfg = Storage.loadChartConfig();
    if (cfg.autoscale) {
        int32_t mn = 0, mx = 500;
        for (int i = 0; i < s_npts; i++) {
            if (!s_hours[i].valid) continue;
            float vals[] = {s_hours[i].pv_w, s_hours[i].grid_w,
                            s_hours[i].batt_w, s_hours[i].load_w};
            for (int k = 0; k < 4; k++) {
                if (s_hidden[k]) continue;
                float v = vals[k];
                if ((int32_t)v > mx) mx = (int32_t)v;
                if ((int32_t)v < mn) mn = (int32_t)v;
            }
        }
        int32_t margin = (mx - mn) / 8;
        if (margin < 200) margin = 200;
        lv_chart_set_range(s_chart_pwr, LV_CHART_AXIS_PRIMARY_Y,
                           mn - margin, mx + margin);
        lv_obj_clean(s_ylabels_container);
        add_y_labels(s_ylabels_container, 0, MERGED_H,
                     (int)(mn - margin), (int)(mx + margin), 4, "k");
    } else {
        int32_t m = (int32_t)cfg.max_kw * 1000;
        lv_chart_set_range(s_chart_pwr, LV_CHART_AXIS_PRIMARY_Y, -m, m);
        lv_obj_clean(s_ylabels_container);
        add_y_labels(s_ylabels_container, 0, MERGED_H, (int)-m, (int)m, 4, "k");
    }
}

// ── Rellena el chart con s_hours ──────────────────────────────────────────
static void update_charts() {
    s_chart_updating = true;

    // Último punto con datos posibles: hoy llega hasta la hora actual; días pasados, todos
    int last_idx = s_npts - 1;
    if (s_offset == 0) {
        time_t now; time(&now);
        struct tm t; localtime_r(&now, &t);
        last_idx = (s_npts - 24) + t.tm_hour;
    }

    lv_chart_set_point_count(s_chart_pwr, s_npts);
    lv_chart_set_div_line_count(s_chart_pwr, 5, s_week ? 8 : 0);   // 8 → una línea por cambio de día

    bool has_any = false;
    for (int i = 0; i < s_npts; i++) {
        const HourAgg& a = s_hours[i];
        bool valid = a.valid && i <= last_idx;
        if (!valid) {
            lv_chart_set_value_by_id(s_chart_pwr, s_spv,   i, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_pwr, s_sgrid, i, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_pwr, s_sbatt, i, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_pwr, s_sload, i, LV_CHART_POINT_NONE);
            lv_chart_set_value_by_id(s_chart_pwr, s_ssoc,  i, LV_CHART_POINT_NONE);
        } else {
            has_any = true;
            // Valores en W (medias del periodo)
            lv_chart_set_value_by_id(s_chart_pwr, s_spv,   i, (int32_t)a.pv_w);
            lv_chart_set_value_by_id(s_chart_pwr, s_sgrid, i, (int32_t)a.grid_w);
            lv_chart_set_value_by_id(s_chart_pwr, s_sbatt, i, (int32_t)a.batt_w);
            lv_chart_set_value_by_id(s_chart_pwr, s_sload, i, (int32_t)a.load_w);
            lv_chart_set_value_by_id(s_chart_pwr, s_ssoc,  i, (int32_t)a.soc);
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

static void load_range() {
    s_selected = -1;
    if (s_popup) lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    if (s_vline)  lv_obj_add_flag(s_vline,  LV_OBJ_FLAG_HIDDEN);

    s_npts = s_week ? PTS_WEEK : PTS_DAY;
    s_day_epoch_loaded = day_epoch_from_offset(s_offset);
    memset(s_hours, 0, sizeof(s_hours));

    // Lee directamente las horas pre-agregadas desde PSRAM, un día tras otro
    const int days = s_week ? 7 : 1;
    for (int d = 0; d < days; d++) {
        uint32_t dep = day_epoch_from_offset(s_offset - (days - 1 - d));
        const HourlyRecord* hr = Cache.getHourly(dep);
        if (!hr) continue;
        // Convertir HourlyRecord → HourAgg para compatibilidad con el resto
        for (int h = 0; h < 24; h++) {
            HourAgg& a = s_hours[d * 24 + h];
            a.valid  = (hr[h].flags & 0x01) && hr[h].sample_count > 0;
            a.pv_w   = hr[h].avg_pv_w;
            a.grid_w = hr[h].avg_grid_w;
            a.batt_w = hr[h].avg_batt_w;
            a.load_w = hr[h].avg_load_w;
            a.soc    = hr[h].soc_end;
        }
    }

    update_date_label();
    rebuild_xlabels();
    update_charts();
}

// ── Popup con valores del instante clicado ────────────────────────────────
static void show_popup(int32_t idx) {
    if (idx < 0 || idx >= s_npts || !s_hours[idx].valid) {
        lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_vline, LV_OBJ_FLAG_HIDDEN);
        s_selected = -1;
        return;
    }
    if (idx == s_selected) {
        lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_vline, LV_OBJ_FLAG_HIDDEN);
        s_selected = -1;
        return;
    }
    s_selected = idx;

    const HourAgg& a = s_hours[idx];

    // Línea vertical
    int line_x = idx_to_x((int)idx);
    lv_obj_set_pos(s_vline, line_x, CHART_PWR_Y);
    lv_obj_set_size(s_vline, 1, MERGED_H);
    lv_obj_remove_flag(s_vline, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_vline);

    // Título: "HH:00 - HH:00" en la vista diaria; "Dia dd, HH:00" en la semanal
    const int h = (int)(idx % 24);
    char title[28];
    if (s_week) {
        time_t t = (time_t)day_epoch_from_offset(s_offset - 6 + (int)(idx / 24));
        struct tm ti; localtime_r(&t, &ti);
        snprintf(title, sizeof(title), "%s %d, %02d:00", DIASEM_C[ti.tm_wday], ti.tm_mday, h);
    } else {
        snprintf(title, sizeof(title), "%02d:00 - %02d:00", h, (h + 1) % 24);
    }
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
static void prev_cb(lv_event_t*) {
    if (s_offset - step_days() >= min_offset()) { s_offset -= step_days(); load_range(); }
}
static void next_cb(lv_event_t*) {
    if (s_offset < 0) {
        s_offset += step_days();
        if (s_offset > 0) s_offset = 0;
        load_range();
    }
}
static void range_cb(lv_event_t*) {
    s_week = !s_week;
    // Al pasar a 24h desde una semana lejana, no quedar fuera del límite diario
    if (!s_week && s_offset < min_offset()) s_offset = min_offset();
    load_range();
}

static void chart_click_cb(lv_event_t* e) {
    if (s_chart_updating) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    if (lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) return;

    uint32_t idx = lv_chart_get_pressed_point(s_chart_pwr);

    if (idx == LV_CHART_POINT_NONE) return;
    show_popup((int32_t)idx);
}

static void popup_click_cb(lv_event_t*) {
    lv_obj_add_flag(s_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_vline,  LV_OBJ_FLAG_HIDDEN);
    s_selected = -1;
}

// Tocar una entrada de la leyenda oculta/muestra su serie (DEYE-52)
static void legend_toggle_cb(lv_event_t* e) {
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_hidden[i] = !s_hidden[i];
    lv_chart_hide_series(s_chart_pwr, s_ser[i], s_hidden[i]);
    lv_obj_set_style_opa(lv_event_get_target_obj(e), s_hidden[i] ? LV_OPA_40 : LV_OPA_COVER, 0);
    if (i < 4) apply_range();            // el autoescalado ignora las series ocultas
    lv_obj_invalidate(s_chart_pwr);
}

// ── Helpers de UI ─────────────────────────────────────────────────────────
static lv_obj_t* nav_btn(lv_obj_t* p, int x, int y, int w, int h, const char* txt,
                          lv_event_cb_t cb) {
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

// Entrada de leyenda: punto + texto dentro de un contenedor clicable (zona de
// toque ampliada) que alterna la serie `idx`
static void legend_item(lv_obj_t* p, int x, int y, lv_color_t c, const char* txt, int idx) {
    const int w = SX(48), h = FONT_SMALL_SIZE + SY(6);
    lv_obj_t* box = lv_obj_create(p);
    lv_obj_set_pos(box, x - SX(3), y - SY(3));
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(box, SY(4));
    lv_obj_add_event_cb(box, legend_toggle_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    lv_obj_t* dot = lv_obj_create(box);
    lv_obj_set_pos(dot, SX(3), SY(3) + 4); lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_bg_color(dot, c, 0); lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, 4, 0);   lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* lbl = lv_label_create(box);
    lv_obj_set_pos(lbl, SX(3) + 11, SY(3) + 1);
    lv_obj_set_style_text_font(lbl, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, C_MUTED, 0);
    lv_label_set_text(lbl, txt);
}

// ── make_chart() — eliminar la línea del axis_tick ────────────────────────
static lv_obj_t* make_chart(lv_obj_t* parent, int y, int h, int y_lo, int y_hi, int hdiv) {
    lv_obj_t* c = lv_chart_create(parent);
    lv_obj_set_pos(c, 0, y); lv_obj_set_size(c, SCREEN_WIDTH, h);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, PTS_DAY);
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
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    // ── Barra de navegación ───────────────────────────────────────────────
    s_btn_prev = nav_btn(parent, SX(2), SY(2), NAV_BTN_W, NAV_BTN_H,
                        LV_SYMBOL_LEFT, prev_cb);
    s_btn_next = nav_btn(parent, SCREEN_WIDTH-NAV_BTN_W-SX(2), SY(2),
                        NAV_BTN_W, NAV_BTN_H, LV_SYMBOL_RIGHT, next_cb);

    // Botón calendario
    const int cal_x = SCREEN_WIDTH-NAV_BTN_W-SX(4)-SX(34);
    lv_obj_t* btn_cal = nav_btn(parent, cal_x, SY(2),
        SX(34), NAV_BTN_H, LV_SYMBOL_CALENDAR, nullptr);
    lv_obj_add_event_cb(btn_cal, [](lv_event_t*) {
        calendar_show(
            day_epoch_from_offset(s_offset),
            Cache.getOldestDailyEpoch(),
            [](uint32_t dep) {
                time_t now; time(&now);
                struct tm tm_now; localtime_r(&now, &tm_now);
                tm_now.tm_hour=0;tm_now.tm_min=0;
                tm_now.tm_sec=0;tm_now.tm_isdst=-1;
                uint32_t today = (uint32_t)mktime(&tm_now);
                s_offset = local_day_offset(dep, today);
                load_range();
            });
    }, LV_EVENT_CLICKED, nullptr);

    // Botón de rango 24h / 7d (DEYE-52), a la izquierda del calendario
    s_btn_range = nav_btn(parent, cal_x - SX(4) - RANGE_BTN_W, SY(2),
        RANGE_BTN_W, NAV_BTN_H, "24h", range_cb);
    s_lbl_range = lv_obj_get_child(s_btn_range, 0);
    lv_obj_set_style_text_font(s_lbl_range, &FONT_SMALL, 0);

    s_lbl_date = lv_label_create(parent);
    lv_obj_set_pos(s_lbl_date, NAV_BTN_W+SX(6), SY(7));
    lv_obj_set_width(s_lbl_date,
        SCREEN_WIDTH - 2*NAV_BTN_W - SX(34) - RANGE_BTN_W - SX(16));
    lv_label_set_long_mode(s_lbl_date, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_lbl_date, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(s_lbl_date, C_WHITE, 0);
    lv_label_set_text(s_lbl_date, "---");
    lv_obj_add_flag(s_lbl_date, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lbl_date, [](lv_event_t*) {
        if (s_offset == 0) return;
        s_offset = 0;
        load_range();
    }, LV_EVENT_CLICKED, nullptr);

    // ── Chart de potencias (altura ampliada, SOC en eje secundario) ──────────
    s_chart_pwr = make_chart(parent, CHART_PWR_Y, MERGED_H, -6000, 6000, 5);
    lv_obj_set_style_pad_right(s_chart_pwr, MERGED_PAD_R, 0);
    lv_chart_set_range(s_chart_pwr, LV_CHART_AXIS_SECONDARY_Y, 0, 100);

    // Etiquetas eje Y izquierda (potencias)
    s_ylabels_container = lv_obj_create(parent);
    lv_obj_set_pos(s_ylabels_container, 0, CHART_PWR_Y);
    lv_obj_set_size(s_ylabels_container, CH_PAD_L, MERGED_H);
    lv_obj_set_style_bg_opa(s_ylabels_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ylabels_container, 0, 0);
    lv_obj_set_style_pad_all(s_ylabels_container, 0, 0);
    lv_obj_set_style_clip_corner(s_ylabels_container, false, 0);
    lv_obj_set_scrollbar_mode(s_ylabels_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s_ylabels_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ylabels_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    add_y_labels(s_ylabels_container, 0, MERGED_H, -6000, 6000, 4, YAXIS_UNIT_PWR);

    // Etiquetas eje Y derecha (SOC %)
    s_ylabels_right = lv_obj_create(parent);
    lv_obj_set_pos(s_ylabels_right, SCREEN_WIDTH - CH_PAD_L, CHART_PWR_Y);
    lv_obj_set_size(s_ylabels_right, CH_PAD_L, MERGED_H);
    lv_obj_set_style_bg_opa(s_ylabels_right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ylabels_right, 0, 0);
    lv_obj_set_style_pad_all(s_ylabels_right, 0, 0);
    lv_obj_set_style_clip_corner(s_ylabels_right, false, 0);
    lv_obj_set_scrollbar_mode(s_ylabels_right, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s_ylabels_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ylabels_right, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    for (int i = 0; i <= 4; i++) {
        int val = i * 25;
        int py = (int)((float)(4 - i) / 4 * (MERGED_H - 2 * CH_PAD_TV))
                 + CH_PAD_TV - FONT_SMALL_SIZE / 2;
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", val);
        lv_obj_t* lbl = lv_label_create(s_ylabels_right);
        lv_obj_set_pos(lbl, 0, py);
        lv_obj_set_width(lbl, CH_PAD_L - 2);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_font(lbl, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl, C_SOC, 0);
        lv_label_set_text(lbl, buf);
    }

    // Series de potencias + SOC en eje secundario
    s_sload = lv_chart_add_series(s_chart_pwr, C_LOAD, LV_CHART_AXIS_PRIMARY_Y);
    s_sbatt = lv_chart_add_series(s_chart_pwr, C_BATT, LV_CHART_AXIS_PRIMARY_Y);
    s_sgrid = lv_chart_add_series(s_chart_pwr, C_GRID, LV_CHART_AXIS_PRIMARY_Y);
    s_spv   = lv_chart_add_series(s_chart_pwr, C_PV,   LV_CHART_AXIS_PRIMARY_Y);
    s_ssoc  = lv_chart_add_series(s_chart_pwr, C_SOC,  LV_CHART_AXIS_SECONDARY_Y);
    s_ser[0] = s_spv; s_ser[1] = s_sgrid; s_ser[2] = s_sbatt; s_ser[3] = s_sload; s_ser[4] = s_ssoc;

    lv_obj_add_flag(s_chart_pwr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_chart_pwr, chart_click_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_remove_flag(s_chart_pwr, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // ── Etiquetas eje X (se rehacen según el rango en load_range) ────────
    s_xlabels = lv_obj_create(parent);
    lv_obj_set_pos(s_xlabels, 0, MERGED_XLAB_Y);
    lv_obj_set_size(s_xlabels, SCREEN_WIDTH, FONT_SMALL_SIZE + SY(2));
    lv_obj_set_style_bg_opa(s_xlabels, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_xlabels, 0, 0);
    lv_obj_set_style_pad_all(s_xlabels, 0, 0);
    lv_obj_set_scrollbar_mode(s_xlabels, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s_xlabels, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_xlabels, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_xlabels, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    rebuild_xlabels();

    // ── Leyenda (tocable: oculta/muestra la serie) ────────────────────────
    struct LegDef { int x; lv_color_t c; const char* txt; };
    LegDef ldefs[] = {
        {SX(5),  C_PV,   "PV"},
        {SX(55), C_GRID, "Red"},
        {SX(105),C_BATT, "Bat"},
        {SX(155),C_LOAD, "Carga"},
        {SX(220),C_SOC,  "SOC"},
    };
    for (int i = 0; i < 5; i++) {
        legend_item(parent, ldefs[i].x, MERGED_LEG_Y, ldefs[i].c, ldefs[i].txt, i);
    }

    // ── Sin datos ─────────────────────────────────────────────────────────
    s_no_data = lv_label_create(parent);
    lv_obj_set_pos(s_no_data, 0, CHART_PWR_Y + MERGED_H/2 - FONT_NORMAL_SIZE/2);
    lv_obj_set_width(s_no_data, SCREEN_WIDTH);
    lv_obj_set_style_text_align(s_no_data, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_no_data, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(s_no_data, C_MUTED, 0);
    lv_label_set_text(s_no_data, "Sin datos para este d\xC3\xAD\x61");

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
    if (active) { load_range(); s_last_tick = millis(); }
}

void chart_screen_tick() {
    if (!s_active) return;

    // ── Detectar medianoche cuando mostramos hoy ──────────────────────────
    if (s_offset == 0) {
        uint32_t current_today = day_epoch_from_offset(0);
        if (current_today != s_day_epoch_loaded) {
            DBGSERIAL.println("[Chart] Cambio de dia, recargando...");
            load_range();
            s_last_tick = millis();
            return;
        }
    }

    // ── Refresco periódico solo cuando es hoy ─────────────────────────────
    if (s_offset != 0) return;
    if (millis() - s_last_tick >= 300000UL) {
        load_range();
        s_last_tick = millis();
    }
}
