#include "dashboard.h"

// ── Paleta ────────────────────────────────────────────────────────────────
#define C_BG        lv_color_hex(0x0D1117)
#define C_CARD      lv_color_hex(0x161B22)
#define C_SOLAR     lv_color_hex(0xF5C518)
#define C_GRID      lv_color_hex(0x4A9EFF)
#define C_BATT_OK   lv_color_hex(0x2ECC71)
#define C_BATT_DIS  lv_color_hex(0xE74C3C)
#define C_LOAD      lv_color_hex(0xBB6BD9)
#define C_WHITE     lv_color_hex(0xEAEAEA)
#define C_MUTED     lv_color_hex(0x6E7681)
#define C_BAR_BG    lv_color_hex(0x21262D)

// ── Geometría (480 × 272) ─────────────────────────────────────────────────
// 2 columnas × 2 filas, margen 4 px, hueco 4 px
#define MARGIN  4
#define GAP     4
#define CARD_W  ((480 - 2*MARGIN - GAP) / 2)   // 234
#define CARD_H  ((272 - 2*MARGIN - GAP) / 2)   // 130
#define PAD     10  // padding interno de cada tarjeta

// ── Punteros a widgets que se actualizan ──────────────────────────────────
static lv_obj_t *lbl_solar_val, *lbl_solar_sub;
static lv_obj_t *lbl_grid_val,  *lbl_grid_sub;
static lv_obj_t *lbl_batt_soc,  *lbl_batt_pwr, *lbl_batt_sub, *bar_batt;
static lv_obj_t *lbl_load_val;

// ── Helper: crea una tarjeta con título ───────────────────────────────────
static lv_obj_t* make_card(lv_obj_t* parent, int x, int y,
                            const char* title, lv_color_t accent)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, accent, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, PAD, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    // Título
    lv_obj_t* lbl = lv_label_create(card);
    lv_label_set_text(lbl, title);
    lv_obj_set_pos(lbl, 0, 0);
    lv_obj_set_style_text_color(lbl, accent, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    return card;
}

// ── Helper: crea un label estándar en coordenadas de content area ─────────
static lv_obj_t* make_label(lv_obj_t* parent, int x, int y,
                             const lv_font_t* font, lv_color_t color,
                             const char* text)
{
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

// ── Inicialización del dashboard ──────────────────────────────────────────
void dashboard_init() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    int x0 = MARGIN;
    int x1 = MARGIN + CARD_W + GAP;
    int y0 = MARGIN;
    int y1 = MARGIN + CARD_H + GAP;

    // ── Tarjeta SOLAR (top-left) ──────────────────────────────────────────
    lv_obj_t* c_solar = make_card(scr, x0, y0, LV_SYMBOL_CHARGE " SOLAR", C_SOLAR);

    lbl_solar_val = make_label(c_solar, 0, 22,
                               &lv_font_montserrat_28, C_WHITE, "--- W");
    lbl_solar_sub = make_label(c_solar, 0, 90,
                               &lv_font_montserrat_14, C_MUTED,
                               "PV1: --- W   PV2: --- W");

    // ── Tarjeta RED (top-right) ───────────────────────────────────────────
    lv_obj_t* c_grid = make_card(scr, x1, y0, LV_SYMBOL_WIFI " RED", C_GRID);

    lbl_grid_val = make_label(c_grid, 0, 22,
                              &lv_font_montserrat_28, C_WHITE, "--- W");
    lbl_grid_sub = make_label(c_grid, 0, 90,
                              &lv_font_montserrat_14, C_MUTED, "--");

    // ── Tarjeta BATERÍA (bottom-left) ─────────────────────────────────────
    lv_obj_t* c_batt = make_card(scr, x0, y1, LV_SYMBOL_BATTERY_FULL " BATERIA", C_BATT_OK);

    lbl_batt_soc = make_label(c_batt, 0, 18,
                              &lv_font_montserrat_28, C_WHITE, "--%");

    bar_batt = lv_bar_create(c_batt);
    lv_obj_set_pos(bar_batt, 0, 56);
    lv_obj_set_size(bar_batt, CARD_W - PAD * 2, 14);
    lv_bar_set_range(bar_batt, 0, 100);
    lv_bar_set_value(bar_batt, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_batt, C_BAR_BG,   LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_batt, C_BATT_OK,  LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_batt, 7, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_batt, 7, LV_PART_INDICATOR);

    lbl_batt_pwr = make_label(c_batt, 0, 76,
                              &lv_font_montserrat_14, C_WHITE, "--- W");
    lbl_batt_sub = make_label(c_batt, 0, 93,
                              &lv_font_montserrat_14, C_MUTED, "--");

    // ── Tarjeta CARGA (bottom-right) ──────────────────────────────────────
    lv_obj_t* c_load = make_card(scr, x1, y1, LV_SYMBOL_HOME " CARGA", C_LOAD);

    make_label(c_load, 0, 22,
               &lv_font_montserrat_14, C_MUTED, "Consumo actual");
    lbl_load_val = make_label(c_load, 0, 44,
                              &lv_font_montserrat_28, C_WHITE, "--- W");

    Serial0.println("Dashboard creado");
}

// ── Actualización de datos ────────────────────────────────────────────────
void dashboard_update(const EnergyData& d) {
    if (!d.valid) return;

    // Solar
    lv_label_set_text_fmt(lbl_solar_val, "%d W", (int)d.pv_power);
    lv_label_set_text_fmt(lbl_solar_sub, "PV1: %d W   PV2: %d W",
                          (int)d.pv1_power, (int)d.pv2_power);

    // Red
    if (d.grid_power > 0) {
        lv_label_set_text_fmt(lbl_grid_val, "+%d W", (int)d.grid_power);
        lv_label_set_text(lbl_grid_sub, "Importando de la red");
    } else if (d.grid_power < 0) {
        lv_label_set_text_fmt(lbl_grid_val, "%d W", (int)d.grid_power);
        lv_label_set_text(lbl_grid_sub, "Exportando a la red");
    } else {
        lv_label_set_text(lbl_grid_val, "0 W");
        lv_label_set_text(lbl_grid_sub, "Sin intercambio");
    }

    // Batería – color barra según estado
    lv_color_t batt_color = (d.batt_power >= 0) ? C_BATT_OK : C_BATT_DIS;
    lv_obj_set_style_bg_color(bar_batt, batt_color, LV_PART_INDICATOR);
    lv_bar_set_value(bar_batt, (int32_t)d.batt_soc, LV_ANIM_ON);
    lv_label_set_text_fmt(lbl_batt_soc, "%d%%", (int)d.batt_soc);

    if (d.batt_power > 0) {
        lv_label_set_text_fmt(lbl_batt_pwr, "+%d W", (int)d.batt_power);
        lv_label_set_text(lbl_batt_sub, "Cargando");
    } else if (d.batt_power < 0) {
        lv_label_set_text_fmt(lbl_batt_pwr, "%d W", (int)d.batt_power);
        lv_label_set_text(lbl_batt_sub, "Descargando");
    } else {
        lv_label_set_text(lbl_batt_pwr, "0 W");
        lv_label_set_text(lbl_batt_sub, "En reposo");
    }

    // Carga
    lv_label_set_text_fmt(lbl_load_val, "%d W", (int)d.load_power);
}