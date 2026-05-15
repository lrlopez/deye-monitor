#include <Arduino.h>
#include "splash_screen.h"
#include "ui_constants.h"

#define C_BG     lv_color_hex(0x0D1117)
#define C_CARD   lv_color_hex(0x161B22)
#define C_WHITE  lv_color_hex(0xEAEAEA)
#define C_MUTED  lv_color_hex(0x6E7681)
#define C_OK     lv_color_hex(0x2ECC71)
#define C_WARN   lv_color_hex(0xF5C518)
#define C_ERR    lv_color_hex(0xE74C3C)
#define C_RUN    lv_color_hex(0x4A9EFF)

static const char* STEP_NAMES[] = {
    "Sistema de archivos",
    "Almacenamiento",
    "Cache PSRAM",
    "Hora NTP",
    "Conectando WiFi...",
    "WiFi",
    "WiFi (sin conexion)",
    "Servidor web",
    "Notificaciones",
    "Listo"
};

static lv_obj_t* s_screen    = nullptr;
static lv_obj_t* s_bar       = nullptr;   // barra de progreso
static lv_obj_t* s_lbl_step  = nullptr;   // paso actual en grande
static lv_obj_t* s_lbl_detail= nullptr;   // detalle opcional
static lv_obj_t* s_rows[(int)SplashStep::DONE] = {};  // icono + label por paso
static lv_obj_t* s_row_labels[(int)SplashStep::DONE] = {};

static constexpr int N_STEPS = (int)SplashStep::DONE;
static constexpr int ROW_H   = 22;
static constexpr int LIST_Y  = SY(90);

// ── Helpers ───────────────────────────────────────────────────────────────
static lv_color_t state_color(SplashState s) {
    switch (s) {
        case SplashState::OK:      return C_OK;
        case SplashState::WARN:    return C_WARN;
        case SplashState::ERROR:   return C_ERR;
        case SplashState::RUNNING: return C_RUN;
        default:                   return C_MUTED;
    }
}

static const char* state_icon(SplashState s) {
    switch (s) {
        case SplashState::OK:      return LV_SYMBOL_OK;
        case SplashState::WARN:    return LV_SYMBOL_WARNING;
        case SplashState::ERROR:   return LV_SYMBOL_CLOSE;
        case SplashState::RUNNING: return LV_SYMBOL_LOOP;
        default:                   return LV_SYMBOL_MINUS;
    }
}

// ── Init ──────────────────────────────────────────────────────────────────
void splash_init() {
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, C_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_scr_load(s_screen);

    // Título
    lv_obj_t* title = lv_label_create(s_screen);
    lv_obj_set_pos(title, 0, SY(14));
    lv_obj_set_width(title, SCREEN_WIDTH);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title, &FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, C_WHITE, 0);
    lv_label_set_text(title, LV_SYMBOL_CHARGE " Deye Monitor");

    // Subtítulo / paso actual
    s_lbl_step = lv_label_create(s_screen);
    lv_obj_set_pos(s_lbl_step, 0, SY(54));
    lv_obj_set_width(s_lbl_step, SCREEN_WIDTH);
    lv_obj_set_style_text_align(s_lbl_step, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_lbl_step, &FONT_NORMAL, 0);
    lv_obj_set_style_text_color(s_lbl_step, C_MUTED, 0);
    lv_label_set_text(s_lbl_step, "Iniciando...");

    // Lista de pasos (solo mostramos los relevantes, no WIFI_OK/WIFI_FAIL
    // que son aliases de WIFI_CONNECTING con distinto estado)
    const int VISIBLE_STEPS[] = {
        (int)SplashStep::LITTLEFS,
        (int)SplashStep::DATASTORE,
        (int)SplashStep::PSRAM_CACHE,
        (int)SplashStep::NTP,
        (int)SplashStep::WIFI_CONNECTING,
        (int)SplashStep::WEBSERVER,
        (int)SplashStep::TELEGRAM,
    };
    const int N_VISIBLE = sizeof(VISIBLE_STEPS)/sizeof(VISIBLE_STEPS[0]);

    int list_x = SX(80);
    for (int vi = 0; vi < N_VISIBLE; vi++) {
        int si = VISIBLE_STEPS[vi];
        int y  = LIST_Y + vi * ROW_H;

        // Icono de estado
        lv_obj_t* icon = lv_label_create(s_screen);
        lv_obj_set_pos(icon, list_x, y);
        lv_obj_set_style_text_font(icon, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(icon, C_MUTED, 0);
        lv_label_set_text(icon, LV_SYMBOL_MINUS);

        // Nombre del paso
        lv_obj_t* lbl = lv_label_create(s_screen);
        lv_obj_set_pos(lbl, list_x + SX(20), y);
        lv_obj_set_style_text_font(lbl, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl, C_MUTED, 0);
        lv_label_set_text(lbl, STEP_NAMES[si]);

        // Guardamos el puntero del icono (índice = step enum)
        s_rows[si] = icon;
        s_row_labels[si] = lbl;   // guardar referencia directa
    }

    // Label de detalle (IP, error, etc.)
    s_lbl_detail = lv_label_create(s_screen);
    lv_obj_set_pos(s_lbl_detail, 0, LIST_Y + N_VISIBLE * ROW_H + SY(8));
    lv_obj_set_width(s_lbl_detail, SCREEN_WIDTH);
    lv_obj_set_style_text_align(s_lbl_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_lbl_detail, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_lbl_detail, C_MUTED, 0);
    lv_label_set_text(s_lbl_detail, "");

    // Barra de progreso
    s_bar = lv_bar_create(s_screen);
    lv_obj_set_pos(s_bar, SX(20), SCREEN_HEIGHT - SY(20));
    lv_obj_set_size(s_bar, SCREEN_WIDTH - SX(40), SS(8));
    lv_bar_set_range(s_bar, 0, N_STEPS - 1);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x21262D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x4A9EFF), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, SS(4), LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, SS(4), LV_PART_INDICATOR);

    // Primer render
    lv_timer_handler();
}

// ── Actualizar ────────────────────────────────────────────────────────────
void splash_update(SplashStep step, SplashState state, const char* detail) {
    int si = (int)step;

    // Normalizar WIFI_OK y WIFI_FAIL al slot de WIFI_CONNECTING
    int row_idx = si;
    if (step == SplashStep::WIFI_OK || step == SplashStep::WIFI_FAIL)
        row_idx = (int)SplashStep::WIFI_CONNECTING;

    // Actualizar icono y color del paso
    if (s_rows[row_idx]) {
        lv_label_set_text(s_rows[row_idx], state_icon(state));
        lv_obj_set_style_text_color(s_rows[row_idx], state_color(state), 0);

        if (s_row_labels[row_idx])
            lv_obj_set_style_text_color(s_row_labels[row_idx], state_color(state), 0);
    }

    // Label del paso actual en grande
    lv_label_set_text(s_lbl_step, STEP_NAMES[si]);
    lv_obj_set_style_text_color(s_lbl_step, state_color(state), 0);

    // Detalle opcional
    if (detail) lv_label_set_text(s_lbl_detail, detail);
    else        lv_label_set_text(s_lbl_detail, "");

    // Barra de progreso
    lv_bar_set_value(s_bar, si, LV_ANIM_ON);

    // Forzar render inmediato — crítico para que se vea durante el init
    lv_timer_handler();
    delay(5);
}

// ── Finish ────────────────────────────────────────────────────────────────
void splash_finish() {
    // El tileview ya está creado en setup(); simplemente cargamos esa pantalla
    // El splash se elimina al ser reemplazado
    extern lv_obj_t* g_main_screen;
    lv_scr_load_anim(g_main_screen, LV_SCR_LOAD_ANIM_FADE_IN, 400, 0, true);
    s_screen = nullptr;
}
