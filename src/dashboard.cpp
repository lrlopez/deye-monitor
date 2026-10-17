#include <WiFi.h>
#include "dashboard.h"
#include "ui_constants.h"
#include "config.h"
#include "storage.h"

// ── Colores ───────────────────────────────────────────────────────────────
// Los comunes (C_BG, C_CARD, C_WHITE, C_MUTED, C_OK, C_ERR, C_WARN, C_RUN)
// vienen de ui_constants.h
#define C_SOLAR    C_WARN                       // amarillo — solar
#define C_BATT_OK  C_OK                         // verde    — cargando
#define C_BATT_DIS C_ERR                        // rojo     — descargando
#define C_BATT_IDL C_MUTED                      // gris     — reposo
#define C_GRID_EXP C_OK                         // verde    — exportando
#define C_GRID_IMP C_ERR                        // rojo     — importando
#define C_CARGA    lv_color_hex(0xBB6BD9)       // morado   — igual que chart_screen
#define C_ARC_BG   lv_color_hex(0x21262D)       // fondo de pista

// ── Geometría del layout de cada tarjeta ─────────────────────────────────
// El arco ocupa toda la altura del área de contenido de la tarjeta y se
// alinea a la derecha; la zona de texto queda a la izquierda.
#define DASH_ARC_SZ   (DASH_CARD_H - 2*UI_PAD)            // ej. 102px @ 480×270
#define ARC_THICK     SS(10)
#define DASH_TEXT_GAP SX(4)                                // hueco entre texto y arco
#define DASH_TEXT_W   (DASH_CARD_W - 2*UI_PAD - DASH_ARC_SZ - DASH_TEXT_GAP)
// Y de las líneas de info (relativo al área de contenido de la tarjeta)
#define INFO_Y1       (FONT_NORMAL_SIZE + SY(3))
#define INFO_Y2       (INFO_Y1 + FONT_SMALL_SIZE + SY(2))

// ── Capacidades cargadas desde NVS ───────────────────────────────────────
static uint16_t s_inv_max_w  = INV_MAX_W_DEF;
static uint16_t s_grid_max_w = GRID_MAX_W_DEF;
static uint16_t s_load_max_w = INV_MAX_W_DEF;

// ── Estado para indicadores ───────────────────────────────────────────────
static uint32_t s_last_update_ms  = 0;
static uint32_t s_init_ms         = 0;     // arranque del dashboard: referencia si aún no llegó ningún dato
static int32_t  s_prev_batt_power = INT32_MIN;
static int32_t  s_prev_grid_power = INT32_MIN;

// DEYE-51: umbrales de frescura de los datos
#define STALE_WARN_MS   10000   // >10 s sin datos: la hora de la muestra pasa a ámbar
#define STALE_LOST_MS   30000   // >30 s: datos obsoletos (valores atenuados + aviso persistente)

// Las cuatro tarjetas, para atenuarlas como conjunto cuando los datos están obsoletos
static lv_obj_t* s_cards[4] = {};
static bool      s_stale    = false;

// ── Punteros a widgets actualizables ─────────────────────────────────────
static lv_obj_t *lbl_clock, *lbl_selfcon, *lbl_sample_time, *lbl_wifi;

static lv_obj_t *arc_solar,     *lbl_solar_val;
static lv_obj_t *lbl_pv1,       *lbl_pv2;

static lv_obj_t *arc_grid,      *lbl_grid_val, *lbl_grid_status;

// Batería: 3 arcos de fondo (gradiente) + 1 máscara dinámica
static lv_obj_t *arc_batt_mask;
static lv_obj_t *lbl_batt_soc,  *lbl_batt_pwr, *lbl_batt_status;

static lv_obj_t *arc_load,      *lbl_load_val;

// ── Helper: tarjeta con título limitado a la zona de texto ───────────────
static lv_obj_t* make_card(lv_obj_t* parent, int x, int y,
                            const char* title, lv_color_t accent)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, DASH_CARD_W, DASH_CARD_H);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, accent, 0);
    lv_obj_set_style_border_width(card, UI_BORDER, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_radius(card, UI_RADIUS, 0);
    lv_obj_set_style_pad_all(card, UI_PAD, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(card);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_width(lbl, DASH_TEXT_W);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(lbl, accent, 0);
    lv_obj_set_style_text_font(lbl, &FONT_NORMAL, 0);
    lv_label_set_text(lbl, title);
    return card;
}

// ── Helper: etiqueta de información (zona de texto izquierda) ────────────
static lv_obj_t* make_info(lv_obj_t* parent, int y, const char* text)
{
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, 0, y);
    lv_obj_set_width(lbl, DASH_TEXT_W);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(lbl, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, C_MUTED, 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

// ── Helper: arco gauge alineado a la derecha, altura total de tarjeta ─────
static lv_obj_t* make_arc(lv_obj_t* parent, lv_color_t color, lv_arc_mode_t mode)
{
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, DASH_ARC_SZ, DASH_ARC_SZ);
    lv_obj_align(arc, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_arc_set_mode(arc, mode);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    lv_obj_set_style_arc_color(arc, C_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ARC_THICK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, ARC_THICK, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}

// ── Helper: arco de fondo coloreado sin indicador (para el gradiente) ─────
static void make_bg_arc(lv_obj_t* parent,
                        uint16_t a_start, uint16_t a_end,
                        lv_color_t color)
{
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, DASH_ARC_SZ, DASH_ARC_SZ);
    lv_obj_align(arc, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_bg_angles(arc, a_start, a_end);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ARC_THICK, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, ARC_THICK, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);

    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
}

// ═════════════════════════════════════════════════════════════════════════
// DEYE-50: vista de flujo de energía
// ═════════════════════════════════════════════════════════════════════════
// Alternativa a las 4 tarjetas: cuatro nodos en cruz (Solar arriba, Red a la
// izquierda, Batería a la derecha, Casa en el centro) unidos por líneas sobre las
// que corren puntos animados. Cada enlace se anima solo mientras fluye energía
// (> 50 W): la dirección indica el sentido y la velocidad la intensidad.
// Se alterna tocando la barra superior; la elección se guarda en NVS.
#define FLOW_DOT        SS(7)
#define FLOW_ACTIVE_W   50      // por debajo se considera sin flujo

struct FlowLink {
    lv_obj_t* line   = nullptr;
    lv_obj_t* dots[3] = {};
    int       x1 = 0, y1 = 0, x2 = 0, y2 = 0;   // origen → hub
    int8_t    dir   = 0;        // +1 hacia el hub, -1 desde el hub, 0 parado
    uint8_t   speed = 0;        // 0 lento · 1 medio · 2 rápido
    bool      running = false;
};
static FlowLink s_fl_solar, s_fl_grid, s_fl_batt;
static lv_point_precise_t s_pts_solar[2], s_pts_grid[2], s_pts_batt[2];
static lv_obj_t* s_flow = nullptr;
static lv_obj_t *fl_solar_val, *fl_grid_val, *fl_grid_st, *fl_batt_val, *fl_batt_st, *fl_load_val;
static uint8_t   s_view = 0;                      // 0 tarjetas · 1 flujo

static const uint16_t FLOW_PERIOD_MS[3] = { 2600, 1700, 1000 };   // vuelta completa de un punto
static uint8_t flow_speed(int32_t w) { w = w < 0 ? -w : w; return w < 500 ? 0 : (w < 2000 ? 1 : 2); }

// Coloca los 3 puntos del enlace a lo largo de su segmento; v = fase 0..1000
static void flow_anim_cb(void* var, int32_t v)
{
    FlowLink* l = (FlowLink*)var;
    for (int k = 0; k < 3; k++) {
        int32_t t = (v + k * 333) % 1000;
        if (l->dir < 0) t = 999 - t;                    // sentido inverso: del hub al nodo
        lv_obj_set_pos(l->dots[k],
                       l->x1 + (l->x2 - l->x1) * t / 1000 - FLOW_DOT / 2,
                       l->y1 + (l->y2 - l->y1) * t / 1000 - FLOW_DOT / 2);
    }
}

static void flow_stop(FlowLink& l)
{
    if (l.running) lv_anim_delete(&l, flow_anim_cb);
    l.running = false; l.dir = 0;
    for (int k = 0; k < 3; k++) lv_obj_add_flag(l.dots[k], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_line_opa(l.line, LV_OPA_20, 0);
}

// Actualiza dirección/velocidad; reinicia la animación solo si algo cambió
static void flow_set(FlowLink& l, int8_t dir, uint8_t speed)
{
    if (dir == 0) { flow_stop(l); return; }
    if (l.running && l.dir == dir && l.speed == speed) return;
    if (l.running) lv_anim_delete(&l, flow_anim_cb);
    l.dir = dir; l.speed = speed; l.running = true;
    for (int k = 0; k < 3; k++) lv_obj_remove_flag(l.dots[k], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_line_opa(l.line, LV_OPA_60, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &l);
    lv_anim_set_exec_cb(&a, flow_anim_cb);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, FLOW_PERIOD_MS[speed]);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static void flow_stop_all()
{
    if (!s_flow) return;
    flow_stop(s_fl_solar); flow_stop(s_fl_grid); flow_stop(s_fl_batt);
}

// Enlace: línea tenue + 3 puntos. (x1,y1) es el extremo del nodo y (x2,y2) el del hub.
static void flow_make_link(FlowLink& l, lv_point_precise_t* pts, lv_obj_t* parent,
                           int x1, int y1, int x2, int y2, lv_color_t color)
{
    l.x1 = x1; l.y1 = y1; l.x2 = x2; l.y2 = y2;
    pts[0].x = x1; pts[0].y = y1; pts[1].x = x2; pts[1].y = y2;
    l.line = lv_line_create(parent);
    lv_line_set_points(l.line, pts, 2);
    lv_obj_set_style_line_width(l.line, SS(3), 0);
    lv_obj_set_style_line_color(l.line, color, 0);
    lv_obj_set_style_line_opa(l.line, LV_OPA_20, 0);
    lv_obj_set_style_line_rounded(l.line, true, 0);
    for (int k = 0; k < 3; k++) {
        lv_obj_t* d = lv_obj_create(parent);
        lv_obj_set_size(d, FLOW_DOT, FLOW_DOT);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, color, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        l.dots[k] = d;
    }
}

// Nodo rectangular: título coloreado, valor grande y línea de estado
static lv_obj_t* flow_make_node(lv_obj_t* parent, int x, int y, int w, int h,
                                const char* title, lv_color_t accent,
                                lv_obj_t** val, lv_obj_t** status)
{
    lv_obj_t* n = lv_obj_create(parent);
    lv_obj_set_pos(n, x, y);
    lv_obj_set_size(n, w, h);
    lv_obj_set_style_bg_color(n, C_CARD, 0);
    lv_obj_set_style_bg_opa(n, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(n, accent, 0);
    lv_obj_set_style_border_width(n, UI_BORDER, 0);
    lv_obj_set_style_border_opa(n, LV_OPA_50, 0);
    lv_obj_set_style_radius(n, UI_RADIUS, 0);
    lv_obj_set_style_pad_all(n, SX(5), 0);
    lv_obj_remove_flag(n, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(n, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* t = lv_label_create(n);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_font(t, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(t, accent, 0);
    lv_label_set_text(t, title);

    *val = lv_label_create(n);
    lv_obj_align(*val, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(*val, &FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(*val, C_WHITE, 0);
    lv_label_set_text(*val, "--- W");

    if (status) {
        *status = lv_label_create(n);
        lv_obj_align(*status, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_text_font(*status, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(*status, C_MUTED, 0);
        lv_label_set_text(*status, "--");
    }
    return n;
}

static void flow_create(lv_obj_t* parent)
{
    const int bottom = SS(16);                                  // barra de puntos de paginación
    const int cw = SCREEN_WIDTH;
    const int ch = SCREEN_HEIGHT - TOP_BAR_H - bottom;

    s_flow = lv_obj_create(parent);
    lv_obj_set_pos(s_flow, 0, TOP_BAR_H);
    lv_obj_set_size(s_flow, cw, ch);
    lv_obj_set_style_bg_opa(s_flow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_flow, 0, 0);
    lv_obj_set_style_pad_all(s_flow, 0, 0);
    lv_obj_remove_flag(s_flow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_flow, LV_OBJ_FLAG_CLICKABLE);

    const int nw  = cw * 27 / 100;                              // ancho de nodo
    const int nh  = SY(66);
    const int hub = SS(76);                                     // diámetro del hub
    const int cx  = cw / 2;
    const int top_y = SY(4);
    const int hub_cy = top_y + nh + SY(44) + hub / 2;

    // Nodos
    flow_make_node(s_flow, cx - nw / 2, top_y, nw, nh, LV_SYMBOL_SUN " SOLAR", C_SOLAR, &fl_solar_val, nullptr);
    flow_make_node(s_flow, UI_MARGIN, hub_cy - nh / 2, nw, nh, LV_SYMBOL_PLUG " RED", C_RUN, &fl_grid_val, &fl_grid_st);
    flow_make_node(s_flow, cw - UI_MARGIN - nw, hub_cy - nh / 2, nw, nh,
                   LV_SYMBOL_BATTERY_FULL " BATER\xC3\xAD\x41", C_BATT_OK, &fl_batt_val, &fl_batt_st);

    // Enlaces (origen → hub). Se crean ANTES que el hub para que este los tape en los extremos.
    flow_make_link(s_fl_solar, s_pts_solar, s_flow, cx, top_y + nh, cx, hub_cy - hub / 2, C_SOLAR);
    flow_make_link(s_fl_grid,  s_pts_grid,  s_flow, UI_MARGIN + nw, hub_cy, cx - hub / 2, hub_cy, C_RUN);
    flow_make_link(s_fl_batt,  s_pts_batt,  s_flow, cw - UI_MARGIN - nw, hub_cy, cx + hub / 2, hub_cy, C_BATT_OK);

    // Hub "Casa"
    lv_obj_t* h = lv_obj_create(s_flow);
    lv_obj_set_size(h, hub, hub);
    lv_obj_set_pos(h, cx - hub / 2, hub_cy - hub / 2);
    lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(h, C_CARD, 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(h, C_CARGA, 0);
    lv_obj_set_style_border_width(h, SS(2), 0);
    lv_obj_set_style_pad_all(h, 0, 0);
    lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* hi = lv_label_create(h);
    lv_obj_align(hi, LV_ALIGN_CENTER, 0, -(FONT_NORMAL_SIZE / 2 + 1));
    lv_obj_set_style_text_font(hi, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(hi, C_CARGA, 0);
    lv_label_set_text(hi, LV_SYMBOL_HOME);
    fl_load_val = lv_label_create(h);
    lv_obj_align(fl_load_val, LV_ALIGN_CENTER, 0, FONT_NORMAL_SIZE / 2 + 1);
    lv_obj_set_style_text_font(fl_load_val, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(fl_load_val, C_WHITE, 0);
    lv_label_set_text(fl_load_val, "--- W");

    lv_obj_add_flag(s_flow, LV_OBJ_FLAG_HIDDEN);                // la vista inicial la decide apply_view()
}

static void flow_update(const EnergyData& d)
{
    if (!s_flow) return;
    lv_label_set_text_fmt(fl_solar_val, "%d W", (int)d.pv_power);
    lv_label_set_text_fmt(fl_grid_val,  "%d W", (int)d.grid_power);
    lv_label_set_text_fmt(fl_batt_val,  "%d%%", (int)d.batt_soc);
    lv_label_set_text_fmt(fl_load_val,  "%d W", (int)d.load_power);

    // Solar: siempre hacia la casa
    flow_set(s_fl_solar, d.pv_power > FLOW_ACTIVE_W ? 1 : 0, flow_speed(d.pv_power));

    // Red: importando (+) hacia la casa, exportando (-) desde la casa
    if (d.grid_power > FLOW_ACTIVE_W) {
        flow_set(s_fl_grid, 1, flow_speed(d.grid_power));
        lv_label_set_text(fl_grid_st, "Importando");
    } else if (d.grid_power < -FLOW_ACTIVE_W) {
        flow_set(s_fl_grid, -1, flow_speed(d.grid_power));
        lv_label_set_text(fl_grid_st, "Exportando");
    } else {
        flow_set(s_fl_grid, 0, 0);
        lv_label_set_text(fl_grid_st, "Sin intercambio");
    }

    // Batería: descargando (+) hacia la casa, cargando (-) desde la casa
    if (d.batt_power > FLOW_ACTIVE_W) {
        flow_set(s_fl_batt, 1, flow_speed(d.batt_power));
        lv_label_set_text_fmt(fl_batt_st, "Descarga %d W", (int)d.batt_power);
    } else if (d.batt_power < -FLOW_ACTIVE_W) {
        flow_set(s_fl_batt, -1, flow_speed(d.batt_power));
        lv_label_set_text_fmt(fl_batt_st, "Carga %d W", (int)-d.batt_power);
    } else {
        flow_set(s_fl_batt, 0, 0);
        lv_label_set_text(fl_batt_st, "Reposo");
    }
}

// Muestra la vista elegida y oculta la otra (las animaciones solo corren en la de flujo)
static void apply_view()
{
    const bool flow = (s_view == 1);
    for (int i = 0; i < 4; i++) {
        if (!s_cards[i]) continue;
        if (flow) lv_obj_add_flag(s_cards[i], LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_remove_flag(s_cards[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (flow) lv_obj_remove_flag(s_flow, LV_OBJ_FLAG_HIDDEN);
    else    { lv_obj_add_flag(s_flow, LV_OBJ_FLAG_HIDDEN); flow_stop_all(); }
}

static void view_toggle_cb(lv_event_t*)
{
    s_view = s_view ? 0 : 1;
    apply_view();
    Storage.saveDashView(s_view);
    // En la vista de flujo los enlaces se reactivan con el siguiente dato (≤ 5 s)
}

// ── DEYE-51: datos obsoletos ──────────────────────────────────────────────
// Atenúa las cuatro tarjetas como conjunto (los valores siguen visibles, pero se
// distinguen de un dato vivo). Solo toca el árbol LVGL al cambiar de estado.
static void apply_stale(bool stale)
{
    if (stale == s_stale) return;
    s_stale = stale;
    for (int i = 0; i < 4; i++)
        if (s_cards[i]) lv_obj_set_style_opa(s_cards[i], stale ? LV_OPA_40 : LV_OPA_COVER, 0);
    if (stale) {
        lv_label_set_text(lbl_selfcon, "--");   // el autoconsumo ya no es fiable (ojo: "—" no está en la fuente)
        flow_stop_all();                        // sin datos vivos no se sabe hacia dónde fluye la energía
    }
}

// "45 s", "3 min", "2 h 05 min"
static void format_age(char* buf, size_t n, uint32_t sec)
{
    if (sec < 60)        snprintf(buf, n, "%u s", (unsigned)sec);
    else if (sec < 3600) snprintf(buf, n, "%u min", (unsigned)(sec / 60));
    else                 snprintf(buf, n, "%u h %02u min", (unsigned)(sec / 3600), (unsigned)((sec % 3600) / 60));
}

// ── Inicialización ────────────────────────────────────────────────────────
void dashboard_init(lv_obj_t* parent)
{
    {
        AppConfig cfg{};
        Storage.loadConfig(cfg);
        s_inv_max_w  = cfg.inv_max_w  > 0 ? cfg.inv_max_w  : INV_MAX_W_DEF;
        s_grid_max_w = cfg.grid_max_w > 0 ? cfg.grid_max_w : GRID_MAX_W_DEF;
        s_load_max_w = s_inv_max_w > s_grid_max_w ? s_inv_max_w : s_grid_max_w;
    }

    s_init_ms = millis();
    lv_obj_set_style_bg_color(parent, C_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    // ── Barra superior ────────────────────────────────────────────────────
    lv_obj_t* topbar = lv_obj_create(parent);
    lv_obj_set_pos(topbar, 0, 0);
    lv_obj_set_size(topbar, SCREEN_WIDTH, TOP_BAR_H);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x0A0F14), 0);
    lv_obj_set_style_bg_opa(topbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_pad_hor(topbar, UI_MARGIN, 0);
    lv_obj_set_style_pad_ver(topbar, SY(3), 0);
    lv_obj_remove_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);
    // DEYE-50: tocar la barra alterna entre tarjetas y flujo de energía
    lv_obj_add_flag(topbar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(topbar, view_toggle_cb, LV_EVENT_CLICKED, nullptr);

    // Icono WiFi — extremo izquierdo; color actualizado cada 5 s en dashboard_tick()
    lbl_wifi = lv_label_create(topbar);
    lv_obj_align(lbl_wifi, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_font(lbl_wifi, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl_wifi, C_MUTED, 0);  // gris hasta primera comprobación
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);

    lbl_clock = lv_label_create(topbar);
    lv_obj_align(lbl_clock, LV_ALIGN_LEFT_MID, SX(20), 0);
    lv_obj_set_style_text_font(lbl_clock, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl_clock, C_MUTED, 0);
    lv_label_set_text(lbl_clock, "--:--:--");

    // Hora de la última muestra — siempre el mismo largo: "HH:MM:SS"
    lbl_sample_time = lv_label_create(topbar);
    lv_obj_align(lbl_sample_time, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(lbl_sample_time, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_sample_time, C_MUTED, 0);
    lv_label_set_text(lbl_sample_time, "Datos de --:--:--");

    // Icono de "cambiar vista" en el extremo derecho (la barra entera es táctil)
    lv_obj_t* lbl_view = lv_label_create(topbar);
    lv_obj_align(lbl_view, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_text_font(lbl_view, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl_view, C_RUN, 0);
    lv_label_set_text(lbl_view, LV_SYMBOL_SHUFFLE);

    lbl_selfcon = lv_label_create(topbar);
    lv_obj_align(lbl_selfcon, LV_ALIGN_RIGHT_MID, -(FONT_NORMAL_SIZE + SX(8)), 0);
    lv_obj_set_style_text_font(lbl_selfcon, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl_selfcon, C_WHITE, 0);
    lv_label_set_text(lbl_selfcon, "Autoconsumo --%");

    // ── Posiciones de tarjetas ────────────────────────────────────────────
    const int x0 = UI_MARGIN;
    const int x1 = UI_MARGIN + DASH_CARD_W + UI_GAP;
    const int y0 = TOP_BAR_H + UI_MARGIN;
    const int y1 = TOP_BAR_H + UI_MARGIN + DASH_CARD_H + UI_GAP;

    // ── SOLAR ─────────────────────────────────────────────────────────────
    {
        lv_obj_t* c = make_card(parent, x0, y0, LV_SYMBOL_SUN " SOLAR", C_SOLAR);
        s_cards[0] = c;

        lbl_pv1 = make_info(c, INFO_Y1, "PV1: --- W");
        lbl_pv2 = make_info(c, INFO_Y2, "PV2: --- W");

        arc_solar = make_arc(c, C_SOLAR, LV_ARC_MODE_NORMAL);
        lv_arc_set_range(arc_solar, 0, (int32_t)s_inv_max_w);
        lv_arc_set_value(arc_solar, 0);

        lbl_solar_val = lv_label_create(arc_solar);
        lv_obj_set_style_text_font(lbl_solar_val, &FONT_MEDIUM, 0);
        lv_obj_set_style_text_color(lbl_solar_val, C_WHITE, 0);
        lv_obj_align(lbl_solar_val, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(lbl_solar_val, "--- W");
    }

    // ── RED ───────────────────────────────────────────────────────────────
    {
        lv_obj_t* c = make_card(parent, x1, y0, LV_SYMBOL_PLUG " RED", C_RUN);
        s_cards[1] = c;

        lbl_grid_status = make_info(c, INFO_Y1, "--");

        arc_grid = make_arc(c, C_ARC_BG, LV_ARC_MODE_SYMMETRICAL);
        lv_arc_set_range(arc_grid, -(int32_t)s_grid_max_w, (int32_t)s_grid_max_w);
        lv_arc_set_value(arc_grid, 0);

        lbl_grid_val = lv_label_create(arc_grid);
        lv_obj_set_style_text_font(lbl_grid_val, &FONT_MEDIUM, 0);
        lv_obj_set_style_text_color(lbl_grid_val, C_WHITE, 0);
        lv_obj_align(lbl_grid_val, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(lbl_grid_val, "--- W");
    }

    // ── BATERÍA ───────────────────────────────────────────────────────────
    {
        lv_obj_t* c = make_card(parent, x0, y1,
                                 LV_SYMBOL_BATTERY_FULL " BATER\xC3\xAD\x41", C_BATT_OK);
        s_cards[2] = c;

        lbl_batt_status = make_info(c, INFO_Y1, "--");

        // Arcos de fondo del degradado — 24 segmentos rojo→amarillo→verde.
        // Cada segmento (excepto el último) se extiende 1° sobre el siguiente para
        // eliminar las costuras producidas por el anti-aliasing del renderer de LVGL.
        // El segmento siguiente, dibujado encima, tapa el solapamiento con su color.
        for (int i = 0; i < 24; i++) {
            float t = (float)i / 23.0f;
            uint8_t r, g, b;
            if (t <= 0.5f) {
                float u = t * 2.0f;
                r = (uint8_t)(231 + (245 - 231) * u + 0.5f);
                g = (uint8_t)( 76 + (197 -  76) * u + 0.5f);
                b = (uint8_t)( 60 - ( 60 -  24) * u + 0.5f);
            } else {
                float u = (t - 0.5f) * 2.0f;
                r = (uint8_t)(245 - (245 -  46) * u + 0.5f);
                g = (uint8_t)(197 + (204 - 197) * u + 0.5f);
                b = (uint8_t)( 24 + (113 -  24) * u + 0.5f);
            }
            uint16_t a0 = (uint16_t)(135 + (int)(i       * 270.0f / 24));
            uint16_t a1 = (uint16_t)(135 + (int)((i + 1) * 270.0f / 24) + (i < 23 ? 1 : 0));
            make_bg_arc(c, a0, a1, lv_color_make(r, g, b));
        }

        // Máscara dinámica (REVERSE): cubre la parte no rellena con gris oscuro.
        // MAIN transparente → gradiente de fondo visible; INDICATOR (gris) tapa lo vacío.
        // Valor = 100 − SOC:  SOC=0 → valor=100 → todo cubierto; SOC=100 → valor=0 → nada.
        arc_batt_mask = make_arc(c, C_ARC_BG, LV_ARC_MODE_REVERSE);
        lv_obj_set_style_arc_opa(arc_batt_mask, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_arc_set_range(arc_batt_mask, 0, 100);
        lv_arc_set_value(arc_batt_mask, 100);   // inicio: todo cubierto

        // Etiquetas dentro del arco — SOC% arriba, W abajo, centradas respecto
        // a la fuente para que el bloque quede centrado en el hueco del arco
        lbl_batt_soc = lv_label_create(arc_batt_mask);
        lv_obj_set_style_text_font(lbl_batt_soc, &FONT_MEDIUM, 0);
        lv_obj_set_style_text_color(lbl_batt_soc, C_WHITE, 0);
        lv_obj_align(lbl_batt_soc, LV_ALIGN_CENTER, 0, -(FONT_SMALL_SIZE / 2 + 2));
        lv_label_set_text(lbl_batt_soc, "--%");

        lbl_batt_pwr = lv_label_create(arc_batt_mask);
        lv_obj_set_style_text_font(lbl_batt_pwr, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_batt_pwr, C_MUTED, 0);
        lv_obj_align(lbl_batt_pwr, LV_ALIGN_CENTER, 0, FONT_MEDIUM_SIZE / 2 + 2);
        lv_label_set_text(lbl_batt_pwr, "--- W");
    }

    // ── CARGA ─────────────────────────────────────────────────────────────
    {
        lv_obj_t* c = make_card(parent, x1, y1, LV_SYMBOL_HOME " CARGA", C_CARGA);
        s_cards[3] = c;

        arc_load = make_arc(c, C_CARGA, LV_ARC_MODE_NORMAL);
        lv_arc_set_range(arc_load, 0, (int32_t)s_load_max_w);
        lv_arc_set_value(arc_load, 0);

        lbl_load_val = lv_label_create(arc_load);
        lv_obj_set_style_text_font(lbl_load_val, &FONT_MEDIUM, 0);
        lv_obj_set_style_text_color(lbl_load_val, C_WHITE, 0);
        lv_obj_align(lbl_load_val, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(lbl_load_val, "--- W");
    }

    // ── Vista de flujo (DEYE-50) y vista elegida la última vez ───────────
    flow_create(parent);
    s_view = Storage.loadDashView();
    apply_view();
}

// ── Actualización de datos ────────────────────────────────────────────────
void dashboard_update(const EnergyData& d)
{
    if (!d.valid) return;

    // Hora exacta de la muestra (capturada en el momento de la actualización)
    {
        struct tm ti;
        if (getLocalTime(&ti, 0)) {
            char buf[20];
            snprintf(buf, sizeof(buf), "Act. %02d:%02d:%02d",
                     ti.tm_hour, ti.tm_min, ti.tm_sec);
            lv_label_set_text(lbl_sample_time, buf);
        }
    }

    // ── Solar ─────────────────────────────────────────────────────────────
    {
        int32_t v = d.pv_power < 0 ? 0 : d.pv_power;
        if (v > (int32_t)s_inv_max_w) v = (int32_t)s_inv_max_w;
        lv_arc_set_value(arc_solar, v);
        lv_label_set_text_fmt(lbl_solar_val, "%d W", (int)d.pv_power);
        lv_label_set_text_fmt(lbl_pv1, "PV1: %d W", (int)d.pv1_power);
        lv_label_set_text_fmt(lbl_pv2, "PV2: %d W", (int)d.pv2_power);
    }

    // ── Red ───────────────────────────────────────────────────────────────
    {
        int32_t v = d.grid_power;
        if (v >  (int32_t)s_grid_max_w) v =  (int32_t)s_grid_max_w;
        if (v < -(int32_t)s_grid_max_w) v = -(int32_t)s_grid_max_w;
        lv_arc_set_value(arc_grid, v);

        lv_color_t arc_col, txt_col = C_WHITE;
        const char* status;
        if (d.grid_power > 50) {
            arc_col = C_GRID_IMP;
            status  = "Importando";
            lv_label_set_text_fmt(lbl_grid_val, "+%d W", (int)d.grid_power);
        } else if (d.grid_power < -50) {
            arc_col = C_GRID_EXP;
            status  = "Exportando";
            lv_label_set_text_fmt(lbl_grid_val, "%d W", (int)d.grid_power);
        } else {
            arc_col = C_ARC_BG;
            status  = "Sin intercambio";
            lv_label_set_text_fmt(lbl_grid_val, "%d W", (int)d.grid_power);
        }
        lv_obj_set_style_arc_color(arc_grid, arc_col, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(lbl_grid_val, txt_col, 0);
        {
            const char* tr = "";
            if (s_prev_grid_power != INT32_MIN) {
                int32_t da = abs(d.grid_power) - abs(s_prev_grid_power);
                tr = da > 150 ? " " LV_SYMBOL_UP : da < -150 ? " " LV_SYMBOL_DOWN : "";
            }
            char buf[32]; snprintf(buf, sizeof(buf), "%s%s", status, tr);
            lv_label_set_text(lbl_grid_status, buf);
        }
    }

    // ── Batería ───────────────────────────────────────────────────────────
    {
        int32_t soc = (int32_t)d.batt_soc;
        // Máscara REVERSE: cubre desde el ángulo de fin hacia atrás (parte vacía).
        // valor = 100 - SOC: a SOC=0 cubre todo; a SOC=100 no cubre nada.
        lv_arc_set_value(arc_batt_mask, 100 - soc);
        lv_label_set_text_fmt(lbl_batt_soc, "%d%%", (int)soc);

        lv_color_t pwr_col;
        const char* status;
        if (d.batt_power < -50) {
            pwr_col = C_BATT_OK;  status = "Cargando";
        } else if (d.batt_power > 50) {
            pwr_col = C_BATT_DIS; status = "Descargando";
        } else {
            pwr_col = C_BATT_IDL; status = "Reposo";
        }
        lv_obj_set_style_text_color(lbl_batt_pwr, pwr_col, 0);
        lv_label_set_text_fmt(lbl_batt_pwr, "%d W", (int)d.batt_power);
        {
            const char* tr = "";
            if (s_prev_batt_power != INT32_MIN) {
                int32_t da = abs(d.batt_power) - abs(s_prev_batt_power);
                tr = da > 150 ? " " LV_SYMBOL_UP : da < -150 ? " " LV_SYMBOL_DOWN : "";
            }
            char buf[32]; snprintf(buf, sizeof(buf), "%s%s", status, tr);
            lv_label_set_text(lbl_batt_status, buf);
        }
    }

    // ── Carga ─────────────────────────────────────────────────────────────
    {
        int32_t v = d.load_power < 0 ? 0 : d.load_power;
        if (v > (int32_t)s_load_max_w) v = (int32_t)s_load_max_w;
        lv_arc_set_value(arc_load, v);
        lv_label_set_text_fmt(lbl_load_val, "%d W", (int)d.load_power);
    }

    // ── Autoconsumo instantáneo ───────────────────────────────────────────
    if (d.pv_power > 10) {
        int32_t export_w  = (d.grid_power < 0) ? -d.grid_power : 0;
        int32_t local_use = d.pv_power - export_w;
        if (local_use < 0) local_use = 0;
        int pct = (int)(local_use * 100 / d.pv_power);
        if (pct > 100) pct = 100;
        lv_color_t col = (pct >= 80) ? C_BATT_OK :
                         (pct >= 50) ? C_WARN : C_BATT_DIS;
        lv_label_set_text_fmt(lbl_selfcon, "Autoconsumo %d%%", pct);
        lv_obj_set_style_text_color(lbl_selfcon, col, 0);
    } else {
        lv_label_set_text(lbl_selfcon, "Sin producci\xC3\xB3n");
        lv_obj_set_style_text_color(lbl_selfcon, C_MUTED, 0);
    }

    s_last_update_ms  = millis();
    s_prev_batt_power = d.batt_power;
    s_prev_grid_power = d.grid_power;
    flow_update(d);          // vista de flujo (no hace nada si no se ha creado)
    apply_stale(false);      // dato vivo: quitar la atenuación (el texto "Act. HH:MM:SS" ya se ha repuesto arriba)
}

// ── Reloj + icono WiFi ────────────────────────────────────────────────────
void dashboard_tick()
{
    // ── Reloj (cada 1 s) ──────────────────────────────────────────────────
    static uint32_t s_last = 0;
    if (millis() - s_last >= 1000) {
        s_last = millis();

        struct tm ti;
        if (getLocalTime(&ti, 0)) {
            static const char* MESES[] = {
                "Ene","Feb","Mar","Abr","May","Jun",
                "Jul","Ago","Sep","Oct","Nov","Dic"
            };
            char buf[32];
            snprintf(buf, sizeof(buf), "%d %s  %02d:%02d:%02d",
                     ti.tm_mday, MESES[ti.tm_mon],
                     ti.tm_hour, ti.tm_min, ti.tm_sec);
            lv_label_set_text(lbl_clock, buf);
        }

        // ── Frescura de los datos (DEYE-51) ───────────────────────────────
        // Referencia: la última muestra o, si aún no llegó ninguna, el arranque
        // (así un logger que no responde desde el inicio también se avisa).
        const uint32_t ref = s_last_update_ms ? s_last_update_ms : s_init_ms;
        const uint32_t age = millis() - ref;
        const bool wifi_down = (WiFi.status() != WL_CONNECTED);

        if (age >= STALE_LOST_MS) {
            // Aviso persistente: mientras dure se muestra la causa más probable
            char t[40];
            if (wifi_down) {
                snprintf(t, sizeof(t), LV_SYMBOL_WARNING " Sin conexi\xC3\xB3n WiFi");
            } else {
                char a[16]; format_age(a, sizeof(a), age / 1000);
                snprintf(t, sizeof(t), LV_SYMBOL_WARNING " Sin datos hace %s", a);
            }
            lv_label_set_text(lbl_sample_time, t);
            lv_obj_set_style_text_color(lbl_sample_time, C_ERR, 0);
            apply_stale(true);
        } else {
            // Color de "Act. HH:MM:SS": verde fresco, ámbar con retraso
            lv_color_t lc = (s_last_update_ms == 0) ? C_MUTED :
                            (age < STALE_WARN_MS)   ? C_OK    : C_WARN;
            lv_obj_set_style_text_color(lbl_sample_time, lc, 0);
        }
    }

    // ── Icono WiFi (cada 5 s) — WiFi.RSSI() es ligero pero no trivial ────
    static bool     s_wifi_init = false;
    static uint32_t s_wifi_last = 0;
    if (!s_wifi_init || millis() - s_wifi_last >= 5000) {
        s_wifi_init = true;
        s_wifi_last = millis();

        lv_color_t c;
        if (WiFi.status() != WL_CONNECTED) {
            c = C_MUTED;   // gris  — sin conexión
        } else {
            int32_t rssi = WiFi.RSSI();
            c = (rssi > -60) ? C_OK   :   // verde  — buena señal
                (rssi > -75) ? C_WARN :   // naranja — señal débil
                               C_ERR;     // rojo   — señal muy débil
        }
        lv_obj_set_style_text_color(lbl_wifi, c, 0);
    }
}
