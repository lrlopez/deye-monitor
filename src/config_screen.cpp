#include "config_screen.h"
#include "storage.h"
#include <WiFi.h>

// ── Paleta (coherente con el resto) ───────────────────────────────────────
#define C_BG        lv_color_hex(0x0D1117)
#define C_CARD      lv_color_hex(0x161B22)
#define C_ACCENT    lv_color_hex(0x4A9EFF)
#define C_WHITE     lv_color_hex(0xEAEAEA)
#define C_MUTED     lv_color_hex(0x6E7681)
#define C_OK        lv_color_hex(0x2ECC71)
#define C_ERR       lv_color_hex(0xE74C3C)
#define C_WARN      lv_color_hex(0xF5C518)
#define C_BTN       lv_color_hex(0x1F6FEB)

// ── Geometría ─────────────────────────────────────────────────────────────
#define SECTION_W   460
#define FIELD_H      36
#define FIELD_W     260
#define LBL_W       130
#define ROW_H        42
#define PAD           8
#define SEC_PAD      10

// ── Widgets que necesitan actualización ───────────────────────────────────
static lv_obj_t* ta_ssid;
static lv_obj_t* ta_pass;
static lv_obj_t* ta_logger_ip;
static lv_obj_t* ta_logger_serial;
static lv_obj_t* lbl_ip;
static lv_obj_t* lbl_rssi;
static lv_obj_t* lbl_status;
static lv_obj_t* kb;

static uint32_t  s_last_net_refresh = 0;

// ── Helpers ───────────────────────────────────────────────────────────────
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

    lv_obj_t* lbl = lv_label_create(card);
    lv_obj_set_pos(lbl, 0, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, C_ACCENT, 0);
    lv_label_set_text(lbl, title);

    return card;
}

static lv_obj_t* make_field_row(lv_obj_t* parent, int y,
                                 const char* label_txt,
                                 bool is_password,
                                 const char* placeholder) {
    // Etiqueta
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, 0, y + 10);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, C_MUTED, 0);
    lv_label_set_text(lbl, label_txt);

    // Textarea
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_set_pos(ta, LBL_W, y);
    lv_obj_set_size(ta, FIELD_W, FIELD_H);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    if (is_password) lv_textarea_set_password_mode(ta, true);
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

static lv_obj_t* make_info_row(lv_obj_t* parent, int y, const char* label_txt) {
    lv_obj_t* lbl_key = lv_label_create(parent);
    lv_obj_set_pos(lbl_key, 0, y);
    lv_obj_set_style_text_font(lbl_key, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_key, C_MUTED, 0);
    lv_label_set_text(lbl_key, label_txt);

    lv_obj_t* lbl_val = lv_label_create(parent);
    lv_obj_set_pos(lbl_val, LBL_W, y);
    lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_val, C_WHITE, 0);
    lv_label_set_text(lbl_val, "...");
    return lbl_val;
}

// ── Callback del teclado ──────────────────────────────────────────────────
static void kb_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ta_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Callback botón Guardar ────────────────────────────────────────────────
static void save_btn_cb(lv_event_t* /*e*/) {
    AppConfig cfg;

    // Leer textareas
    strncpy(cfg.wifi_ssid,  lv_textarea_get_text(ta_ssid),          sizeof(cfg.wifi_ssid) - 1);
    strncpy(cfg.wifi_pass,  lv_textarea_get_text(ta_pass),          sizeof(cfg.wifi_pass) - 1);
    strncpy(cfg.logger_ip,  lv_textarea_get_text(ta_logger_ip),     sizeof(cfg.logger_ip) - 1);
    cfg.logger_serial = (uint32_t)atol(lv_textarea_get_text(ta_logger_serial));

    Storage.saveConfig(cfg);

    lv_label_set_text(lbl_status, LV_SYMBOL_OK " Guardado. Reiniciando...");
    lv_obj_set_style_text_color(lbl_status, C_OK, 0);
    lv_timer_handler();   // forzar repintado antes de reiniciar

    delay(1200);
    ESP.restart();
}

// ── Inicialización ────────────────────────────────────────────────────────
void config_screen_init(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, C_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    AppConfig cfg;
    Storage.loadConfig(cfg);

    // ── Título ────────────────────────────────────────────────────────────
    lv_obj_t* title = lv_label_create(parent);
    lv_obj_set_pos(title, 0, 6);
    lv_obj_set_width(title, 480);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, C_MUTED, 0);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Configuracion");

    // ── Sección WiFi (y=28, h=106) ────────────────────────────────────────
    lv_obj_t* sec_wifi = make_section(parent, "RED WiFi", 28, 106);

    ta_ssid = make_field_row(sec_wifi, 18,  "SSID",       false, "Nombre red WiFi");
    ta_pass = make_field_row(sec_wifi, 62,  "Contrasena", true,  "Contrasena WiFi");

    lv_textarea_set_text(ta_ssid, cfg.wifi_ssid);
    lv_textarea_set_text(ta_pass, cfg.wifi_pass);

    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    // ── Sección Inversor (y=142, h=106) ───────────────────────────────────
    lv_obj_t* sec_inv = make_section(parent, "INVERSOR / DATALOGGER", 142, 106);

    ta_logger_ip     = make_field_row(sec_inv, 18, "IP Logger",  false, "192.168.1.xxx");
    ta_logger_serial = make_field_row(sec_inv, 62, "Num. Serie", false, "Decimal (etiqueta)");

    lv_textarea_set_text(ta_logger_ip, cfg.logger_ip);
    char serial_buf[16];
    snprintf(serial_buf, sizeof(serial_buf), "%lu", (unsigned long)cfg.logger_serial);
    lv_textarea_set_text(ta_logger_serial, serial_buf);

    lv_obj_add_event_cb(ta_logger_ip,     ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_logger_ip,     ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_add_event_cb(ta_logger_serial, ta_event_cb, LV_EVENT_FOCUSED,   nullptr);
    lv_obj_add_event_cb(ta_logger_serial, ta_event_cb, LV_EVENT_DEFOCUSED, nullptr);

    // ── Sección Estado de red (y=256, h=60) ───────────────────────────────
    // Queda debajo del área visible → se verá al hacer scroll si el teclado
    // no está activo; si prefieres, sube todo el layout 20px.
    // Optamos por ponerla en el margen inferior visible sin teclado.
    lv_obj_t* sec_net = make_section(parent, "ESTADO RED", 256, 60);  // fuera; ver nota

    lbl_ip   = make_info_row(sec_net,  18, "IP ESP32");
    lbl_rssi = make_info_row(sec_net,  36, "Señal WiFi");

    // ── Botón Guardar + label estado ──────────────────────────────────────
    // Fijo en la parte derecha fuera de las secciones para que siempre
    // sea visible sin teclado
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, 340, 230);
    lv_obj_set_size(btn, 130, 36);
    lv_obj_set_style_bg_color(btn, C_BTN, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_add_event_cb(btn, save_btn_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, LV_SYMBOL_SAVE " Guardar");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);

    lbl_status = lv_label_create(parent);
    lv_obj_set_pos(lbl_status, 10, 238);
    lv_obj_set_width(lbl_status, 320);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_status, C_MUTED, 0);
    lv_label_set_text(lbl_status, "");

    // ── Teclado LVGL (oculto por defecto) ─────────────────────────────────
    kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, 480, 140);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY,  nullptr);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, nullptr);
}

// ── Refresco periódico de estado de red ───────────────────────────────────
void config_screen_tick() {
    if (millis() - s_last_net_refresh < 5000) return;
    s_last_net_refresh = millis();

    if (WiFi.status() == WL_CONNECTED) {
        lv_label_set_text(lbl_ip, WiFi.localIP().toString().c_str());

        int32_t rssi = WiFi.RSSI();
        lv_color_t col = (rssi > -60) ? C_OK : (rssi > -75) ? C_WARN : C_ERR;
        const char* qual = (rssi > -60) ? "Buena" : (rssi > -75) ? "Regular" : "Debil";
        char buf[32];
        snprintf(buf, sizeof(buf), "%s (%d dBm)", qual, (int)rssi);
        lv_label_set_text(lbl_rssi, buf);
        lv_obj_set_style_text_color(lbl_rssi, col, 0);
    } else {
        lv_label_set_text(lbl_ip,   "Sin conexion");
        lv_label_set_text(lbl_rssi, "--");
        lv_obj_set_style_text_color(lbl_rssi, C_ERR, 0);
    }
}