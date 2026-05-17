#pragma once
#include <lvgl.h>

// ── Resolución y fuentes (definir en config.h o platformio.ini) ───────────
// #define SCREEN_WIDTH    480
// #define SCREEN_HEIGHT   270
// #define FONT_SMALL_SIZE  12
// #define FONT_SMALL  lv_font_montserrat_12
// #define FONT_NORMAL_SIZE 14
// #define FONT_NORMAL lv_font_montserrat_14
// #define FONT_LARGE_SIZE  28
// #define FONT_LARGE  lv_font_montserrat_28

// ── Macros de escala ──────────────────────────────────────────────────────
// Escalan un valor de referencia (diseñado a 480×270) a la resolución actual
#define SX(px)  ((px) * SCREEN_WIDTH  / 480)
#define SY(px)  ((px) * SCREEN_HEIGHT / 270)

// Escala uniforme (la menor de las dos dimensiones)
// Útil para radios, grosores, iconos
#define SS(px)  (SX(px) < SY(px) ? SX(px) : SY(px))

// ── Primitivas de layout ──────────────────────────────────────────────────
#define UI_MARGIN         SX(4)          // margen exterior de pantalla
#define UI_GAP            SX(4)          // hueco entre tarjetas
#define UI_PAD            SX(8)          // padding interno de secciones
#define UI_RADIUS         SS(8)          // radio de esquinas
#define UI_BORDER         1              // grosor de borde (siempre 1px)
#define UI_DOT_SZ         SS(8)          // cuadradito de leyenda
#define UI_SEP_H          1              // separador horizontal

// ── Barra de navegación ───────────────────────────────────────────────────
// Calculada a partir del tamaño de fuente normal + padding vertical
#define NAV_H             (FONT_NORMAL_SIZE + SY(10))
#define NAV_BTN_W         SX(44)
#define NAV_BTN_H         (NAV_H - 4)

// ── Barra superior del dashboard ──────────────────────────────────────────
#define TOP_BAR_H         (FONT_NORMAL_SIZE + SY(8))

// ── Dashboard: tarjetas 2×2 ───────────────────────────────────────────────
#define DASH_CARD_W       ((SCREEN_WIDTH  - 2*UI_MARGIN - UI_GAP) / 2)
#define DASH_CARD_H       ((SCREEN_HEIGHT - TOP_BAR_H - 2*UI_MARGIN - UI_GAP) / 2)

// ── Stats screen: donuts ──────────────────────────────────────────────────
#define DONUT_R           (SCREEN_HEIGHT / 5)        // ~54px @ 270
#define ARC_W             (DONUT_R / 3)              // ~18px
#define STATS_COL_W       (SCREEN_WIDTH / 2)
#define STATS_NAV_H       NAV_H
#define STATS_DONUT_CY    (STATS_NAV_H + SY(75))
#define STATS_LEGEND_Y    (STATS_NAV_H + SY(152))
#define STATS_LEG_ROW_H   SY(30)

// ── Chart screen ──────────────────────────────────────────────────────────
// El padding izquierdo debe alojar las etiquetas del eje Y
#define CH_PAD_L          (FONT_SMALL_SIZE * 3 + 2)  // ~38px @ 12pt
#define CH_PAD_R          SX(4)
#define CH_PAD_TV         SX(4)

#define CHART_PWR_Y       NAV_H
#define CHART_PWR_H       (SCREEN_HEIGHT * 138 / 270)
#define CHART_LEG_Y       (CHART_PWR_Y + CHART_PWR_H)
#define CHART_LEG_H       (FONT_SMALL_SIZE + SY(4))
#define CHART_SOC_Y       (CHART_LEG_Y + CHART_LEG_H)
#define CHART_SOC_H       (SCREEN_HEIGHT * 54 / 270)
#define CHART_XLAB_Y      (CHART_SOC_Y + CHART_SOC_H + 2)

// Posición X en pantalla para una hora h (0-23) en el chart
#define CHART_CONTENT_W   (SCREEN_WIDTH - CH_PAD_L - CH_PAD_R)
#define CHART_HOUR_X(h)   (CH_PAD_L + (int)((h) * CHART_CONTENT_W / 23.0f + 0.5f))

// ── Summary screen: barras semanales ─────────────────────────────────────
#define SUMM_NAV_H        NAV_H
#define SUMM_LEG_H        (FONT_SMALL_SIZE * 2 + SY(6))  // 2 líneas de leyenda
#define SUMM_CONT_H       (SCREEN_HEIGHT - SUMM_NAV_H - SUMM_LEG_H)
#define SUMM_ROW_H        (SUMM_CONT_H / 7)
#define SUMM_DAY_W        SX(44)
#define SUMM_BAR_X        SUMM_DAY_W
#define SUMM_BAR_W        (SCREEN_WIDTH - SUMM_BAR_X - SX(4))
#define SUMM_BAR_H        SS(10)
#define SUMM_BAR_GAP      SS(4)

// ── Config screen ─────────────────────────────────────────────────────────
#define CFG_SECTION_W     (SCREEN_WIDTH - 20)
#define CFG_LBL_W         (SCREEN_WIDTH / 4)         // ~120px @ 480
#define CFG_FIELD_H       (FONT_NORMAL_SIZE + SY(20))
#define CFG_SEC_PAD       SX(10)
#define CFG_SCAN_BTN_W    SX(34)
#define CFG_FIELD_W       (CFG_SECTION_W - CFG_LBL_W - CFG_SEC_PAD \
                           - CFG_SCAN_BTN_W - SX(16))
#define CFG_ROW_H         (CFG_FIELD_H + SY(6))

// ── Popups ────────────────────────────────────────────────────────────────
#define POPUP_PAD         SX(7)
#define POPUP_ROW_H       (FONT_SMALL_SIZE + SY(6))

// Popup de chart_screen
#define POPUP_CHART_W     SX(168)
#define POPUP_CHART_H     (FONT_SMALL_SIZE*6 + POPUP_PAD*2 + SY(30))

// Popup de stats_screen y summary_screen
#define POPUP_STATS_W     SX(220)
#define POPUP_STATS_H     SY(200)

// ── Labels del eje Y ─────────────────────────────────────────────────────
// Genera el texto de un tick de eje Y en W → "Xk"
// (solo para uso en add_y_labels())
#define YAXIS_UNIT_PWR    "k"
#define YAXIS_UNIT_SOC    "%"

// ── Paleta ───────────────────────────────────────────────────────────────
#define C_BG        lv_color_hex(0x0D1117)
#define C_CARD      lv_color_hex(0x161B22)
#define C_WHITE     lv_color_hex(0xEAEAEA)
#define C_MUTED     lv_color_hex(0x4E5A6E)
#define C_MUTED2    lv_color_hex(0x6E7A8E)
#define C_OK        lv_color_hex(0x2ECC71)
#define C_ERR       lv_color_hex(0xE74C3C)
#define C_WARN      lv_color_hex(0xF5C518)
#define C_BTN       lv_color_hex(0x1F6FEB)

#define C_RUN       lv_color_hex(0x4A9EFF)
#define C_ACCENT    lv_color_hex(0x1F6FEB)
#define C_ACCENT2   lv_color_hex(0xF5C518)
#define C_DOTS      lv_color_hex(0x1E2D45)
#define C_LIST      lv_color_hex(0x1C2128)

#define C_OVERLAY   lv_color_hex(0x000000)
#define C_HDR_BG    lv_color_hex(0x0D1117)
#define C_BORDER    lv_color_hex(0x30363D)
#define C_HAS_DATA  lv_color_hex(0x2ECC71)
#define C_TODAY_BG  lv_color_hex(0x1B3358)
#define C_SEL_BG    lv_color_hex(0x1F6FEB)
#define C_DISABLED  lv_color_hex(0x252D3A)
#define C_PRESS     lv_color_hex(0x1A2236)
