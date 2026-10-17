#include "setup_wizard.h"
#include <WiFi.h>
#include "ui_constants.h"
#include "ui_toast.h"
#include "storage.h"
#include "config.h"
#include "input_validators.h"

// ── Pasos ─────────────────────────────────────────────────────────────────
enum { ST_WELCOME = 0, ST_WIFI, ST_LOGGER, ST_WEB, ST_SUMMARY, ST_N };
#define STEPS_SHOWN 4                     // la bienvenida no cuenta: "Paso 1 de 4" es el WiFi

#define WZ_HDR_H    SY(34)
#define WZ_FOOT_H   (CFG_FIELD_H + SY(10))
#define WZ_MARGIN   SX(10)
#define WZ_ROW_H    (CFG_FIELD_H + SY(10))
#define WZ_FIELD_X  CFG_LBL_W
#define WZ_FIELD_W  (SCREEN_WIDTH - 2 * WZ_MARGIN - CFG_LBL_W)

static lv_obj_t*  s_ov          = nullptr;
static lv_obj_t*  s_panel[ST_N] = {};
static int        s_step        = ST_WELCOME;

static lv_obj_t *ta_ssid, *ta_pass, *ta_ip, *ta_serial, *ta_web1, *ta_web2;
static lv_obj_t *s_lbl_step, *s_btn_back, *s_btn_next, *s_lbl_next, *s_kb, *s_sum;
static lv_obj_t *s_scan_lbl;
static lv_obj_t* s_net_modal = nullptr;
static lv_timer_t* s_scan_timer = nullptr;
static bool        s_scanning   = false;

bool wizard_active() { return s_ov != nullptr; }

bool wizard_should_run() {
    if (Storage.loadWizardDone()) return false;
    AppConfig c{};
    Storage.loadConfig(c);
    if (c.wifi_ssid[0] != '\0') {          // ya venía configurado: no molestar
        Storage.saveWizardDone(true);
        return false;
    }
    return true;
}

// ── Widgets básicos ───────────────────────────────────────────────────────
static lv_obj_t* mk_label(lv_obj_t* p, int x, int y, int w, const char* txt,
                          const lv_font_t* font, lv_color_t col, bool wrap = false) {
    lv_obj_t* l = lv_label_create(p);
    lv_obj_set_pos(l, x, y);
    if (w > 0) lv_obj_set_width(l, w);
    if (wrap) lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t* mk_btn(lv_obj_t* p, int x, int y, int w, lv_color_t bg, const char* txt,
                        lv_event_cb_t cb, lv_obj_t** lbl_out = nullptr) {
    lv_obj_t* b = lv_btn_create(p);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, CFG_FIELD_H);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &FONT_NORMAL, 0);
    lv_obj_center(l);
    if (lbl_out) *lbl_out = l;
    return b;
}

static void kb_event_cb(lv_event_t* e) {
    const lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

// Foco: el teclado se ancla al campo (numérico para IP y serie) y se muestra
static void ta_event_cb(lv_event_t* e) {
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
        const bool numeric = ((intptr_t)lv_event_get_user_data(e) != 0);
        lv_keyboard_set_mode(s_kb, numeric ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(s_kb, ta);
        lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_kb);
    } else if (lv_event_get_code(e) == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t* mk_field(lv_obj_t* p, int x, int y, int w, bool password, bool numeric,
                          const char* placeholder, const char* accepted) {
    lv_obj_t* ta = lv_textarea_create(p);
    lv_obj_set_pos(ta, x, y);
    lv_obj_set_size(ta, w, CFG_FIELD_H);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    if (password) lv_textarea_set_password_mode(ta, true);
    if (accepted) lv_textarea_set_accepted_chars(ta, accepted);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x21262D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, C_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, C_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, &FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ta, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ta, 6, LV_PART_MAIN);
    // Cursor visible con foco (mismo ajuste que la pantalla Configuración)
    lv_obj_set_style_border_width(ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, (lv_style_selector_t)LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(ta, LV_BORDER_SIDE_LEFT, (lv_style_selector_t)LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(ta, C_WHITE, (lv_style_selector_t)LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2, (lv_style_selector_t)LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_FOCUSED,   (void*)(intptr_t)(numeric ? 1 : 0));
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_DEFOCUSED, (void*)(intptr_t)(numeric ? 1 : 0));
    return ta;
}

static void mark_invalid(lv_obj_t* ta, bool invalid) {
    lv_obj_set_style_border_color(ta, invalid ? C_ERR : C_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, invalid ? 2 : 1, LV_PART_MAIN);
}

// ── Búsqueda de redes ─────────────────────────────────────────────────────
static void net_modal_close() {
    if (s_net_modal) { lv_obj_delete(s_net_modal); s_net_modal = nullptr; }
}

static char s_ssid_buf[12][33];

static void net_item_cb(lv_event_t* e) {
    const char* ssid = (const char*)lv_event_get_user_data(e);
    if (ssid) lv_textarea_set_text(ta_ssid, ssid);
    mark_invalid(ta_ssid, false);
    net_modal_close();
}

static void net_modal_open() {
    net_modal_close();
    const int n = WiFi.scanComplete();
    if (n <= 0) { ui_toast_error("No se han encontrado redes WiFi"); return; }

    s_net_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_net_modal, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(s_net_modal, 0, 0);
    lv_obj_set_style_bg_color(s_net_modal, C_BG, 0);
    lv_obj_set_style_bg_opa(s_net_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_net_modal, 0, 0);
    lv_obj_set_style_radius(s_net_modal, 0, 0);
    lv_obj_set_style_pad_all(s_net_modal, 0, 0);
    lv_obj_remove_flag(s_net_modal, LV_OBJ_FLAG_SCROLLABLE);

    mk_label(s_net_modal, 0, SY(8), SCREEN_WIDTH, "Elige tu red WiFi", &FONT_NORMAL, C_WHITE);
    lv_obj_set_style_text_align(lv_obj_get_child(s_net_modal, 0), LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* list = lv_list_create(s_net_modal);
    lv_obj_set_pos(list, WZ_MARGIN, WZ_HDR_H);
    lv_obj_set_size(list, SCREEN_WIDTH - 2 * WZ_MARGIN, SCREEN_HEIGHT - WZ_HDR_H - WZ_FOOT_H - SY(6));
    lv_obj_set_style_bg_color(list, C_LIST, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(list, C_ACCENT, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_border_opa(list, LV_OPA_60, 0);
    lv_obj_set_style_radius(list, 6, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_style_pad_row(list, 1, 0);

    // Ordenar por señal, sin SSIDs vacíos ni repetidos (varios puntos de acceso de la misma red)
    int idx[n];
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (WiFi.RSSI(idx[j]) > WiFi.RSSI(idx[i])) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }

    int shown = 0;
    for (int k = 0; k < n && shown < 12; k++) {
        const String name = WiFi.SSID(idx[k]);
        if (name.length() == 0) continue;
        bool dup = false;
        for (int d = 0; d < shown; d++) if (name.equals(s_ssid_buf[d])) { dup = true; break; }
        if (dup) continue;
        strlcpy(s_ssid_buf[shown], name.c_str(), sizeof(s_ssid_buf[shown]));

        const int rssi = WiFi.RSSI(idx[k]);
        char txt[64];
        snprintf(txt, sizeof(txt), LV_SYMBOL_WIFI " %s  (%d dBm)", s_ssid_buf[shown], rssi);
        lv_obj_t* b = lv_list_add_btn(list, nullptr, txt);
        lv_obj_set_style_bg_color(b, C_LIST, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x21262D), LV_STATE_PRESSED);
        lv_obj_t* l = lv_obj_get_child(b, 0);
        if (l) {
            lv_obj_set_style_text_color(l, rssi > -60 ? C_OK : (rssi > -75 ? C_WARN : C_ERR), 0);
            lv_obj_set_style_text_font(l, &FONT_SMALL, 0);
        }
        lv_obj_add_event_cb(b, net_item_cb, LV_EVENT_CLICKED, s_ssid_buf[shown]);
        shown++;
    }

    mk_btn(s_net_modal, WZ_MARGIN, SCREEN_HEIGHT - WZ_FOOT_H, SX(140), C_GBTN, "Cancelar",
           [](lv_event_t*) { net_modal_close(); });
}

static void scan_poll_cb(lv_timer_t*) {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
    lv_timer_delete(s_scan_timer);
    s_scan_timer = nullptr;
    s_scanning = false;
    lv_label_set_text(s_scan_lbl, LV_SYMBOL_REFRESH);
    net_modal_open();
}

static void scan_btn_cb(lv_event_t*) {
    if (s_scanning) return;
    WiFi.scanDelete();
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
    s_scanning = true;
    lv_label_set_text(s_scan_lbl, LV_SYMBOL_LOOP);
    s_scan_timer = lv_timer_create(scan_poll_cb, 300, nullptr);
}

// ── Navegación ────────────────────────────────────────────────────────────
static void update_summary() {
    char b[200];
    const char* web = lv_textarea_get_text(ta_web1);
    snprintf(b, sizeof(b),
        LV_SYMBOL_WIFI " WiFi: %s\n"
        LV_SYMBOL_EDIT " Datalogger: %s (serie %s)\n"
        LV_SYMBOL_WARNING " Web de administraci\xC3\xB3n: %s",
        lv_textarea_get_text(ta_ssid), lv_textarea_get_text(ta_ip), lv_textarea_get_text(ta_serial),
        web[0] ? "clave definida" : "SIN clave (quedar\xC3\xA1 bloqueada)");
    lv_label_set_text(s_sum, b);
}

static void show_step(int n) {
    s_step = n;
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < ST_N; i++) {
        if (i == n) lv_obj_remove_flag(s_panel[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_panel[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (n == ST_WELCOME) lv_label_set_text(s_lbl_step, "");
    else lv_label_set_text_fmt(s_lbl_step, "Paso %d de %d", n, STEPS_SHOWN);

    if (n == ST_WELCOME) lv_obj_add_flag(s_btn_back, LV_OBJ_FLAG_HIDDEN);
    else                 lv_obj_remove_flag(s_btn_back, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(s_lbl_next,
        n == ST_WELCOME ? "Empezar " LV_SYMBOL_RIGHT :
        n == ST_SUMMARY ? LV_SYMBOL_SAVE " Guardar y reiniciar" : "Siguiente " LV_SYMBOL_RIGHT);
    if (n == ST_SUMMARY) update_summary();
}

// Valida el paso actual; en caso contrario marca el campo, avisa y devuelve false
static bool validate_step(int n) {
    if (n == ST_WIFI) {
        const bool ssid_ok = lv_textarea_get_text(ta_ssid)[0] != '\0';
        const bool pass_ok = valid_wifi_pass(lv_textarea_get_text(ta_pass));
        mark_invalid(ta_ssid, !ssid_ok);
        mark_invalid(ta_pass, !pass_ok);
        if (!ssid_ok) { ui_toast_error("Escribe o elige el nombre de tu red WiFi"); return false; }
        if (!pass_ok) { ui_toast_error("Clave WiFi: vac\xC3\xAD\x61 o de 8 a 63 caracteres"); return false; }
    } else if (n == ST_LOGGER) {
        const bool ip_ok = valid_logger_ip(lv_textarea_get_text(ta_ip));
        const bool sn_ok = valid_logger_serial(lv_textarea_get_text(ta_serial));
        mark_invalid(ta_ip, !ip_ok);
        mark_invalid(ta_serial, !sn_ok);
        if (!ip_ok) { ui_toast_error("IP del logger no v\xC3\xA1lida (ej. 192.168.1.50)"); return false; }
        if (!sn_ok) { ui_toast_error("N\xC3\xBAm. de serie: solo d\xC3\xADgitos"); return false; }
    } else if (n == ST_WEB) {
        const char* a = lv_textarea_get_text(ta_web1);
        const char* b = lv_textarea_get_text(ta_web2);
        if (a[0] == '\0' && b[0] == '\0') return true;          // sin clave: se permite (web bloqueada)
        const bool len_ok = valid_admin_pass(a) && a[0] != '\0';
        const bool same   = strcmp(a, b) == 0;
        mark_invalid(ta_web1, !len_ok);
        mark_invalid(ta_web2, !same);
        if (!len_ok) { ui_toast_error("La clave web debe tener al menos 8 caracteres"); return false; }
        if (!same)   { ui_toast_error("Las dos claves no coinciden"); return false; }
    }
    return true;
}

static void close_wizard() {
    if (s_scan_timer) { lv_timer_delete(s_scan_timer); s_scan_timer = nullptr; s_scanning = false; }
    net_modal_close();
    if (s_kb) { lv_obj_delete(s_kb); s_kb = nullptr; }
    if (s_ov) { lv_obj_delete(s_ov); s_ov = nullptr; }
}

// Guarda la configuración, marca el asistente como completado y reinicia
static void finish_and_restart() {
    AppConfig cfg{};
    Storage.loadConfig(cfg);                     // conserva mDNS y capacidades por defecto
    strncpy(cfg.wifi_ssid, lv_textarea_get_text(ta_ssid), sizeof(cfg.wifi_ssid) - 1);
    strncpy(cfg.wifi_pass, lv_textarea_get_text(ta_pass), sizeof(cfg.wifi_pass) - 1);
    strncpy(cfg.logger_ip, lv_textarea_get_text(ta_ip),   sizeof(cfg.logger_ip) - 1);
    cfg.logger_serial = (uint32_t)strtoul(lv_textarea_get_text(ta_serial), nullptr, 10);
    Storage.saveConfig(cfg);
    const char* web = lv_textarea_get_text(ta_web1);
    if (web[0]) Storage.saveAdminPassword(web);
    Storage.saveWizardDone(true);

    lv_label_set_text(s_sum, LV_SYMBOL_OK " Guardado. Reiniciando...");
    lv_obj_set_style_text_color(s_sum, C_OK, 0);
    lv_timer_handler();
    delay(1200);
    ESP.restart();
}

static void next_cb(lv_event_t*) {
    if (!validate_step(s_step)) return;
    if (s_step == ST_SUMMARY) { finish_and_restart(); return; }
    show_step(s_step + 1);
}

static void back_cb(lv_event_t*) {
    if (s_step > ST_WELCOME) show_step(s_step - 1);
}

static void skip_confirm_no_cb(lv_event_t* e)  { lv_msgbox_close((lv_obj_t*)lv_event_get_user_data(e)); }
static void skip_confirm_yes_cb(lv_event_t* e) {
    lv_msgbox_close((lv_obj_t*)lv_event_get_user_data(e));
    Storage.saveWizardDone(true);              // no volver a ofrecerlo
    close_wizard();
}

static void skip_cb(lv_event_t*) {
    lv_obj_t* mbox = lv_msgbox_create(nullptr);   // modal en layer_top
    lv_msgbox_add_title(mbox, "Omitir el asistente");
    lv_msgbox_add_text(mbox, "Podr\xC3\xA1s configurar todo m\xC3\xA1s tarde desde la pantalla Configuraci\xC3\xB3n. \xC2\xBFOmitir?");
    lv_obj_t* no  = lv_msgbox_add_footer_button(mbox, "Seguir");
    lv_obj_t* yes = lv_msgbox_add_footer_button(mbox, "Omitir");
    lv_obj_add_event_cb(no,  skip_confirm_no_cb,  LV_EVENT_CLICKED, mbox);
    lv_obj_add_event_cb(yes, skip_confirm_yes_cb, LV_EVENT_CLICKED, mbox);
}

// ── Construcción ──────────────────────────────────────────────────────────
static lv_obj_t* make_panel(lv_obj_t* parent) {
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_set_pos(p, WZ_MARGIN, WZ_HDR_H);
    lv_obj_set_size(p, SCREEN_WIDTH - 2 * WZ_MARGIN, SCREEN_HEIGHT - WZ_HDR_H - WZ_FOOT_H);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

void wizard_start() {
    if (s_ov) return;
    const int pw = SCREEN_WIDTH - 2 * WZ_MARGIN;       // ancho útil de un panel
    const int fw = pw - CFG_LBL_W;                     // ancho de un campo dentro del panel

    s_ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ov, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(s_ov, 0, 0);
    lv_obj_set_style_bg_color(s_ov, C_BG, 0);
    lv_obj_set_style_bg_opa(s_ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ov, 0, 0);
    lv_obj_set_style_radius(s_ov, 0, 0);
    lv_obj_set_style_pad_all(s_ov, 0, 0);
    lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);

    // Cabecera
    mk_label(s_ov, WZ_MARGIN, SY(7), 0, LV_SYMBOL_SETTINGS "  Asistente de configuraci\xC3\xB3n",
             &FONT_NORMAL, C_WHITE);
    s_lbl_step = mk_label(s_ov, 0, SY(9), SCREEN_WIDTH, "", &FONT_SMALL, C_MUTED);
    lv_obj_set_style_text_align(s_lbl_step, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* skip = mk_label(s_ov, SCREEN_WIDTH - WZ_MARGIN - SX(70), SY(9), SX(70), "Omitir",
                              &FONT_SMALL, C_RUN);
    lv_obj_set_style_text_align(skip, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(skip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(skip, SY(10));
    lv_obj_add_event_cb(skip, skip_cb, LV_EVENT_CLICKED, nullptr);

    for (int i = 0; i < ST_N; i++) s_panel[i] = make_panel(s_ov);

    // ── Bienvenida ────────────────────────────────────────────────────────
    {
        lv_obj_t* p = s_panel[ST_WELCOME];
        mk_label(p, 0, SY(10), pw, "Bienvenido", &FONT_LARGE, C_WHITE);
        mk_label(p, 0, SY(10) + FONT_LARGE_SIZE + SY(10), pw,
                 "Vamos a conectar este monitor a tu red WiFi y a tu inversor Deye. "
                 "Solo te llevar\xC3\xA1 un minuto.", &FONT_NORMAL, C_MUTED, true);
        mk_label(p, 0, SY(118), pw,
                 "1  Red WiFi\n2  Datalogger del inversor\n3  Clave de la web de administraci\xC3\xB3n",
                 &FONT_SMALL, C_MUTED2);
    }

    // ── WiFi ──────────────────────────────────────────────────────────────
    {
        lv_obj_t* p = s_panel[ST_WIFI];
        const int scan_w = CFG_SCAN_BTN_W;
        mk_label(p, 0, SY(8) + SY(10), CFG_LBL_W, "Red WiFi", &FONT_SMALL, C_MUTED);
        ta_ssid = mk_field(p, WZ_FIELD_X, SY(8), fw - scan_w - SX(4), false, false, "Nombre de tu red", nullptr);
        lv_textarea_set_max_length(ta_ssid, 32);
        lv_obj_t* sb = lv_btn_create(p);
        lv_obj_set_pos(sb, WZ_FIELD_X + fw - scan_w, SY(8));
        lv_obj_set_size(sb, scan_w, CFG_FIELD_H);
        lv_obj_set_style_bg_color(sb, lv_color_hex(0x21262D), 0);
        lv_obj_set_style_border_color(sb, C_ACCENT, 0);
        lv_obj_set_style_border_width(sb, 1, 0);
        lv_obj_set_style_radius(sb, 6, 0);
        lv_obj_set_style_shadow_width(sb, 0, 0);
        lv_obj_add_event_cb(sb, scan_btn_cb, LV_EVENT_CLICKED, nullptr);
        s_scan_lbl = lv_label_create(sb);
        lv_label_set_text(s_scan_lbl, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(s_scan_lbl, C_ACCENT, 0);
        lv_obj_set_style_text_font(s_scan_lbl, &FONT_NORMAL, 0);
        lv_obj_center(s_scan_lbl);

        mk_label(p, 0, SY(8) + WZ_ROW_H + SY(10), CFG_LBL_W, "Contrase\xC3\xB1\x61", &FONT_SMALL, C_MUTED);
        ta_pass = mk_field(p, WZ_FIELD_X, SY(8) + WZ_ROW_H, fw, true, false, "Clave de tu WiFi", nullptr);
        lv_textarea_set_max_length(ta_pass, 63);
        mk_label(p, 0, SY(8) + 2 * WZ_ROW_H + SY(4), pw,
                 "Pulsa " LV_SYMBOL_REFRESH " para buscar redes cercanas. Si tu red no tiene clave, d\xC3\xA9jala vac\xC3\xAD\x61.",
                 &FONT_SMALL, C_MUTED2, true);
    }

    // ── Datalogger ────────────────────────────────────────────────────────
    {
        lv_obj_t* p = s_panel[ST_LOGGER];
        mk_label(p, 0, SY(8) + SY(10), CFG_LBL_W, "IP del logger", &FONT_SMALL, C_MUTED);
        ta_ip = mk_field(p, WZ_FIELD_X, SY(8), fw, false, true, "192.168.1.xxx", "0123456789.");
        lv_textarea_set_max_length(ta_ip, 15);
        mk_label(p, 0, SY(8) + WZ_ROW_H + SY(10), CFG_LBL_W, "N\xC3\xBAm. de serie", &FONT_SMALL, C_MUTED);
        ta_serial = mk_field(p, WZ_FIELD_X, SY(8) + WZ_ROW_H, fw, false, true, "Ver etiqueta del stick", "0123456789");
        lv_textarea_set_max_length(ta_serial, 10);
        mk_label(p, 0, SY(8) + 2 * WZ_ROW_H + SY(4), pw,
                 "La IP del stick WiFi (LSW3) aparece en tu router; el n\xC3\xBAmero de serie, en su etiqueta o en la app SolarmanPV.",
                 &FONT_SMALL, C_MUTED2, true);
    }

    // ── Clave web ─────────────────────────────────────────────────────────
    {
        lv_obj_t* p = s_panel[ST_WEB];
        mk_label(p, 0, SY(8) + SY(10), CFG_LBL_W, "Clave web", &FONT_SMALL, C_MUTED);
        ta_web1 = mk_field(p, WZ_FIELD_X, SY(8), fw, true, false, "M\xC3\xADnimo 8 caracteres", nullptr);
        lv_textarea_set_max_length(ta_web1, 63);
        mk_label(p, 0, SY(8) + WZ_ROW_H + SY(10), CFG_LBL_W, "Repetir", &FONT_SMALL, C_MUTED);
        ta_web2 = mk_field(p, WZ_FIELD_X, SY(8) + WZ_ROW_H, fw, true, false, "Repite la clave", nullptr);
        lv_textarea_set_max_length(ta_web2, 63);
        mk_label(p, 0, SY(8) + 2 * WZ_ROW_H + SY(4), pw,
                 "Protege la administraci\xC3\xB3n web y las actualizaciones. Si la dejas vac\xC3\xAD\x61, la web de "
                 "administraci\xC3\xB3n quedar\xC3\xA1 bloqueada hasta que la definas en Configuraci\xC3\xB3n.",
                 &FONT_SMALL, C_MUTED2, true);
    }

    // ── Resumen ───────────────────────────────────────────────────────────
    {
        lv_obj_t* p = s_panel[ST_SUMMARY];
        mk_label(p, 0, SY(4), pw, "Todo listo", &FONT_LARGE, C_WHITE);
        s_sum = mk_label(p, 0, SY(4) + FONT_LARGE_SIZE + SY(10), pw, "", &FONT_NORMAL, C_WHITE, true);
        mk_label(p, 0, SCREEN_HEIGHT - WZ_HDR_H - WZ_FOOT_H - FONT_SMALL_SIZE * 2 - SY(8), pw,
                 "Al guardar, el dispositivo se reiniciar\xC3\xA1 y se conectar\xC3\xA1 a tu red.",
                 &FONT_SMALL, C_MUTED2, true);
    }

    // Pie de navegación
    s_btn_back = mk_btn(s_ov, WZ_MARGIN, SCREEN_HEIGHT - WZ_FOOT_H, SX(110), C_GBTN,
                        LV_SYMBOL_LEFT " Atr\xC3\xA1s", back_cb);
    s_btn_next = mk_btn(s_ov, SCREEN_WIDTH - WZ_MARGIN - SX(190), SCREEN_HEIGHT - WZ_FOOT_H, SX(190), C_BTN,
                        "Siguiente", next_cb, &s_lbl_next);

    // Teclado (encima de todo, oculto hasta que un campo toma el foco)
    s_kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_kb, SCREEN_WIDTH, SY(140));
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_READY,  nullptr);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_CANCEL, nullptr);

    show_step(ST_WELCOME);
}
