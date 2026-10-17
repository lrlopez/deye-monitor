#include "logger_scan_ui.h"
#include <WiFi.h>
#include "ui_constants.h"
#include "ui_toast.h"
#include "logger_discovery.h"
#include "input_validators.h"
#include "config.h"

#define SCAN_MAX 4

// Estado del escaneo en curso. Lo rellena la tarea; la UI solo lo lee cuando `done` es true.
static struct {
    volatile bool running;
    volatile bool done;
    uint32_t      serial;          // 0 = sin serie válido: no se verifica
    char          ips[SCAN_MAX][16];
    bool          verified[SCAN_MAX];
    int           n;
    bool          any_verified;
    lv_obj_t*     ta_ip;
    lv_obj_t*     lbl;
} s_job;

static lv_timer_t* s_timer = nullptr;
static lv_obj_t*   s_modal = nullptr;

static void modal_close() {
    if (s_modal) { lv_obj_delete(s_modal); s_modal = nullptr; }
}

static void scan_task(void*) {
    s_job.n = logger_scan(s_job.ips, SCAN_MAX);
    s_job.any_verified = false;
    for (int i = 0; i < s_job.n; i++) {
        s_job.verified[i] = s_job.serial && logger_verify(s_job.ips[i], s_job.serial, MODBUS_UNIT_ID);
        if (s_job.verified[i]) s_job.any_verified = true;
    }
    s_job.done = true;
    vTaskDelete(nullptr);
}

static void pick(const char* ip) {
    if (lv_obj_is_valid(s_job.ta_ip)) lv_textarea_set_text(s_job.ta_ip, ip);
}

static void item_cb(lv_event_t* e) {
    pick((const char*)lv_event_get_user_data(e));
    modal_close();
}

// Índices de los candidatos que se ofrecen: los verificados si hay alguno, si no todos
static int offered(int* idx) {
    int m = 0;
    for (int i = 0; i < s_job.n; i++)
        if (!s_job.any_verified || s_job.verified[i]) idx[m++] = i;
    return m;
}

static void modal_open(const int* idx, int m) {
    modal_close();
    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_modal, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(s_modal, 0, 0);
    lv_obj_set_style_bg_color(s_modal, C_BG, 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_modal, 0, 0);
    lv_obj_set_style_radius(s_modal, 0, 0);
    lv_obj_set_style_pad_all(s_modal, 0, 0);
    lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t = lv_label_create(s_modal);
    lv_label_set_text(t, "Elige tu datalogger");
    lv_obj_set_style_text_font(t, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(t, C_WHITE, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, SY(8));

    const int foot_h = CFG_FIELD_H + SY(10);
    lv_obj_t* list = lv_list_create(s_modal);
    lv_obj_set_pos(list, SX(10), SY(34));
    lv_obj_set_size(list, SCREEN_WIDTH - 2 * SX(10), SCREEN_HEIGHT - SY(34) - foot_h - SY(6));
    lv_obj_set_style_bg_color(list, C_LIST, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(list, C_ACCENT, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_radius(list, 6, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_style_pad_row(list, 1, 0);

    for (int k = 0; k < m; k++) {
        const int i = idx[k];
        char txt[48];
        snprintf(txt, sizeof(txt), LV_SYMBOL_WIFI " %s%s", s_job.ips[i],
                 s_job.verified[i] ? "  (verificado)" : "");
        lv_obj_t* b = lv_list_add_btn(list, nullptr, txt);
        lv_obj_set_style_bg_color(b, C_LIST, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x21262D), LV_STATE_PRESSED);
        lv_obj_t* l = lv_obj_get_child(b, 0);
        if (l) {
            lv_obj_set_style_text_color(l, s_job.verified[i] ? C_OK : C_WHITE, 0);
            lv_obj_set_style_text_font(l, &FONT_SMALL, 0);
        }
        lv_obj_add_event_cb(b, item_cb, LV_EVENT_CLICKED, s_job.ips[i]);
    }

    lv_obj_t* c = lv_btn_create(s_modal);
    lv_obj_set_pos(c, SX(10), SCREEN_HEIGHT - foot_h);
    lv_obj_set_size(c, SX(140), CFG_FIELD_H);
    lv_obj_set_style_bg_color(c, C_GBTN, 0);
    lv_obj_set_style_radius(c, 6, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    lv_obj_add_event_cb(c, [](lv_event_t*) { modal_close(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* cl = lv_label_create(c);
    lv_label_set_text(cl, "Cancelar");
    lv_obj_set_style_text_font(cl, &FONT_NORMAL, 0);
    lv_obj_center(cl);
}

static void poll_cb(lv_timer_t*) {
    if (!s_job.done) return;
    lv_timer_delete(s_timer);
    s_timer = nullptr;
    s_job.running = false;
    if (lv_obj_is_valid(s_job.lbl)) {
        lv_label_set_text(s_job.lbl, LV_SYMBOL_GPS);
        lv_obj_set_style_text_color(s_job.lbl, C_ACCENT, 0);
    }

    int idx[SCAN_MAX];
    const int m = offered(idx);
    if (m == 0) {
        ui_toast_error("No se ha encontrado ning\xC3\xBAn logger. Escribe su IP a mano");
    } else if (m == 1) {
        pick(s_job.ips[idx[0]]);
        char b[80];
        snprintf(b, sizeof(b), "Logger encontrado: %s%s", s_job.ips[idx[0]],
                 s_job.verified[idx[0]] ? " (verificado)" : " (sin verificar)");
        ui_toast_show(b);
    } else {
        modal_open(idx, m);
    }
}

static void btn_cb(lv_event_t* e) {
    if (s_job.running) return;
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t** pair = (lv_obj_t**)lv_event_get_user_data(e);   // {ta_ip, ta_serial}

    if (WiFi.status() != WL_CONNECTED) {
        ui_toast_error("Sin conexi\xC3\xB3n WiFi: no se puede buscar el logger");
        return;
    }
    s_job.ta_ip = pair[0];
    s_job.lbl   = lv_obj_get_child(btn, 0);
    const char* sn = pair[1] ? lv_textarea_get_text(pair[1]) : "";
    s_job.serial = valid_logger_serial(sn) ? (uint32_t)strtoul(sn, nullptr, 10) : 0;
    s_job.n = 0;
    s_job.done = false;
    s_job.running = true;
    lv_label_set_text(s_job.lbl, LV_SYMBOL_LOOP);
    lv_obj_set_style_text_color(s_job.lbl, C_WARN, 0);
    ui_toast_show("Buscando el logger en la red...", 2500);

    if (xTaskCreatePinnedToCore(scan_task, "logger_scan", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
        s_job.running = false;
        lv_label_set_text(s_job.lbl, LV_SYMBOL_GPS);
        lv_obj_set_style_text_color(s_job.lbl, C_ACCENT, 0);
        ui_toast_error("No hay memoria para buscar el logger");
        return;
    }
    s_timer = lv_timer_create(poll_cb, 250, nullptr);
}

lv_obj_t* logger_scan_button(lv_obj_t* parent, int x, int y, int w,
                             lv_obj_t* ta_ip, lv_obj_t* ta_serial) {
    // Los dos campos viven toda la ejecución (las pantallas no se destruyen): basta un par estático por botón
    lv_obj_t** pair = (lv_obj_t**)lv_malloc(2 * sizeof(lv_obj_t*));
    pair[0] = ta_ip;
    pair[1] = ta_serial;

    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, CFG_FIELD_H);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x21262D), 0);
    lv_obj_set_style_border_color(b, C_ACCENT, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, pair);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, LV_SYMBOL_GPS);
    lv_obj_set_style_text_color(l, C_ACCENT, 0);
    lv_obj_set_style_text_font(l, &FONT_NORMAL, 0);
    lv_obj_center(l);
    return b;
}
