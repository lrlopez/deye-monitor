#include "calendar_popup.h"
#include "psram_cache.h"
#include "ui_constants.h"
#include <time.h>

// ── Geometría ─────────────────────────────────────────────────────────────
#define CAL_W       SX(308)
#define CAL_H       SY(222)
#define CAL_X       ((SCREEN_WIDTH  - CAL_W) / 2)
#define CAL_Y       ((SCREEN_HEIGHT - CAL_H) / 2)
#define CAL_HDR_H   SY(30)
#define CAL_WD_H    SY(18)
#define CAL_ROWS    6
#define CAL_CELL_W  (CAL_W / 7)
#define CAL_CELL_H  ((CAL_H - CAL_HDR_H - CAL_WD_H) / CAL_ROWS)

static const char* MESES[] = {
    "Enero","Febrero","Marzo","Abril","Mayo","Junio",
    "Julio","Agosto","Septiembre","Octubre","Nov.","Dic."
};
static const char* WD[] = {"Lu","Ma","Mi","Ju","Vi","Sa","Do"};

// ── Estado ────────────────────────────────────────────────────────────────
static lv_obj_t*      s_overlay  = nullptr;
static lv_obj_t*      s_popup    = nullptr;
static lv_obj_t*      s_lbl_month= nullptr;
static lv_obj_t*      s_cells[42]= {};   // contenedores (fondo)
static uint32_t       s_epochs[42]={};   // epoch de cada celda (0=vacía/disabled)
static CalendarSelectCb s_cb     = nullptr;
static uint32_t       s_sel_ep   = 0;
static uint32_t       s_old_ep   = 0;
static int            s_mon      = 0;
static int            s_yr       = 0;

// ── Helpers de fecha ──────────────────────────────────────────────────────
static uint32_t today_ep() {
    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
    return (uint32_t)mktime(&t);
}

static uint32_t make_ep(int y, int m, int d) {
    struct tm t{};
    t.tm_year = y-1900; t.tm_mon = m; t.tm_mday = d; t.tm_isdst = -1;
    return (uint32_t)mktime(&t);
}

static int first_weekday(int y, int m) {   // 0=Lun … 6=Dom
    struct tm t{};
    t.tm_year = y-1900; t.tm_mon = m; t.tm_mday = 1; t.tm_isdst = -1;
    mktime(&t);
    return (t.tm_wday == 0) ? 6 : t.tm_wday - 1;
}

static int days_in_month(int y, int m) {
    struct tm t1{}, t2{};
    t1.tm_year = t2.tm_year = y-1900;
    t1.tm_mon  = m;   t1.tm_mday = 1; t1.tm_isdst = -1;
    t2.tm_mon  = m+1; t2.tm_mday = 1; t2.tm_isdst = -1;
    return (int)((mktime(&t2) - mktime(&t1)) / 86400);
}

// ── Click en celda ────────────────────────────────────────────────────────
static void cell_cb(lv_event_t* e) {
    lv_obj_t* tgt = (lv_obj_t*)lv_event_get_target(e);
    for (int i = 0; i < 42; i++) {
        if (s_cells[i] == tgt && s_epochs[i] != 0) {
            if (s_cb) s_cb(s_epochs[i]);
            calendar_hide();
            return;
        }
    }
}

// ── Render del mes ────────────────────────────────────────────────────────
static void render_month() {
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d", MESES[s_mon], s_yr);
    lv_label_set_text(s_lbl_month, buf);

    uint32_t today = today_ep();
    int dim      = days_in_month(s_yr, s_mon);
    int first_wd = first_weekday(s_yr, s_mon);

    for (int ci = 0; ci < 42; ci++) {
        int day = ci - first_wd + 1;
        lv_obj_t* lbl = lv_obj_get_child(s_cells[ci], 0);

        if (day < 1 || day > dim) {
            // Celda vacía
            lv_label_set_text(lbl, "");
            lv_obj_set_style_bg_opa(s_cells[ci], LV_OPA_TRANSP, 0);
            lv_obj_remove_flag(s_cells[ci], LV_OBJ_FLAG_CLICKABLE);
            s_epochs[ci] = 0;
            continue;
        }

        uint32_t dep     = make_ep(s_yr, s_mon, day);
        bool is_today    = (dep == today);
        bool is_sel      = (dep == s_sel_ep);
        bool is_future   = (dep > today);
        bool is_old      = (s_old_ep > 0 && dep < s_old_ep);
        bool is_disabled = is_future || is_old;

        bool has_data = !is_disabled && Cache.dayHasData(dep);

        // Epoch para el callback (0 = no clickable)
        s_epochs[ci] = is_disabled ? 0 : dep;

        // Texto
        char ds[4]; snprintf(ds, sizeof(ds), "%d", day);
        lv_label_set_text(lbl, ds);

        // Fondo
        if (is_sel) {
            lv_obj_set_style_bg_color(s_cells[ci], C_SEL_BG, 0);
            lv_obj_set_style_bg_opa(s_cells[ci], LV_OPA_COVER, 0);
        } else if (is_today) {
            lv_obj_set_style_bg_color(s_cells[ci], C_TODAY_BG, 0);
            lv_obj_set_style_bg_opa(s_cells[ci], LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_opa(s_cells[ci], LV_OPA_TRANSP, 0);
        }

        // Color de texto
        lv_color_t tc =
            is_disabled                    ? C_DISABLED  :
            (is_sel || is_today)           ? C_WHITE     :
            has_data                       ? C_HAS_DATA  : C_MUTED;
        lv_obj_set_style_text_color(lbl, tc, 0);

        // Clickable
        if (is_disabled) lv_obj_remove_flag(s_cells[ci], LV_OBJ_FLAG_CLICKABLE);
        else             lv_obj_add_flag(s_cells[ci],    LV_OBJ_FLAG_CLICKABLE);
    }
}

// ── Botones de navegación del header ─────────────────────────────────────
static void prev_mon_cb(lv_event_t*) {
    s_mon--;
    if (s_mon < 0) { s_mon = 11; s_yr--; }
    render_month();
}
static void next_mon_cb(lv_event_t*) {
    // No ir más allá del mes actual
    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);
    if (s_yr > t.tm_year+1900 ||
        (s_yr == t.tm_year+1900 && s_mon >= t.tm_mon)) return;
    s_mon++;
    if (s_mon > 11) { s_mon = 0; s_yr++; }
    render_month();
}

static lv_obj_t* hdr_btn(lv_obj_t* p, int x, const char* txt, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(p);
    lv_obj_set_pos(btn, x, (CAL_HDR_H - SY(22))/2);
    lv_obj_set_size(btn, SX(28), SY(22));
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x21262D), 0);
    lv_obj_set_style_radius(btn, SS(4), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t* l = lv_label_create(btn);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, C_ACCENT, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

// ── API pública ───────────────────────────────────────────────────────────
void calendar_show(uint32_t sel_epoch,
                   uint32_t oldest_epoch,
                   CalendarSelectCb cb) {
    if (s_popup) calendar_hide();

    s_cb     = cb;
    s_sel_ep = sel_epoch;
    s_old_ep = oldest_epoch;

    // Mes inicial = mes del día seleccionado (o mes actual)
    uint32_t start = sel_epoch ? sel_epoch : today_ep();
    time_t t0 = (time_t)start;
    struct tm ts; localtime_r(&t0, &ts);
    s_mon = ts.tm_mon;
    s_yr  = ts.tm_year + 1900;

    // ── Overlay ───────────────────────────────────────────────────────────
    s_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_size(s_overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(s_overlay, C_OVERLAY, 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, [](lv_event_t*){ calendar_hide(); },
                        LV_EVENT_CLICKED, nullptr);
    lv_obj_move_foreground(s_overlay);

    // ── Popup ─────────────────────────────────────────────────────────────
    s_popup = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(s_popup, CAL_X, CAL_Y);
    lv_obj_set_size(s_popup, CAL_W, CAL_H);
    lv_obj_set_style_bg_color(s_popup, C_CARD, 0);
    lv_obj_set_style_bg_opa(s_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_popup, C_BORDER, 0);
    lv_obj_set_style_border_width(s_popup, 1, 0);
    lv_obj_set_style_radius(s_popup, SS(10), 0);
    lv_obj_set_style_pad_all(s_popup, 0, 0);
    lv_obj_set_scrollbar_mode(s_popup, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_popup);

    // ── Header ────────────────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(s_popup);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, CAL_W, CAL_HDR_H);
    lv_obj_set_style_bg_color(hdr, C_HDR_BG, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    hdr_btn(hdr, SX(4), LV_SYMBOL_LEFT, prev_mon_cb);

    s_lbl_month = lv_label_create(hdr);
    lv_obj_set_width(s_lbl_month, CAL_W - SX(80));
    lv_obj_align(s_lbl_month, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(s_lbl_month, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(s_lbl_month, C_WHITE, 0);
    lv_obj_set_style_text_align(s_lbl_month, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_lbl_month, "");

    hdr_btn(hdr, CAL_W - SX(4) - SX(28), LV_SYMBOL_RIGHT, next_mon_cb);

    // Separador bajo el header
    lv_obj_t* sep = lv_obj_create(s_popup);
    lv_obj_set_pos(sep, 0, CAL_HDR_H);
    lv_obj_set_size(sep, CAL_W, 1);
    lv_obj_set_style_bg_color(sep, C_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    // ── Cabeceras días de semana ──────────────────────────────────────────
    for (int c = 0; c < 7; c++) {
        lv_obj_t* l = lv_label_create(s_popup);
        lv_obj_set_pos(l, c * CAL_CELL_W, CAL_HDR_H + 2);
        lv_obj_set_size(l, CAL_CELL_W, CAL_WD_H);
        lv_obj_set_style_text_font(l, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(l, C_MUTED, 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(l, WD[c]);
    }

    // ── Grid de celdas (42) ───────────────────────────────────────────────
    int grid_y = CAL_HDR_H + CAL_WD_H + 2;
    for (int ci = 0; ci < 42; ci++) {
        int row = ci / 7, col = ci % 7;

        lv_obj_t* cell = lv_obj_create(s_popup);
        lv_obj_set_pos(cell, col * CAL_CELL_W, grid_y + row * CAL_CELL_H);
        lv_obj_set_size(cell, CAL_CELL_W, CAL_CELL_H);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_radius(cell, SS(4), 0);
        lv_obj_set_style_bg_color(cell, C_PRESS, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(cell, cell_cb, LV_EVENT_CLICKED, nullptr);

        // Label centrado
        lv_obj_t* lbl = lv_label_create(cell);
        lv_obj_set_style_text_font(lbl, &FONT_SMALL, 0);
        lv_obj_center(lbl);
        lv_label_set_text(lbl, "");

        s_cells[ci]  = cell;
        s_epochs[ci] = 0;
    }

    // ── Leyenda mini ──────────────────────────────────────────────────────
    // Debajo del grid, muy pequeña: ● con datos  ● sin datos
    // (solo si caben — en 272px de alto podría ajustar)
    // Omitimos si no hay espacio; los colores son autoexplicativos

    render_month();
}

void calendar_hide() {
    if (s_overlay) { lv_obj_del(s_overlay); s_overlay = nullptr; }
    if (s_popup)   { lv_obj_del(s_popup);   s_popup   = nullptr; }
    s_cb = nullptr;
}

bool calendar_is_visible() { return s_popup != nullptr; }
