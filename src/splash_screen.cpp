#include <Arduino.h>
#include "splash_screen.h"
#include "config.h"
#include "ui_constants.h"

// ── Paleta ────────────────────────────────────────────────────────────────
#define C_BG        lv_color_hex(0x0A0E17)
#define C_CARD      lv_color_hex(0x131929)
#define C_WHITE     lv_color_hex(0xEAEAEA)
#define C_MUTED     lv_color_hex(0x4E5A6E)
#define C_MUTED2    lv_color_hex(0x6E7A8E)
#define C_OK        lv_color_hex(0x2ECC71)
#define C_WARN      lv_color_hex(0xF5C518)
#define C_ERR       lv_color_hex(0xE74C3C)
#define C_RUN       lv_color_hex(0x4A9EFF)
#define C_ACCENT    lv_color_hex(0x1F6FEB)
#define C_ACCENT2   lv_color_hex(0xF5C518)
#define C_DOTS      lv_color_hex(0x1E2D45)

// ── Pasos visibles (sin WIFI_OK/WIFI_FAIL que son alias) ──────────────────
static const int VISIBLE_STEPS[] = {
    (int)SplashStep::LITTLEFS,
    (int)SplashStep::DATASTORE,
    (int)SplashStep::PSRAM_CACHE,
    (int)SplashStep::NTP,
    (int)SplashStep::WIFI_CONNECTING,
    (int)SplashStep::WEBSERVER,
    (int)SplashStep::TELEGRAM,
};
static constexpr int N_VISIBLE = sizeof(VISIBLE_STEPS)/sizeof(VISIBLE_STEPS[0]);

static const char* STEP_NAMES[] = {
    "Sistema de archivos",
    "Almacenamiento",
    "Cache PSRAM",
    "Hora NTP",
    "Red WiFi",
    "Red WiFi",      // WIFI_OK  → alias
    "Red WiFi",      // WIFI_FAIL → alias
    "Servidor web",
    "Notificaciones",
    "Listo"
};

// ── Geometría ─────────────────────────────────────────────────────────────
#define HEADER_H     SY(72)    // zona del título
#define ACCENT_Y     SY(58)    // línea decorativa bajo el título
#define VERSION_Y    SY(62)    // versión y fecha de compilación
#define LIST_TOP     SY(80)    // inicio de la lista de pasos
#define ROW_H        SY(24)    // altura de cada fila
#define LIST_X       SX(16)    // margen izquierdo de la lista
#define DOT_X        LIST_X
#define LABEL_X      (LIST_X + SX(16))
#define STATUS_X     (SCREEN_WIDTH - SX(28))
#define BAR_Y        (SCREEN_HEIGHT - SY(18))
#define DETAIL_Y     (BAR_Y - SY(16))

// ── Widgets ───────────────────────────────────────────────────────────────
static lv_obj_t* s_screen      = nullptr;
static lv_obj_t* s_bar         = nullptr;
static lv_obj_t* s_lbl_detail  = nullptr;
static lv_obj_t* s_dots[N_VISIBLE]       = {};  // círculo de color
static lv_obj_t* s_step_labels[N_VISIBLE] = {};  // nombre del paso
static lv_obj_t* s_step_status[N_VISIBLE] = {};  // icono de estado derecha

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

// Índice en VISIBLE_STEPS para un step enum
static int visible_idx(int step_enum) {
    // Normalizar aliases
    if (step_enum == (int)SplashStep::WIFI_OK ||
        step_enum == (int)SplashStep::WIFI_FAIL)
        step_enum = (int)SplashStep::WIFI_CONNECTING;
    for (int i = 0; i < N_VISIBLE; i++)
        if (VISIBLE_STEPS[i] == step_enum) return i;
    return -1;
}

// ── Fecha de compilación legible ──────────────────────────────────────────
// __DATE__ = "May 15 2026", __TIME__ = "22:14:05"
static void build_date_str(char* out, size_t max) {
    // Parsear __DATE__ para reordenar a "15 May 2026"
    static const char* months[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    static const char* months_es[] = {
        "Ene","Feb","Mar","Abr","May","Jun",
        "Jul","Ago","Sep","Oct","Nov","Dic"
    };
    char month_s[4] = {}; int day = 0, year = 0;
    sscanf(__DATE__, "%3s %d %d", month_s, &day, &year);
    int month_idx = 0;
    for (int i = 0; i < 12; i++)
        if (strncmp(month_s, months[i], 3) == 0) { month_idx = i; break; }

    char time_s[6] = {};
    strncpy(time_s, __TIME__, 5);   // "HH:MM"

    snprintf(out, max, "v%s  ·  %d %s %d  ·  %s",
             APP_VERSION, day, months_es[month_idx], year, time_s);
}

// ── Línea decorativa con degradado simulado ───────────────────────────────
// LVGL 9 no tiene gradiente nativo → simulamos con 3 rectángulos
static void make_accent_line(lv_obj_t* parent, int y) {
    struct Seg { int x, w; lv_color_t col; lv_opa_t opa; };
    const int TH = SS(2);   // grosor de la línea
    Seg segs[] = {
        { 0,                SX(60),  C_ACCENT,  LV_OPA_20 },
        { SX(60),           SX(120), C_ACCENT,  LV_OPA_70 },
        { SX(180),          SX(120), C_ACCENT2, LV_OPA_90 },
        { SX(300),          SX(120), C_ACCENT,  LV_OPA_70 },
        { SX(420),          SX(60),  C_ACCENT,  LV_OPA_20 },
    };
    for (auto& seg : segs) {
        lv_obj_t* r = lv_obj_create(parent);
        lv_obj_set_pos(r, seg.x, y);
        lv_obj_set_size(r, seg.w, TH);
        lv_obj_set_style_bg_color(r, seg.col, 0);
        lv_obj_set_style_bg_opa(r, seg.opa, 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_radius(r, 0, 0);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    }
}

// ── Fila de puntos separadores ────────────────────────────────────────────
// Relleno visual entre nombre y estado con puntitos
static void make_dots_row(lv_obj_t* parent, int y) {
    const int DOT_W  = SX(3);
    const int DOT_SP = SX(5);
    const int START  = LABEL_X + SX(130);
    const int END    = STATUS_X - SX(8);
    for (int x = START; x < END; x += DOT_W + DOT_SP) {
        lv_obj_t* d = lv_obj_create(parent);
        lv_obj_set_pos(d, x, y + ROW_H/2 - 1);
        lv_obj_set_size(d, DOT_W, SS(2));
        lv_obj_set_style_bg_color(d, C_DOTS, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_radius(d, 1, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// API pública
// ═════════════════════════════════════════════════════════════════════════

void splash_init() {
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, C_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_scr_load(s_screen);

    // ── Zona de cabecera con fondo sutil ──────────────────────────────────
    lv_obj_t* header_bg = lv_obj_create(s_screen);
    lv_obj_set_pos(header_bg, 0, 0);
    lv_obj_set_size(header_bg, SCREEN_WIDTH, HEADER_H);
    lv_obj_set_style_bg_color(header_bg, C_CARD, 0);
    lv_obj_set_style_bg_opa(header_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header_bg, 0, 0);
    lv_obj_remove_flag(header_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(header_bg, LV_OBJ_FLAG_SCROLLABLE);

    // ── Título: icono + nombre ────────────────────────────────────────────
    // Icono solar en amarillo
    lv_obj_t* icon = lv_label_create(s_screen);
    lv_obj_set_style_text_font(icon, &FONT_LARGE, 0);
    lv_obj_set_style_text_color(icon, C_ACCENT2, 0);
    lv_label_set_text(icon, LV_SYMBOL_CHARGE);
    lv_obj_set_pos(icon, SX(140), SY(12));

    // Nombre de la aplicación
    lv_obj_t* title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, C_WHITE, 0);
    lv_label_set_text(title, APP_NAME);
    lv_obj_set_pos(title, SX(178), SY(12));

    // ── Línea decorativa ─────────────────────────────────────────────────
    make_accent_line(s_screen, ACCENT_Y);

    // ── Versión y fecha de compilación ────────────────────────────────────
    char build_info[64];
    build_date_str(build_info, sizeof(build_info));

    lv_obj_t* ver = lv_label_create(s_screen);
    lv_obj_set_width(ver, SCREEN_WIDTH);
    lv_obj_set_pos(ver, 0, VERSION_Y);
    lv_obj_set_style_text_align(ver, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ver, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(ver, C_MUTED2, 0);
    lv_label_set_text(ver, build_info);

    // ── Lista de pasos ────────────────────────────────────────────────────
    for (int vi = 0; vi < N_VISIBLE; vi++) {
        int si = VISIBLE_STEPS[vi];
        int y  = LIST_TOP + vi * ROW_H;

        // Dot de color izquierdo
        lv_obj_t* dot = lv_obj_create(s_screen);
        lv_obj_set_pos(dot, DOT_X, y + ROW_H/2 - SS(4));
        lv_obj_set_size(dot, SS(8), SS(8));
        lv_obj_set_style_bg_color(dot, C_MUTED, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_radius(dot, SS(4), 0);   // círculo
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        s_dots[vi] = dot;

        // Nombre del paso
        lv_obj_t* lbl = lv_label_create(s_screen);
        lv_obj_set_pos(lbl, LABEL_X, y + (ROW_H - FONT_SMALL_SIZE) / 2);
        lv_obj_set_style_text_font(lbl, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(lbl, C_MUTED, 0);
        lv_label_set_text(lbl, STEP_NAMES[si]);
        s_step_labels[vi] = lbl;

        // Puntos separadores decorativos
        make_dots_row(s_screen, y);

        // Icono de estado derecha
        lv_obj_t* st = lv_label_create(s_screen);
        lv_obj_set_pos(st, STATUS_X, y + (ROW_H - FONT_SMALL_SIZE) / 2);
        lv_obj_set_style_text_font(st, &FONT_SMALL, 0);
        lv_obj_set_style_text_color(st, C_MUTED, 0);
        lv_label_set_text(st, LV_SYMBOL_MINUS);
        s_step_status[vi] = st;
    }

    // ── Label de detalle ──────────────────────────────────────────────────
    s_lbl_detail = lv_label_create(s_screen);
    lv_obj_set_pos(s_lbl_detail, SX(16), DETAIL_Y);
    lv_obj_set_width(s_lbl_detail, SCREEN_WIDTH - SX(32));
    lv_obj_set_style_text_font(s_lbl_detail, &FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_lbl_detail, C_MUTED2, 0);
    lv_obj_set_style_text_align(s_lbl_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_lbl_detail, "");

    // ── Barra de progreso ─────────────────────────────────────────────────
    s_bar = lv_bar_create(s_screen);
    lv_obj_set_pos(s_bar, SX(16), BAR_Y);
    lv_obj_set_size(s_bar, SCREEN_WIDTH - SX(32), SS(6));
    lv_bar_set_range(s_bar, 0, (int)SplashStep::DONE);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x1A2233), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER,             LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, SS(3),                     LV_PART_MAIN);

    // Indicador con degradado simulado: usamos el color accent
    lv_obj_set_style_bg_color(s_bar, C_ACCENT,   LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, SS(3),         LV_PART_INDICATOR);

    lv_timer_handler();
}

// ── splash_update ─────────────────────────────────────────────────────────
void splash_update(SplashStep step, SplashState state, const char* detail) {
    int si  = (int)step;
    int vi  = visible_idx(si);
    lv_color_t col = state_color(state);

    if (vi >= 0) {
        // Dot de color
        lv_obj_set_style_bg_color(s_dots[vi], col, 0);

        // Nombre del paso con color del estado
        lv_obj_set_style_text_color(s_step_labels[vi], col, 0);

        // Icono de estado
        lv_label_set_text(s_step_status[vi], state_icon(state));
        lv_obj_set_style_text_color(s_step_status[vi], col, 0);
    }

    // Detalle
    if (detail && detail[0]) {
        lv_label_set_text(s_lbl_detail, detail);
        lv_obj_set_style_text_color(s_lbl_detail,
            (state == SplashState::ERROR) ? C_ERR :
            (state == SplashState::WARN)  ? C_WARN : C_MUTED2, 0);
    } else {
        lv_label_set_text(s_lbl_detail, "");
    }

    // Barra — avanza si el estado es terminal
    if (state == SplashState::OK   ||
        state == SplashState::WARN ||
        state == SplashState::ERROR) {
        lv_bar_set_value(s_bar, si + 1, LV_ANIM_ON);
    }

    lv_timer_handler();
    delay(5);
}

// ── splash_finish ─────────────────────────────────────────────────────────
void splash_finish() {
    extern lv_obj_t* g_main_screen;
    lv_scr_load_anim(g_main_screen,
                     LV_SCR_LOAD_ANIM_FADE_IN, 400, 200, true);
    s_screen = nullptr;
}
