#include "clock_mode.h"
#include "config.h"
#include "ui_constants.h"
#include "storage.h"
#include <Arduino.h>
#include <time.h>

static lv_obj_t* s_ov        = nullptr;     // overlay (se crea la primera vez y después solo se oculta/muestra)
static lv_obj_t* s_lbl_time  = nullptr;
static lv_obj_t* s_lbl_date  = nullptr;
static lv_obj_t* s_lbl_pv    = nullptr;
static lv_obj_t* s_lbl_load  = nullptr;
static lv_obj_t* s_lbl_batt  = nullptr;
static bool      s_visible   = false;
static bool      s_enabled   = true;
static EnergyData s_e{};
static bool      s_e_dirty   = false;
static uint32_t  s_last_ms   = 0;

static lv_obj_t* make_metric(lv_obj_t* parent, lv_color_t col)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &FONT_LARGE, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_label_set_text(l, "--");
    return l;
}

static void create_overlay()
{
    s_ov = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_ov, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(s_ov, 0, 0);
    lv_obj_set_style_bg_color(s_ov, C_OVERLAY, 0);
    lv_obj_set_style_bg_opa(s_ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ov, 0, 0);
    lv_obj_set_style_radius(s_ov, 0, 0);
    lv_obj_set_style_pad_all(s_ov, 0, 0);
    lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
    // Clicable a propósito: es lo que impide que el toque de "despertar" llegue a la UI de debajo
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_time = lv_label_create(s_ov);
    lv_obj_set_style_text_font(s_lbl_time, &FONT_CLOCK, 0);
    lv_obj_set_style_text_color(s_lbl_time, C_WHITE, 0);
    lv_label_set_text(s_lbl_time, "--:--");
    lv_obj_align(s_lbl_time, LV_ALIGN_TOP_MID, 0, SY(30));

    s_lbl_date = lv_label_create(s_ov);
    lv_obj_set_style_text_font(s_lbl_date, &FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(s_lbl_date, C_MUTED2, 0);
    lv_label_set_text(s_lbl_date, "");
    lv_obj_align_to(s_lbl_date, s_lbl_time, LV_ALIGN_OUT_BOTTOM_MID, 0, SY(8));

    // Tres datos en fila, repartidos: solar · casa · batería
    lv_obj_t* row = lv_obj_create(s_ov);
    lv_obj_set_size(row, SCREEN_WIDTH, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -SY(14));
    s_lbl_pv   = make_metric(row, C_WARN);
    s_lbl_load = make_metric(row, lv_color_hex(0xBB6BD9));
    s_lbl_batt = make_metric(row, C_OK);
}

static void refresh()
{
    struct tm ti;
    if (getLocalTime(&ti, 0)) {
        static const char* DIAS[]  = { "Dom","Lun","Mar","Mi\xC3\xA9","Jue","Vie","S\xC3\xA1\x62" };
        static const char* MESES[] = { "Ene","Feb","Mar","Abr","May","Jun","Jul","Ago","Sep","Oct","Nov","Dic" };
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
        lv_label_set_text(s_lbl_time, buf);
        snprintf(buf, sizeof(buf), "%s %d %s", DIAS[ti.tm_wday % 7], ti.tm_mday, MESES[ti.tm_mon % 12]);
        lv_label_set_text(s_lbl_date, buf);
    }
    if (s_e.valid) {
        lv_label_set_text_fmt(s_lbl_pv,   LV_SYMBOL_SUN " %d W", (int)s_e.pv_power);
        lv_label_set_text_fmt(s_lbl_load, LV_SYMBOL_HOME " %d W", (int)s_e.load_power);
        lv_label_set_text_fmt(s_lbl_batt, LV_SYMBOL_BATTERY_FULL " %d%%", (int)s_e.batt_soc);
    }
    s_e_dirty = false;
}

void clock_mode_set_energy(const EnergyData& e)
{
    s_e = e;
    s_e_dirty = true;
}

bool clock_mode_visible() { return s_visible; }

void clock_mode_init()                 { s_enabled = Storage.loadClockMode(); }
bool clock_mode_enabled()              { return s_enabled; }
void clock_mode_set_enabled(bool en)   { s_enabled = en; Storage.saveClockMode(en); }

void clock_mode_tick(bool active)
{
    active = active && s_enabled;      // desactivado en el ajuste: nunca se muestra (y se retira si estaba)
    if (active) {
        if (!s_visible) {
            if (!s_ov) create_overlay();
            lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
            s_visible = true;
            refresh();
            s_last_ms = millis();
            return;
        }
        // Visible: refrescar el reloj cada segundo (o antes si llegó un dato nuevo)
        if (s_e_dirty || millis() - s_last_ms >= 1000) {
            s_last_ms = millis();
            refresh();
        }
    } else if (s_visible) {
        lv_obj_add_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
        s_visible = false;
    }
}
