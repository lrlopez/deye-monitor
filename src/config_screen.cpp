#include "config_screen.h"
#include "storage.h"
#include <WiFi.h>

// ── Paleta ────────────────────────────────────────────────────────────────
#define C_BG    lv_color_hex(0x0D1117)
#define C_CARD  lv_color_hex(0x161B22)
#define C_ACCENT lv_color_hex(0x4A9EFF)
#define C_WHITE lv_color_hex(0xEAEAEA)
#define C_MUTED lv_color_hex(0x6E7681)
#define C_OK    lv_color_hex(0x2ECC71)
#define C_ERR   lv_color_hex(0xE74C3C)
#define C_WARN  lv_color_hex(0xF5C518)
#define C_BTN   lv_color_hex(0x1F6FEB)
#define C_LIST  lv_color_hex(0x1C2128)

// ── Geometría ─────────────────────────────────────────────────────────────
#define SECTION_W  460
#define FIELD_H     36
#define FIELD_W    220    // reducido para dejar sitio al botón scan
#define LBL_W      130
#define ROW_H       42
#define PAD          8
#define SEC_PAD     10
#define SCAN_BTN_W  34

// Posición absoluta del campo SSID en pantalla (para anclar el dropdown)
// sec_wifi está en y=28, campo SSID en y=18 dentro de la sección
// 28 (sec y) + SEC_PAD(10) + 18 (row y) + FIELD_H(36) + 2 = 94
#define SSID_DROPDOWN_Y  94
#define SSID_DROPDOWN_X  (10 + SEC_PAD + LBL_W)   // ≈ 150
#define SSID_DROPDOWN_W  (FIELD_W + SCAN_BTN_W + 4)
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
    lv_obj_set_size(card, SECTION_W, h);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, C_ACCENT, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, SEC_PAD, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(card);
    lv_obj_set_pos(lbl, 0, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, C_ACCENT, 0);
    lv_label_set_text(lbl, title);
    return card;
}

static lv_obj_t* make_field(lv_obj_t* parent, int x, int y, int w,
                              bool password, const char* placeholder) {
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_set_pos(ta, x, y);
    lv_obj_set_size(ta, w, FIELD_H);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    if (password) lv_textarea_set_password_mode(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x21262D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, C_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, C_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ta, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ta, 6, LV_PART_MAIN);
    return ta;
}

static lv_obj_t* make_row_label(lv_obj_t* parent, int y, const char* txt) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_pos(l, 0, y + 10);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, C_MUTED, 0);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t* make_info_row(lv_obj_t* parent, int y, const char* key) {
    lv_obj_t* lk = lv_label_create(parent);
    lv_obj_set_pos(lk, 0, y);
    lv_obj_set_style_text_font(lk, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lk, C_MUTED, 0);
    lv_label_set_text(lk, key);

    lv_obj_t* lv2 = lv_label_create(parent);
    lv_obj_set_pos(lv2, LBL_W, y);
    lv_obj_set_style_text_font(lv2, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lv2, C_WHITE, 0);
    lv_label_set_text(lv2, "...");
    return lv2;
}

// ── Icono de señal WiFi según RSSI ─────────────────────────────────────────
static const char* rssi_icon(int rssi) {
    if (rssi >= -55) return LV_SYMBOL_WIFI;          // 3 barras
    if (rssi >= -70) return LV_SYMBOL_WIFI;          // LVGL no tiene iconos parciales
    return LV_SYMBOL_WIFI;                            // usamos color para diferenciar
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
        lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_12, 0);
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
            lv_obj_set_style_text_font(item_lbl, &lv_font_montserrat_12, 0);
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

// ═════════════════════════════════════════════════════════════════════════
// Botón Guardar
// ═════════════════════════════════════════════════════════════════════════
static void save_btn_cb(lv_event_t* /*e*/) {
    close_ssid_list();

    AppConfig cfg;
    strncpy(cfg.wifi_ssid, lv_textarea_get_text(ta_ssid),          sizeof(cfg.wifi_ssid)-1);
    strncpy(cfg.wifi_pass, lv_textarea_get_text(ta_pass),          sizeof(cfg.wifi_pass)-1);
    strncpy(cfg.logger_ip, lv_textarea_get_text(ta_logger_ip),     sizeof(cfg.logger_ip)-1);
    cfg.logger_serial = (uint32_t)atol(lv_textarea_get_text(ta_logger_serial));

    ChartConfig ccfg;
    ccfg.autoscale = lv_obj_has_state(cb_autoscale, LV_STATE_CHECKED);
    ccfg.max_kw = (uint8_t)lv_slider_get_value(slider_kw_global);

    Storage.saveConfig(cfg);
    Storage.saveChartConfig(ccfg);

    lv_label_set_text(lbl_status, LV_SYMBOL_OK " Guardado. Reiniciando...");
    lv_obj_set_style_text_color(lbl_status, C_OK, 0);
    lv_timer_handler();

    delay(1200);
    ESP.restart();
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

    // ── Posiciones Y apiladas sin solapamiento ────────────────────────────
    // Título         y=6    h=20  → bottom=26
    // sec_wifi       y=30   h=108 → bottom=138
    // sec_inv        y=142  h=108 → bottom=250
    // sec_chart      y=254  h=70  → bottom=324
    // sec_net        y=328  h=58  → bottom=386
    // lbl_status     y=392
    // btn Guardar    y=390
    // (contenido total ~434px, scrolleable)

    // ── Título ────────────────────────────────────────────────────────────
    lv_obj_t* title = lv_label_create(parent);
    lv_obj_set_pos(title, 0, 6); lv_obj_set_width(title, 480);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, C_MUTED, 0);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Configuracion");

    // ── Sección WiFi ──────────────────────────────────────────────────────
    lv_obj_t* sec_wifi = make_section(parent, LV_SYMBOL_WIFI "RED WiFi", 30, 108);

    make_row_label(sec_wifi, 18, "SSID");
    ta_ssid = make_field(sec_wifi, LBL_W, 18, FIELD_W, false, "Nombre red WiFi");
    lv_textarea_set_text(ta_ssid, cfg.wifi_ssid);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    scan_btn = lv_btn_create(sec_wifi);
    lv_obj_set_pos(scan_btn, LBL_W + FIELD_W + 4, 18);
    lv_obj_set_size(scan_btn, SCAN_BTN_W, FIELD_H);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x21262D), 0);
    lv_obj_set_style_border_color(scan_btn, C_ACCENT, 0);
    lv_obj_set_style_border_width(scan_btn, 1, 0);
    lv_obj_set_style_radius(scan_btn, 6, 0);
    lv_obj_set_style_shadow_width(scan_btn, 0, 0);
    lv_obj_add_event_cb(scan_btn, scan_btn_cb, LV_EVENT_CLICKED, nullptr);

    scan_btn_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_btn_lbl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(scan_btn_lbl, C_ACCENT, 0);
    lv_obj_set_style_text_font(scan_btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(scan_btn_lbl);

    make_row_label(sec_wifi, 62, "Contrasena");
    ta_pass = make_field(sec_wifi, LBL_W, 62,
                         FIELD_W + SCAN_BTN_W + 4, true, "Contrasena WiFi");
    lv_textarea_set_text(ta_pass, cfg.wifi_pass);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    // ── Sección Inversor ──────────────────────────────────────────────────
    lv_obj_t* sec_inv = make_section(parent, LV_SYMBOL_EDIT " INVERSOR / DATALOGGER", 142, 108);

    make_row_label(sec_inv, 18, "IP Logger");
    ta_logger_ip = make_field(sec_inv, LBL_W, 18,
                              FIELD_W + SCAN_BTN_W + 4, false, "192.168.1.xxx");
    lv_textarea_set_text(ta_logger_ip, cfg.logger_ip);
    lv_obj_add_event_cb(ta_logger_ip, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_logger_ip, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    make_row_label(sec_inv, 62, "Num. Serie");
    ta_logger_serial = make_field(sec_inv, LBL_W, 62,
                                  FIELD_W + SCAN_BTN_W + 4, false, "Decimal (etiqueta)");
    char sbuf[16];
    snprintf(sbuf, sizeof(sbuf), "%lu", (unsigned long)cfg.logger_serial);
    lv_textarea_set_text(ta_logger_serial, sbuf);
    lv_obj_add_event_cb(ta_logger_serial, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_logger_serial, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    // ── Sección Gráfica ───────────────────────────────────────────────────────
    // h=100: fila checkbox(16+10) + fila label/valor(20) + fila slider(24) + padding
    lv_obj_t* sec_chart = make_section(parent, LV_SYMBOL_CHARGE " GRAFICA", 254, 100);

    // Fila 1: checkbox autoescalado  (y=16)
    cb_autoscale = lv_checkbox_create(sec_chart);
    lv_obj_set_pos(cb_autoscale, 0, 16);
    lv_checkbox_set_text(cb_autoscale, "Autoescalado");
    lv_obj_set_style_text_font(cb_autoscale, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cb_autoscale, C_WHITE, 0);
    if (ccfg.autoscale) lv_obj_add_state(cb_autoscale, LV_STATE_CHECKED);

    // Fila 2: "Max kW:" a la izquierda, valor numérico a la derecha  (y=44)
    lv_obj_t* lkw = lv_label_create(sec_chart);
    lv_obj_set_pos(lkw, 0, 46);
    lv_obj_set_style_text_font(lkw, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lkw, C_MUTED, 0);
    lv_label_set_text(lkw, "Max kW:");

    lv_obj_t* lbl_kw_val = lv_label_create(sec_chart);
    lv_obj_set_pos(lbl_kw_val, 70, 46);
    lv_obj_set_style_text_font(lbl_kw_val, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_kw_val, C_WHITE, 0);
    char kwbuf[8];
    snprintf(kwbuf, sizeof(kwbuf), "%d kW", ccfg.max_kw);
    lv_label_set_text(lbl_kw_val, kwbuf);

    // Fila 3: slider ocupando todo el ancho menos márgenes  (y=66)
    // Margen derecho de 16px para que el knob no sobresalga nunca
    lv_obj_t* slider_kw = lv_slider_create(sec_chart);
    lv_obj_set_pos(slider_kw, 0, 68);
    lv_obj_set_size(slider_kw, SECTION_W - SEC_PAD * 2 - 16, 16);
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
    }, LV_EVENT_VALUE_CHANGED, lbl_kw_val);

    // ── Sección Estado red  (baja a 358 = 254+100+4) ──────────────────────────
    lv_obj_t* sec_net = make_section(parent, LV_SYMBOL_GPS " ESTADO RED", 358, 70);
    lbl_ip   = make_info_row(sec_net, 18, "IP ESP32");
    lbl_rssi = make_info_row(sec_net, 38, "Senal WiFi");

    // ── Botón Guardar + status  (bajan acordes) ───────────────────────────────
    lbl_status = lv_label_create(parent);
    lv_obj_set_pos(lbl_status, 10, 438);
    lv_obj_set_width(lbl_status, 310);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_status, C_MUTED, 0);
    lv_label_set_text(lbl_status, "");

    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, 340, 432);
    lv_obj_set_size(btn, 130, 36);
    lv_obj_set_style_bg_color(btn, C_BTN, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_add_event_cb(btn, save_btn_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* blbl = lv_label_create(btn);
    lv_label_set_text(blbl, LV_SYMBOL_SAVE " Guardar");
    lv_obj_set_style_text_font(blbl, &lv_font_montserrat_14, 0);
    lv_obj_center(blbl);

    // ── Teclado ────────────────────────────────────────────────────────────
    // El teclado se ancla a la pantalla raíz para no desplazarse con el scroll
    kb = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(kb, 480, 140);
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