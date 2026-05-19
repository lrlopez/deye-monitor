#include <WiFi.h>
#include "config_screen.h"
#include "storage.h"
#include "telegram.h"
#include "ui_constants.h"
#include "config.h"
#include "backlight.h"

// Posición absoluta del campo SSID en pantalla (para anclar el dropdown)
// sec_wifi está en y=28, campo SSID en y=18 dentro de la sección
// 28 (sec y) + CFG_SEC_PAD(10) + 18 (row y) + CFG_FIELD_H(36) + 2 = 94
#define SSID_DROPDOWN_Y  94
#define SSID_DROPDOWN_X  (10 + CFG_SEC_PAD + CFG_LBL_W)   // ≈ 150
#define SSID_DROPDOWN_W  (CFG_FIELD_W + CFG_SCAN_BTN_W + 4)
#define SSID_DROPDOWN_H  120

// ── Widgets formulario ────────────────────────────────────────────────────
static lv_obj_t* ta_ssid;
static lv_obj_t* ta_pass;
static lv_obj_t* ta_logger_ip;
static lv_obj_t* ta_logger_serial;
static lv_obj_t* lbl_ip;
static lv_obj_t* lbl_rssi;
static lv_obj_t* lbl_status;
static lv_obj_t* kb;
static lv_obj_t* s_ta_tg_token   = nullptr;
static lv_obj_t* s_ta_tg_chatid  = nullptr;
static lv_obj_t* s_slider_tg_thr = nullptr;
static lv_obj_t* s_cb_tg_solar   = nullptr;
static lv_obj_t* s_cb_tg_grid    = nullptr;
static lv_obj_t* s_cb_tg_logger  = nullptr;

static lv_obj_t* s_slider_bl_norm    = nullptr;
static lv_obj_t* s_lbl_bl_norm       = nullptr;
static lv_obj_t* s_slider_bl_red     = nullptr;
static lv_obj_t* s_lbl_bl_red        = nullptr;
static lv_obj_t* s_cb_bl_inact       = nullptr;
static lv_obj_t* s_slider_bl_inact   = nullptr;
static lv_obj_t* s_lbl_bl_inact      = nullptr;
static lv_obj_t* s_cb_bl_night       = nullptr;
static lv_obj_t* s_slider_bl_nstart  = nullptr;
static lv_obj_t* s_lbl_bl_nstart     = nullptr;
static lv_obj_t* s_slider_bl_nend    = nullptr;
static lv_obj_t* s_lbl_bl_nend       = nullptr;

static lv_obj_t* s_ta_admin_pass     = nullptr;

// ── Widgets scan WiFi ─────────────────────────────────────────────────────
static lv_obj_t*  scan_btn;
static lv_obj_t*  scan_btn_lbl;
static lv_obj_t*  ssid_list;        // lista flotante de redes
static lv_timer_t* scan_timer = nullptr;
static bool        s_scanning  = false;

// ── Config gráfica (estáticos para save_btn_cb) ───────────────────────────
static lv_obj_t* cb_autoscale;
static lv_obj_t* ta_kw;
static lv_obj_t* slider_kw_global = nullptr;

// ── Timestamp último refresco red ─────────────────────────────────────────
static uint32_t s_last_net_refresh = 0;

// ═════════════════════════════════════════════════════════════════════════
// Helpers genéricos de UI
// ═════════════════════════════════════════════════════════════════════════
static lv_obj_t* make_section(lv_obj_t* parent, const char* title, int y, int h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, 10, y);
    lv_obj_set_size(card, CFG_SECTION_W, h);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, C_ACCENT, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_radius(card, UI_RADIUS, 0);
    lv_obj_set_style_pad_all(card, CFG_SEC_PAD, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(card);
    lv_obj_set_pos(lbl, 0, 0);
    lv_obj_set_style_text_font(lbl, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl, C_ACCENT, 0);
    lv_label_set_text(lbl, title);
    return card;
}

static lv_obj_t* make_field(lv_obj_t* parent, int x, int y, int w,
                              bool password, const char* placeholder) {
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_set_pos(ta, x, y);
    lv_obj_set_size(ta, w, CFG_FIELD_H);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    if (password) lv_textarea_set_password_mode(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x21262D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, C_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, C_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, &FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ta, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ta, 6, LV_PART_MAIN);
    // Cursor: los estilos de LVGL se resuelven por especificidad de estado.
    // El tema tiene estilos para LV_PART_CURSOR|LV_STATE_FOCUSED que ganan
    // sobre estilos base locales → cursor invisible con foco, visible sin él.
    // Solución: ocultar en base y definir el estilo también con LV_STATE_FOCUSED.
    lv_obj_set_style_border_width(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(ta, C_WHITE, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2, LV_PART_CURSOR | LV_STATE_FOCUSED);
    return ta;
}

static lv_obj_t* make_row_label(lv_obj_t* parent, int y, const char* txt) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_pos(l, 0, y + 10);
    lv_obj_set_style_text_font(l, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, C_MUTED, 0);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t* make_info_row(lv_obj_t* parent, int y, const char* key) {
    lv_obj_t* lk = lv_label_create(parent);
    lv_obj_set_pos(lk, 0, y);
    lv_obj_set_style_text_font(lk, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lk, C_MUTED, 0);
    lv_label_set_text(lk, key);

    lv_obj_t* lv2 = lv_label_create(parent);
    lv_obj_set_pos(lv2, CFG_LBL_W, y);
    lv_obj_set_style_text_font(lv2, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lv2, C_WHITE, 0);
    lv_label_set_text(lv2, "...");
    return lv2;
}

// Crea: [label izq]  [slider]  [value label]
// Devuelve el slider. value_out recibe el label del valor.
static lv_obj_t* make_slider_row(lv_obj_t* parent, int y,
                                  const char* left_lbl,
                                  int vmin, int vmax, int vdef,
                                  lv_obj_t** value_lbl_out,
                                  const char* unit,
                                  lv_event_cb_t cb) {
    lv_obj_t* ll = lv_label_create(parent);
    lv_obj_set_pos(ll, 0, y + SY(4));
    lv_obj_set_style_text_font(ll, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(ll, C_MUTED, 0);
    lv_label_set_text(ll, left_lbl);

    // Label de valor (a la derecha, ancho fijo)
    const int VAL_W = SX(44);
    *value_lbl_out = lv_label_create(parent);
    lv_obj_set_pos(*value_lbl_out, CFG_LBL_W, y + SY(4));
    lv_obj_set_size(*value_lbl_out, VAL_W, FONT_SMALL_SIZE + SY(4));
    lv_obj_set_style_text_font(*value_lbl_out, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(*value_lbl_out, C_WHITE, 0);
    char buf[12]; snprintf(buf, sizeof(buf), "%d%s", vdef, unit);
    lv_label_set_text(*value_lbl_out, buf);

    // Slider
    int slider_x = CFG_LBL_W + VAL_W + SX(4);
    int slider_w = CFG_SECTION_W - CFG_SEC_PAD*2 - slider_x - SX(8);
    lv_obj_t* sl = lv_slider_create(parent);
    lv_obj_set_pos(sl, slider_x, y + SY(2));
    lv_obj_set_size(sl, slider_w, SS(16));
    lv_slider_set_range(sl, vmin, vmax);
    lv_slider_set_value(sl, vdef, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, C_BTN,                   LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x21262D),  LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, C_WHITE,                 LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, SX(4),                    LV_PART_KNOB);
    if (cb) lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, *value_lbl_out);
    return sl;
}

// ── Icono de señal WiFi según RSSI ─────────────────────────────────────────
static const char* rssi_icon(int rssi) {
    if (rssi >= -55) return LV_SYMBOL_WIFI;          // 3 barras
    if (rssi >= -70) return LV_SYMBOL_WIFI;          // LVGL no tiene iconos parciales
    return LV_SYMBOL_WIFI;                           // usamos color para diferenciar
}

static lv_color_t rssi_color(int rssi) {
    if (rssi > -60) return C_OK;
    if (rssi > -75) return C_WARN;
    return C_ERR;
}

// ═════════════════════════════════════════════════════════════════════════
// Lista desplegable de SSIDs
// ═════════════════════════════════════════════════════════════════════════
static void close_ssid_list() {
    if (ssid_list) {
        lv_obj_del(ssid_list);
        ssid_list = nullptr;
    }
}

static void ssid_item_cb(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    // El label del botón de lista lleva el SSID (primer label hijo)
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
    if (!lbl) return;

    const char* full = lv_label_get_text(lbl);
    // El texto tiene formato "● SSID_NAME (-XX dBm)"
    // Extraemos solo el SSID: entre "● " y " ("
    String s(full);
    int start = s.indexOf(' ') + 1;        // después del símbolo
    int end   = s.lastIndexOf(" (");
    if (start > 0 && end > start)
        lv_textarea_set_text(ta_ssid, s.substring(start, end).c_str());
    else
        lv_textarea_set_text(ta_ssid, full);

    close_ssid_list();
}

static void build_ssid_list(lv_obj_t* parent_screen) {
    close_ssid_list();   // cerrar si ya había una

    int n = WiFi.scanComplete();
    if (n <= 0) return;   // sin resultados aún

    // Ordenar índices por RSSI descendente
    int idx[n];
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (WiFi.RSSI(idx[j]) > WiFi.RSSI(idx[i])) { int t=idx[i]; idx[i]=idx[j]; idx[j]=t; }

    // Contenedor flotante anclado a la pantalla raíz
    ssid_list = lv_list_create(parent_screen);
    lv_obj_set_pos(ssid_list, SSID_DROPDOWN_X, SSID_DROPDOWN_Y);
    lv_obj_set_size(ssid_list, SSID_DROPDOWN_W, SSID_DROPDOWN_H);
    lv_obj_set_style_bg_color(ssid_list, C_LIST, 0);
    lv_obj_set_style_bg_opa(ssid_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ssid_list, C_ACCENT, 0);
    lv_obj_set_style_border_width(ssid_list, 1, 0);
    lv_obj_set_style_border_opa(ssid_list, LV_OPA_60, 0);
    lv_obj_set_style_radius(ssid_list, 6, 0);
    lv_obj_set_style_pad_all(ssid_list, 2, 0);
    lv_obj_set_style_pad_row(ssid_list, 1, 0);

    // Cabecera con conteo + botón cerrar
    lv_obj_t* hdr = lv_list_add_btn(ssid_list, nullptr, "");
    char hdr_txt[32];
    snprintf(hdr_txt, sizeof(hdr_txt), "%d redes encontradas", n);
    lv_obj_t* hdr_lbl = lv_obj_get_child(hdr, 0);
    if (hdr_lbl) {
        lv_label_set_text(hdr_lbl, hdr_txt);
        lv_obj_set_style_text_color(hdr_lbl, C_MUTED, 0);
        lv_obj_set_style_text_font(hdr_lbl, &FONT_SMALL, 0);
    }
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0D1117), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(hdr, [](lv_event_t*){ close_ssid_list(); },
                        LV_EVENT_CLICKED, nullptr);

    // Ítem por red
    int shown = (n > 8) ? 8 : n;   // máximo 8 para no desbordarse
    for (int i = 0; i < shown; i++) {
        int  ri   = idx[i];
        int  rssi = WiFi.RSSI(ri);
        bool open = (WiFi.encryptionType(ri) == WIFI_AUTH_OPEN);

        char txt[64];
        snprintf(txt, sizeof(txt), "%s %s (%d dBm)",
                 open ? LV_SYMBOL_WIFI : LV_SYMBOL_WIFI,
                 WiFi.SSID(ri).c_str(), rssi);

        lv_obj_t* btn = lv_list_add_btn(ssid_list, nullptr, txt);
        lv_obj_set_style_bg_color(btn, C_LIST, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x21262D), LV_STATE_PRESSED);

        // Color del texto según señal
        lv_obj_t* item_lbl = lv_obj_get_child(btn, 0);
        if (item_lbl) {
            lv_obj_set_style_text_color(item_lbl, rssi_color(rssi), 0);
            lv_obj_set_style_text_font(item_lbl, &FONT_SMALL, 0);
        }
        lv_obj_add_event_cb(btn, ssid_item_cb, LV_EVENT_CLICKED, nullptr);
    }

    // Mover al frente para que quede sobre otros widgets
    lv_obj_move_foreground(ssid_list);
}

// ═════════════════════════════════════════════════════════════════════════
// Timer de sondeo del escaneo WiFi
// ═════════════════════════════════════════════════════════════════════════
static lv_obj_t* s_parent_screen = nullptr;   // guardado en init para el build_ssid_list

static void scan_poll_cb(lv_timer_t* /*t*/) {
    int result = WiFi.scanComplete();

    if (result == WIFI_SCAN_RUNNING) return;   // todavía escaneando

    // Escaneo terminado (resultado ≥ 0) o error (-1)
    lv_timer_del(scan_timer);
    scan_timer = nullptr;
    s_scanning = false;

    lv_label_set_text(scan_btn_lbl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(scan_btn_lbl, C_ACCENT, 0);

    if (result > 0)
        build_ssid_list(s_parent_screen);
    else
        lv_label_set_text(lbl_status, "Sin redes encontradas");
}

// ── Callback botón scan ───────────────────────────────────────────────────
static void scan_btn_cb(lv_event_t* /*e*/) {
    if (s_scanning) return;

    close_ssid_list();
    WiFi.scanDelete();
    // async=true, show_hidden=false → no bloquea
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);

    s_scanning = true;
    lv_label_set_text(scan_btn_lbl, LV_SYMBOL_LOOP);
    lv_obj_set_style_text_color(scan_btn_lbl, C_WARN, 0);
    lv_label_set_text(lbl_status, "Buscando redes WiFi...");

    // Comprobar resultado cada 300 ms
    scan_timer = lv_timer_create(scan_poll_cb, 300, nullptr);
}

// ═════════════════════════════════════════════════════════════════════════
// Teclado
// ═════════════════════════════════════════════════════════════════════════
static void kb_event_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_READY ||
        lv_event_get_code(e) == LV_EVENT_CANCEL)
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void ta_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
        if (ta == ta_ssid) close_ssid_list();  // cerrar lista al escribir
    }
    if (code == LV_EVENT_DEFOCUSED)
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void bl_norm_cb(lv_event_t* e) {
    int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    char buf[8]; snprintf(buf, sizeof(buf), "%d%%", v);
    lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), buf);
}
static void bl_red_cb(lv_event_t* e) {
    int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    char buf[8]; snprintf(buf, sizeof(buf), "%d%%", v);
    lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), buf);
}
static void bl_inact_cb(lv_event_t* e) {
    int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    char buf[8]; snprintf(buf, sizeof(buf), "%ds", v * 10);
    lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), buf);
}
static void bl_nstart_cb(lv_event_t* e) {
    int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    char buf[8]; snprintf(buf, sizeof(buf), "%02dh", v);
    lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), buf);
}
static void bl_nend_cb(lv_event_t* e) {
    int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    char buf[8]; snprintf(buf, sizeof(buf), "%02dh", v);
    lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), buf);
}

// ═════════════════════════════════════════════════════════════════════════
// Botón Guardar
// ═════════════════════════════════════════════════════════════════════════
static void save_btn_cb(lv_event_t* /*e*/) {
    close_ssid_list();

    // Cargar config actual ANTES de sobreescribirla para poder comparar
    AppConfig old_cfg{};
    Storage.loadConfig(old_cfg);

    AppConfig cfg{};
    strncpy(cfg.wifi_ssid, lv_textarea_get_text(ta_ssid),          sizeof(cfg.wifi_ssid)-1);
    strncpy(cfg.wifi_pass, lv_textarea_get_text(ta_pass),          sizeof(cfg.wifi_pass)-1);
    strncpy(cfg.logger_ip, lv_textarea_get_text(ta_logger_ip),     sizeof(cfg.logger_ip)-1);
    cfg.logger_serial = (uint32_t)atol(lv_textarea_get_text(ta_logger_serial));

    ChartConfig ccfg{};
    ccfg.autoscale = lv_obj_has_state(cb_autoscale, LV_STATE_CHECKED);
    ccfg.max_kw = (uint8_t)lv_slider_get_value(slider_kw_global);

    TelegramConfig tgcfg{};
    strncpy(tgcfg.token,   lv_textarea_get_text(s_ta_tg_token),
            sizeof(tgcfg.token) - 1);
    strncpy(tgcfg.chat_id, lv_textarea_get_text(s_ta_tg_chatid),
            sizeof(tgcfg.chat_id) - 1);
    tgcfg.batt_threshold = (uint8_t)lv_slider_get_value(s_slider_tg_thr);
    tgcfg.notify_solar   = lv_obj_has_state(s_cb_tg_solar,  LV_STATE_CHECKED);
    tgcfg.notify_grid    = lv_obj_has_state(s_cb_tg_grid,   LV_STATE_CHECKED);
    tgcfg.notify_logger  = lv_obj_has_state(s_cb_tg_logger, LV_STATE_CHECKED);

    BacklightConfig blcfg{};
    blcfg.normal_pct         = (uint8_t)lv_slider_get_value(s_slider_bl_norm);
    blcfg.reduced_pct        = (uint8_t)lv_slider_get_value(s_slider_bl_red);
    blcfg.inactivity_enabled = lv_obj_has_state(s_cb_bl_inact, LV_STATE_CHECKED);
    blcfg.inactivity_div10   = (uint8_t)lv_slider_get_value(s_slider_bl_inact);
    blcfg.night_enabled      = lv_obj_has_state(s_cb_bl_night, LV_STATE_CHECKED);
    blcfg.night_start_h      = (uint8_t)lv_slider_get_value(s_slider_bl_nstart);
    blcfg.night_end_h        = (uint8_t)lv_slider_get_value(s_slider_bl_nend);

    bool needs_restart = (strcmp(cfg.wifi_ssid, old_cfg.wifi_ssid) != 0 ||
                          strcmp(cfg.wifi_pass, old_cfg.wifi_pass) != 0 ||
                          strcmp(cfg.logger_ip, old_cfg.logger_ip) != 0 ||
                          cfg.logger_serial != old_cfg.logger_serial);

    // Guardar todo una sola vez
    Storage.saveConfig(cfg);
    Storage.saveChartConfig(ccfg);
    Storage.saveTelegramConfig(tgcfg);
    Backlight.setConfig(blcfg);  // guarda en NVS y aplica en caliente

    // Aplicar credenciales Telegram en caliente
    Telegram.setCredentials(tgcfg.token, tgcfg.chat_id);

    // Contraseña de acceso web (solo si se escribió algo)
    if (s_ta_admin_pass) {
        const char* ap = lv_textarea_get_text(s_ta_admin_pass);
        if (strlen(ap) > 0) {
            Storage.saveAdminPassword(ap);
            lv_textarea_set_text(s_ta_admin_pass, "");
        }
    }

    if (needs_restart) {
        lv_label_set_text(lbl_status, LV_SYMBOL_OK " Guardado. Reiniciando...");
        lv_obj_set_style_text_color(lbl_status, C_OK, 0);
        lv_timer_handler();
        delay(1200);
        ESP.restart();
    } else {
        lv_label_set_text(lbl_status, LV_SYMBOL_OK " Guardado");
        lv_obj_set_style_text_color(lbl_status, C_OK, 0);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Inicialización
// ═════════════════════════════════════════════════════════════════════════
void config_screen_init(lv_obj_t* parent) {
    s_parent_screen = parent;

    lv_obj_set_style_bg_color(parent, C_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    // ── Scroll vertical habilitado ────────────────────────────────────────
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_style_pad_bottom(parent, 12, 0);

    AppConfig   cfg  = {}; Storage.loadConfig(cfg);
    ChartConfig ccfg = Storage.loadChartConfig();

    // Posiciones Y calculadas en cascada
    const int SEC_WIFI_Y   = SY(30);
    const int SEC_WIFI_H   = CFG_FIELD_H*2 + CFG_SEC_PAD*2 + SY(20);
    const int SEC_INV_Y    = SEC_WIFI_Y  + SEC_WIFI_H  + SY(4);
    const int SEC_INV_H    = SEC_WIFI_H;
    const int SEC_CHART_Y  = SEC_INV_Y  + SEC_INV_H   + SY(4);
    const int SEC_CHART_H  = CFG_FIELD_H + SS(16) + CFG_SEC_PAD*2 + SY(30);
    const int SEC_NET_Y    = SEC_CHART_Y + SEC_CHART_H + SY(4);
    const int SEC_NET_H    = CFG_FIELD_H + SY(20) + SY(20);
    const int SEC_TG_Y     = SEC_NET_Y   + SEC_NET_H   + SY(4);
    const int SEC_TG_H     = CFG_FIELD_H*2 + SS(16) + CFG_SEC_PAD*2 + SY(50) + SY(20);


    // ── Título ────────────────────────────────────────────────────────────
    lv_obj_t* title = lv_label_create(parent);
    lv_obj_set_pos(title, 0, SY(6)); lv_obj_set_width(title, SCREEN_WIDTH);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(title, C_MUTED, 0);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Configuracion");

    // ── Sección WiFi ──────────────────────────────────────────────────────
    lv_obj_t* sec_wifi = make_section(parent, LV_SYMBOL_WIFI "RED WiFi", SEC_WIFI_Y, SEC_WIFI_H);

    make_row_label(sec_wifi, SY(18), "SSID");
    ta_ssid = make_field(sec_wifi, CFG_LBL_W, SY(18), CFG_FIELD_W, false, "Nombre red WiFi");
    lv_textarea_set_text(ta_ssid, cfg.wifi_ssid);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    scan_btn = lv_btn_create(sec_wifi);
    lv_obj_set_pos(scan_btn, CFG_LBL_W + CFG_FIELD_W + SX(4), SY(18));
    lv_obj_set_size(scan_btn, CFG_SCAN_BTN_W, CFG_FIELD_H);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x21262D), 0);
    lv_obj_set_style_border_color(scan_btn, C_ACCENT, 0);
    lv_obj_set_style_border_width(scan_btn, 1, 0);
    lv_obj_set_style_radius(scan_btn, 6, 0);
    lv_obj_set_style_shadow_width(scan_btn, 0, 0);
    lv_obj_add_event_cb(scan_btn, scan_btn_cb, LV_EVENT_CLICKED, nullptr);

    scan_btn_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_btn_lbl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(scan_btn_lbl, C_ACCENT, 0);
    lv_obj_set_style_text_font(scan_btn_lbl, &FONT_NORMAL, 0);
    lv_obj_center(scan_btn_lbl);

    make_row_label(sec_wifi, SY(62), "Contrasena");
    ta_pass = make_field(sec_wifi, CFG_LBL_W, SY(62),
                         CFG_FIELD_W + CFG_SCAN_BTN_W + SX(4), true, "Contrasena WiFi");
    lv_textarea_set_text(ta_pass, cfg.wifi_pass);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    // ── Sección Inversor ──────────────────────────────────────────────────
    lv_obj_t* sec_inv = make_section(parent, LV_SYMBOL_EDIT " INVERSOR / DATALOGGER", SEC_INV_Y, SEC_INV_H);

    make_row_label(sec_inv, SY(18), "IP Logger");
    ta_logger_ip = make_field(sec_inv, CFG_LBL_W, SY(18),
                              CFG_FIELD_W + CFG_SCAN_BTN_W + SX(4), false, "192.168.1.xxx");
    lv_textarea_set_text(ta_logger_ip, cfg.logger_ip);
    lv_obj_add_event_cb(ta_logger_ip, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_logger_ip, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    make_row_label(sec_inv, SY(62), "Num. Serie");
    ta_logger_serial = make_field(sec_inv, CFG_LBL_W, SY(62),
                                  CFG_FIELD_W + CFG_SCAN_BTN_W + SX(4), false, "Decimal (etiqueta)");
    char sbuf[16];
    snprintf(sbuf, sizeof(sbuf), "%lu", (unsigned long)cfg.logger_serial);
    lv_textarea_set_text(ta_logger_serial, sbuf);
    lv_obj_add_event_cb(ta_logger_serial, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_logger_serial, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    // ── Sección Gráfica ───────────────────────────────────────────────────────
    // h=100: fila checkbox(16+10) + fila label/valor(20) + fila slider(24) + padding
    lv_obj_t* sec_chart = make_section(parent, LV_SYMBOL_CHARGE " GRAFICA", SEC_CHART_Y, SEC_CHART_H);

    // Fila 1: checkbox autoescalado  (y=16)
    cb_autoscale = lv_checkbox_create(sec_chart);
    lv_obj_set_pos(cb_autoscale, 0, SY(16));
    lv_checkbox_set_text(cb_autoscale, "Autoescalado");
    lv_obj_set_style_text_font(cb_autoscale, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(cb_autoscale, C_WHITE, 0);
    if (ccfg.autoscale) lv_obj_add_state(cb_autoscale, LV_STATE_CHECKED);

    // Fila 2: "Max kW:" a la izquierda, valor numérico a la derecha  (y=44)
    lv_obj_t* lkw = lv_label_create(sec_chart);
    lv_obj_set_pos(lkw, 0, SY(46));
    lv_obj_set_style_text_font(lkw, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lkw, C_MUTED, 0);
    lv_label_set_text(lkw, "Max kW:");

    lv_obj_t* CFG_LBL_kw_val = lv_label_create(sec_chart);
    lv_obj_set_pos(CFG_LBL_kw_val, SX(70), SY(46));
    lv_obj_set_style_text_font(CFG_LBL_kw_val, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(CFG_LBL_kw_val, C_WHITE, 0);
    char kwbuf[8];
    snprintf(kwbuf, sizeof(kwbuf), "%d kW", ccfg.max_kw);
    lv_label_set_text(CFG_LBL_kw_val, kwbuf);

    // Fila 3: slider ocupando todo el ancho menos márgenes  (y=66)
    // Margen derecho de 16px para que el knob no sobresalga nunca
    lv_obj_t* slider_kw = lv_slider_create(sec_chart);
    lv_obj_set_pos(slider_kw, 0, SY(68));
    lv_obj_set_size(slider_kw, CFG_SECTION_W - CFG_SEC_PAD * 2 - SX(16), SY(16));
    lv_slider_set_range(slider_kw, 1, 20);
    lv_slider_set_value(slider_kw, ccfg.max_kw, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_kw, C_BTN,                   LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_kw, lv_color_hex(0x21262D),  LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_kw, C_WHITE,                 LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_kw,  4,                       LV_PART_KNOB);

    slider_kw_global = slider_kw;

    lv_obj_add_event_cb(slider_kw, [](lv_event_t* e) {
        lv_obj_t* sl  = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* lbl = (lv_obj_t*)lv_event_get_user_data(e);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d kW", (int)lv_slider_get_value(sl));
        lv_label_set_text(lbl, buf);
    }, LV_EVENT_VALUE_CHANGED, CFG_LBL_kw_val);

    // ── Sección Estado red  (baja a 358 = 254+100+4) ──────────────────────────
    lv_obj_t* sec_net = make_section(parent, LV_SYMBOL_GPS " ESTADO RED", SEC_NET_Y, SEC_NET_H);
    lbl_ip   = make_info_row(sec_net, SY(18), "IP ESP32");
    lbl_rssi = make_info_row(sec_net, SY(38), "Senal WiFi");

    // ── Sección Telegram (y=432, h=178) ───────────────────────────────────
    TelegramConfig tgcfg = Storage.loadTelegramConfig();

    lv_obj_t* sec_tg = make_section(parent, LV_SYMBOL_BELL " TELEGRAM", SEC_TG_Y, SEC_TG_H);

    // Token
    make_row_label(sec_tg, SY(18), "Bot Token");
    lv_obj_t* ta_token = make_field(sec_tg, CFG_LBL_W, SY(18),
                                     CFG_SECTION_W - CFG_LBL_W - CFG_SEC_PAD, false, "123456:ABC...");
    lv_textarea_set_text(ta_token, tgcfg.token);
    lv_obj_add_event_cb(ta_token, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_token, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    // Chat ID
    make_row_label(sec_tg, 62, "Chat ID");
    lv_obj_t* ta_chatid = make_field(sec_tg, CFG_LBL_W, SY(62),
                                      CFG_FIELD_W + CFG_SCAN_BTN_W + SX(4), false, "-100123456789");
    lv_textarea_set_text(ta_chatid, tgcfg.chat_id);
    lv_obj_add_event_cb(ta_chatid, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_chatid, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    // Umbral batería
    lv_obj_t* CFG_LBL_thr = lv_label_create(sec_tg);
    lv_obj_set_pos(CFG_LBL_thr, 0, SY(106));
    lv_obj_set_style_text_font(CFG_LBL_thr, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(CFG_LBL_thr, C_MUTED, 0);
    lv_label_set_text(CFG_LBL_thr, "Alerta bat.:");

    lv_obj_t* CFG_LBL_thr_val = lv_label_create(sec_tg);
    lv_obj_set_pos(CFG_LBL_thr_val, SX(90), SY(106));
    lv_obj_set_style_text_font(CFG_LBL_thr_val, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(CFG_LBL_thr_val, C_WHITE, 0);
    char thrbuf[8];
    snprintf(thrbuf, sizeof(thrbuf), "%d%%", tgcfg.batt_threshold);
    lv_label_set_text(CFG_LBL_thr_val, thrbuf);

    lv_obj_t* slider_thr = lv_slider_create(sec_tg);
    lv_obj_set_pos(slider_thr, SX(134), SY(110));
    lv_obj_set_size(slider_thr, CFG_SECTION_W - SX(150), SY(16));
    lv_slider_set_range(slider_thr, 5, 50);
    lv_slider_set_value(slider_thr, tgcfg.batt_threshold, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_thr, C_BTN,                  LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_thr, lv_color_hex(0x21262D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_thr, C_WHITE,                LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_thr, 4,                       LV_PART_KNOB);
    lv_obj_add_event_cb(slider_thr, [](lv_event_t* e) {
        lv_obj_t* sl  = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* lbl = (lv_obj_t*)lv_event_get_user_data(e);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(sl));
        lv_label_set_text(lbl, buf);
    }, LV_EVENT_VALUE_CHANGED, CFG_LBL_thr_val);

    // Checkboxes de alertas
    lv_obj_t* cb_solar = lv_checkbox_create(sec_tg);
    lv_obj_set_pos(cb_solar, 0, SY(132));
    lv_checkbox_set_text(cb_solar, "Solar");
    lv_obj_set_style_text_font(cb_solar, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(cb_solar, C_WHITE, 0);
    if (tgcfg.notify_solar) lv_obj_add_state(cb_solar, LV_STATE_CHECKED);

    lv_obj_t* cb_grid = lv_checkbox_create(sec_tg);
    lv_obj_set_pos(cb_grid, SX(90), SY(132));
    lv_checkbox_set_text(cb_grid, "Red");
    lv_obj_set_style_text_font(cb_grid, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(cb_grid, C_WHITE, 0);
    if (tgcfg.notify_grid) lv_obj_add_state(cb_grid, LV_STATE_CHECKED);

    lv_obj_t* cb_logger = lv_checkbox_create(sec_tg);
    lv_obj_set_pos(cb_logger, SX(165), SY(132));
    lv_checkbox_set_text(cb_logger, "Logger");
    lv_obj_set_style_text_font(cb_logger, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(cb_logger, C_WHITE, 0);
    if (tgcfg.notify_logger) lv_obj_add_state(cb_logger, LV_STATE_CHECKED);

    // Botón de prueba
    lv_obj_t* btn_test = lv_btn_create(sec_tg);
    lv_obj_set_pos(btn_test, CFG_SECTION_W - CFG_SEC_PAD*2 - SX(110), SY(128));
    lv_obj_set_size(btn_test, SX(110), SY(30));
    lv_obj_set_style_bg_color(btn_test, lv_color_hex(0x2D7D46), 0);
    lv_obj_set_style_radius(btn_test, 6, 0);
    lv_obj_add_event_cb(btn_test, [](lv_event_t*) {
        Telegram.enqueueAlert(AlertType::TEST);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* CFG_LBL_test = lv_label_create(btn_test);
    lv_label_set_text(CFG_LBL_test, LV_SYMBOL_BELL " Probar");
    lv_obj_set_style_text_font(CFG_LBL_test, &FONT_SMALL, 0);
    lv_obj_center(CFG_LBL_test);

    // Guardar referencias para save_btn_cb
    // (añadir como static al principio del fichero)
    s_ta_tg_token   = ta_token;
    s_ta_tg_chatid  = ta_chatid;
    s_slider_tg_thr = slider_thr;
    s_cb_tg_solar   = cb_solar;
    s_cb_tg_grid    = cb_grid;
    s_cb_tg_logger  = cb_logger;

    // ── Sección PANTALLA ──────────────────────────────────────────────────
    BacklightConfig blcfg = Storage.loadBacklightConfig();

    // Altura: pad×2(20) + título(14) + 5 filas×(SS16+6) + 4 checkboxes(22×2)
    // = 20 + 14 + 5×22 + 2×22 = 20+14+110+44 = 188 → redondeamos a 190
    const int SEC_BL_Y    = SEC_TG_Y + SEC_TG_H + SY(4);
    const int SEC_BL_H    = SY(190);
    const int SEC_ADMIN_Y = SEC_BL_Y + SEC_BL_H + SY(4);
    const int SEC_ADMIN_H = CFG_FIELD_H + CFG_SEC_PAD*2 + SY(50);
    const int BTN_Y       = SEC_ADMIN_Y + SEC_ADMIN_H + SY(8);

    lv_obj_t* sec_bl = make_section(parent,
                        LV_SYMBOL_EYE_OPEN " PANTALLA", SEC_BL_Y, SEC_BL_H);

    // Fila 1: Brillo normal
    const int R1 = SY(18);
    s_slider_bl_norm = make_slider_row(sec_bl, R1,
        "Brillo normal:", 10, 100, blcfg.normal_pct,
        &s_lbl_bl_norm, "%", bl_norm_cb);

    // Fila 2: Brillo reducido
    const int R2 = R1 + SS(16) + SY(10);
    s_slider_bl_red = make_slider_row(sec_bl, R2,
        "Brillo reducido:", 0, 100, blcfg.reduced_pct,
        &s_lbl_bl_red, "%", bl_red_cb);

    // Fila 3: Checkbox inactividad + slider segundos
    const int R3 = R2 + SS(16) + SY(10);
    s_cb_bl_inact = lv_checkbox_create(sec_bl);
    lv_obj_set_pos(s_cb_bl_inact, 0, R3);
    lv_checkbox_set_text(s_cb_bl_inact, "Reducir con inactividad");
    lv_obj_set_style_text_font(s_cb_bl_inact, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_cb_bl_inact, C_WHITE, 0);
    if (blcfg.inactivity_enabled) lv_obj_add_state(s_cb_bl_inact, LV_STATE_CHECKED);

    const int R3B = R3 + SY(22);
    s_slider_bl_inact = make_slider_row(sec_bl, R3B,
        "Espera:", 1, 18, blcfg.inactivity_div10,
        &s_lbl_bl_inact, "s", bl_inact_cb);
    // Corregir el label inicial (muestra div10 * 10)
    char ibuf[8];
    snprintf(ibuf, sizeof(ibuf), "%ds", blcfg.inactivity_div10 * 10);
    lv_label_set_text(s_lbl_bl_inact, ibuf);

    // Fila 4: Checkbox horario nocturno
    const int R4 = R3B + SS(16) + SY(10);
    s_cb_bl_night = lv_checkbox_create(sec_bl);
    lv_obj_set_pos(s_cb_bl_night, 0, R4);
    lv_checkbox_set_text(s_cb_bl_night, "Horario nocturno");
    lv_obj_set_style_text_font(s_cb_bl_night, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_cb_bl_night, C_WHITE, 0);
    if (blcfg.night_enabled) lv_obj_add_state(s_cb_bl_night, LV_STATE_CHECKED);

    // Fila 5: Inicio y fin del horario
    const int R5 = R4 + SY(22);

    // Inicio
    lv_obj_t* lbl_ns = lv_label_create(sec_bl);
    lv_obj_set_pos(lbl_ns, 0, R5 + SY(4));
    lv_obj_set_style_text_font(lbl_ns, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_ns, C_MUTED, 0);
    lv_label_set_text(lbl_ns, "Inicio:");

    s_lbl_bl_nstart = lv_label_create(sec_bl);
    lv_obj_set_pos(s_lbl_bl_nstart, CFG_LBL_W, R5 + SY(4));
    lv_obj_set_style_text_font(s_lbl_bl_nstart, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_lbl_bl_nstart, C_WHITE, 0);
    char nsbuf[8]; snprintf(nsbuf, sizeof(nsbuf), "%02dh", blcfg.night_start_h);
    lv_label_set_text(s_lbl_bl_nstart, nsbuf);

    const int HALF_W = (CFG_SECTION_W - CFG_SEC_PAD*2 - CFG_LBL_W - SX(8)) / 2;
    s_slider_bl_nstart = lv_slider_create(sec_bl);
    lv_obj_set_pos(s_slider_bl_nstart, CFG_LBL_W + SX(44), R5 + SY(2));
    lv_obj_set_size(s_slider_bl_nstart, HALF_W - SX(4), SS(16));
    lv_slider_set_range(s_slider_bl_nstart, 0, 23);
    lv_slider_set_value(s_slider_bl_nstart, blcfg.night_start_h, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_bl_nstart, C_BTN,                  LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_bl_nstart, lv_color_hex(0x21262D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider_bl_nstart, C_WHITE,                LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider_bl_nstart, SX(4),                   LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider_bl_nstart, bl_nstart_cb,
                         LV_EVENT_VALUE_CHANGED, s_lbl_bl_nstart);

    // Fin
    int x_fin = CFG_LBL_W + SX(44) + HALF_W + SX(4);

    lv_obj_t* lbl_ne = lv_label_create(sec_bl);
    lv_obj_set_pos(lbl_ne, x_fin, R5 + SY(4));
    lv_obj_set_style_text_font(lbl_ne, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_ne, C_MUTED, 0);
    lv_label_set_text(lbl_ne, "Fin:");

    s_lbl_bl_nend = lv_label_create(sec_bl);
    lv_obj_set_pos(s_lbl_bl_nend, x_fin + SX(28), R5 + SY(4));
    lv_obj_set_style_text_font(s_lbl_bl_nend, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_lbl_bl_nend, C_WHITE, 0);
    char nebuf[8]; snprintf(nebuf, sizeof(nebuf), "%02dh", blcfg.night_end_h);
    lv_label_set_text(s_lbl_bl_nend, nebuf);

    s_slider_bl_nend = lv_slider_create(sec_bl);
    lv_obj_set_pos(s_slider_bl_nend, x_fin + SX(44), R5 + SY(2));
    lv_obj_set_size(s_slider_bl_nend, HALF_W - SX(44), SS(16));
    lv_slider_set_range(s_slider_bl_nend, 0, 23);
    lv_slider_set_value(s_slider_bl_nend, blcfg.night_end_h, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_slider_bl_nend, C_BTN,                  LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_bl_nend, lv_color_hex(0x21262D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider_bl_nend, C_WHITE,                LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider_bl_nend, SX(4),                   LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider_bl_nend, bl_nend_cb,
                         LV_EVENT_VALUE_CHANGED, s_lbl_bl_nend);

    // ── Sección Acceso Web ────────────────────────────────────────────────────
    {
        String pw_status = Storage.loadAdminPassword().isEmpty()
            ? "Sin proteccion (acceso libre)"
            : "Contrasena configurada";

        lv_obj_t* sec_admin = make_section(parent,
            LV_SYMBOL_WARNING " ACCESO WEB", SEC_ADMIN_Y, SEC_ADMIN_H);

        lv_obj_t* lbl_pw_st = lv_label_create(sec_admin);
        lv_obj_set_pos(lbl_pw_st, 0, SY(18));
        lv_obj_set_style_text_font(lbl_pw_st, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl_pw_st,
            Storage.loadAdminPassword().isEmpty() ? C_WARN : C_OK, 0);
        lv_label_set_text(lbl_pw_st, pw_status.c_str());

        make_row_label(sec_admin, SY(40), "Nueva clave");
        s_ta_admin_pass = make_field(sec_admin, CFG_LBL_W, SY(40),
            CFG_SECTION_W - CFG_LBL_W - CFG_SEC_PAD*2, true, "Nueva contrasena web...");
        lv_obj_add_event_cb(s_ta_admin_pass, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
        lv_obj_add_event_cb(s_ta_admin_pass, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);
    }

    // ── Botón Guardar + status  (bajan acordes) ───────────────────────────────
    lbl_status = lv_label_create(parent);
    lv_obj_set_pos(lbl_status, SX(10),  BTN_Y + SY(4));
    lv_obj_set_width(lbl_status, 310);
    lv_obj_set_style_text_font(lbl_status, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_status, C_MUTED, 0);
    lv_label_set_text(lbl_status, "");

    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_pos(btn,        SCREEN_WIDTH - SX(140), BTN_Y);
    lv_obj_set_size(btn,       SX(130), CFG_FIELD_H);
    lv_obj_set_style_bg_color(btn, C_BTN, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_add_event_cb(btn, save_btn_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* blbl = lv_label_create(btn);
    lv_label_set_text(blbl, LV_SYMBOL_SAVE " Guardar");
    lv_obj_set_style_text_font(blbl, &FONT_NORMAL, 0);
    lv_obj_center(blbl);

    // ── Teclado ────────────────────────────────────────────────────────────
    // El teclado se ancla a la pantalla raíz para no desplazarse con el scroll
    kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(kb, SCREEN_WIDTH, SY(140));
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY,  nullptr);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, nullptr);
}

// ═════════════════════════════════════════════════════════════════════════
// Refresco periódico de red
// ═════════════════════════════════════════════════════════════════════════
void config_screen_tick() {
    if (millis() - s_last_net_refresh < 5000) return;
    s_last_net_refresh = millis();

    if (WiFi.status() == WL_CONNECTED) {
        lv_label_set_text(lbl_ip, WiFi.localIP().toString().c_str());
        int32_t rssi = WiFi.RSSI();
        const char* qual = rssi > -60 ? "Buena" : rssi > -75 ? "Regular" : "Debil";
        char buf[32]; snprintf(buf, sizeof(buf), "%s (%d dBm)", qual, (int)rssi);
        lv_label_set_text(lbl_rssi, buf);
        lv_obj_set_style_text_color(lbl_rssi, rssi_color(rssi), 0);
    } else {
        lv_label_set_text(lbl_ip,   "Sin conexion");
        lv_label_set_text(lbl_rssi, "--");
        lv_obj_set_style_text_color(lbl_rssi, C_ERR, 0);
    }
}