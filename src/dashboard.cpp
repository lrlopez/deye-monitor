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

// ── Punteros a widgets actualizables ─────────────────────────────────────
static lv_obj_t *lbl_clock, *lbl_selfcon, *lbl_sample_time;

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

    lbl_clock = lv_label_create(topbar);
    lv_obj_align(lbl_clock, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_font(lbl_clock, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(lbl_clock, C_MUTED, 0);
    lv_label_set_text(lbl_clock, "--:--:--");

    // Hora de la última muestra — siempre el mismo largo: "HH:MM:SS"
    lbl_sample_time = lv_label_create(topbar);
    lv_obj_align(lbl_sample_time, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(lbl_sample_time, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_sample_time, C_MUTED, 0);
    lv_label_set_text(lbl_sample_time, "Datos de --:--:--");

    lbl_selfcon = lv_label_create(topbar);
    lv_obj_align(lbl_selfcon, LV_ALIGN_RIGHT_MID, 0, 0);
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
        lv_obj_t* c = make_card(parent, x0, y0, LV_SYMBOL_CHARGE " SOLAR", C_SOLAR);

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
        lv_obj_t* c = make_card(parent, x1, y0, LV_SYMBOL_WIFI " RED", C_RUN);

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
                                 LV_SYMBOL_BATTERY_FULL " BATERIA", C_BATT_OK);

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

        arc_load = make_arc(c, C_CARGA, LV_ARC_MODE_NORMAL);
        lv_arc_set_range(arc_load, 0, (int32_t)s_load_max_w);
        lv_arc_set_value(arc_load, 0);

        lbl_load_val = lv_label_create(arc_load);
        lv_obj_set_style_text_font(lbl_load_val, &FONT_MEDIUM, 0);
        lv_obj_set_style_text_color(lbl_load_val, C_WHITE, 0);
        lv_obj_align(lbl_load_val, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(lbl_load_val, "--- W");
    }
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
        lv_label_set_text(lbl_grid_status, status);
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
        lv_label_set_text(lbl_batt_status, status);
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
        lv_label_set_text(lbl_selfcon, "Sin produccion");
        lv_obj_set_style_text_color(lbl_selfcon, C_MUTED, 0);
    }
}

// ── Reloj ─────────────────────────────────────────────────────────────────
void dashboard_tick()
{
    static uint32_t s_last = 0;
    if (millis() - s_last < 1000) return;
    s_last = millis();

    struct tm ti;
    if (!getLocalTime(&ti, 0)) return;

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
