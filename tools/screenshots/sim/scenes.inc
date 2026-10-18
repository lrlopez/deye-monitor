#include "pagination_dots.h"
#include "home_button.h"
#include "ui_toast.h"
#include "clock_mode.h"
#include "stats_screen.h"
#include "chart_screen.h"
#include "config_screen.h"
#include "energy_profile.h"
static lv_obj_t* find_label(lv_obj_t* o, const char* txt) {
    if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), txt)) return o;
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) { lv_obj_t* r = find_label(lv_obj_get_child(o, i), txt); if (r) return r; }
    return nullptr;
}
static lv_obj_t* find_ta(lv_obj_t* o, const char* txt) {
    if (lv_obj_check_type(o, &lv_textarea_class) && !strcmp(lv_textarea_get_text(o), txt)) return o;
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) { lv_obj_t* r = find_ta(lv_obj_get_child(o, i), txt); if (r) return r; }
    return nullptr;
}
static void click_label(lv_obj_t* root, const char* txt) {
    lv_obj_t* l = find_label(root, txt);
    if (l) { lv_obj_t* box = lv_obj_get_parent(l); lv_obj_send_event(box, LV_EVENT_CLICKED, nullptr); }
}
static lv_obj_t* find_ph(lv_obj_t* o, const char* txt) {
    if (lv_obj_check_type(o, &lv_textarea_class) && lv_textarea_get_placeholder_text(o) && !strcmp(lv_textarea_get_placeholder_text(o), txt)) return o;
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) { lv_obj_t* r = find_ph(lv_obj_get_child(o, i), txt); if (r) return r; }
    return nullptr;
}
#include "setup_wizard.h"
lv_obj_t* find_dropdown(lv_obj_t* o) {
    if (lv_obj_check_type(o, &lv_dropdown_class)) return o;
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) { lv_obj_t* r = find_dropdown(lv_obj_get_child(o, i)); if (r) return r; }
    return nullptr;
}
static EnergyData sample(int pv, int grid, int batt, int soc, int load) {
    EnergyData e{}; e.pv1_power = pv * 6 / 10; e.pv2_power = pv - e.pv1_power; e.pv_power = pv;
    e.grid_power = grid; e.batt_power = batt; e.batt_soc = soc; e.load_power = load; e.valid = true; return e;
}
static void build_scene(const char* name, lv_obj_t* scr) {
    if (!strcmp(name, "dashboard")) {
        extern volatile uint32_t g_batt_eta; g_batt_eta = (2u << 16) | 130;   // cargando: llena en 2h10
        dashboard_init(scr);
        dashboard_update(sample(3200, -1100, -900, 78, 1200));
        run(1200); dashboard_tick(); run(100);   // >1 s para que el reloj de la barra superior se pinte
    } else if (!strcmp(name, "dash_stale")) {          // 75 s sin datos con WiFi OK
        dashboard_init(scr);
        dashboard_update(sample(3200, -1100, -900, 78, 1200));
        run(600); dashboard_tick();
        g_now_ms += 75000; run(200); dashboard_tick(); run(200);
    } else if (!strcmp(name, "dash_nowifi")) {         // 3 min sin datos y WiFi caído
        dashboard_init(scr);
        dashboard_update(sample(3200, -1100, -900, 78, 1200));
        run(600); dashboard_tick();
        WiFi.state = 6; g_now_ms += 190000; run(200); dashboard_tick(); run(200);
    } else if (!strcmp(name, "dash_nodata")) {         // nunca llegó nada, 40 s desde el arranque
        dashboard_init(scr);
        run(600); g_now_ms += 40000; run(200); dashboard_tick(); run(200);
    } else if (!strcmp(name, "dash_recover")) {        // obsoleto y luego llega un dato
        dashboard_init(scr);
        dashboard_update(sample(3200, -1100, -900, 78, 1200));
        run(600); g_now_ms += 75000; run(200); dashboard_tick(); run(200);
        dashboard_update(sample(2800, 200, 0, 80, 3000)); run(1200); dashboard_tick(); run(200);
    }
    else if (!strcmp(name, "dash_eta")) {            // batería descargando con autonomía estimada
        dashboard_init(scr);
        dashboard_update(sample(0, 350, 900, 41, 1250));
        run(700); dashboard_tick(); run(100);
    }
    else if (!strcmp(name, "flow_exp")) {      // sol, exportando y cargando batería
        extern uint8_t g_dash_view; g_dash_view = 1;
        dashboard_init(scr);
        dashboard_update(sample(4200, -1800, -1500, 78, 900));
        run(700); dashboard_tick(); run(100);
    } else if (!strcmp(name, "flow_imp")) {   // noche: importando y batería descargando
        extern uint8_t g_dash_view; g_dash_view = 1;
        dashboard_init(scr);
        dashboard_update(sample(0, 350, 900, 41, 1250));
        run(900); dashboard_tick(); run(100);
    } else if (!strcmp(name, "flow_idle")) {  // sin flujos
        extern uint8_t g_dash_view; g_dash_view = 1;
        dashboard_init(scr);
        dashboard_update(sample(0, 10, 0, 100, 300));
        run(700); dashboard_tick(); run(100);
    } else if (!strcmp(name, "flow_toggle")) { // tarjetas -> toque en la barra -> flujo
        extern uint8_t g_dash_view; g_dash_view = 0;
        dashboard_init(scr);
        dashboard_update(sample(4200, -1800, -1500, 78, 900));
        run(300);
        lv_obj_t* tb = lv_obj_get_child(scr, 0);
        lv_obj_send_event(tb, LV_EVENT_CLICKED, nullptr);
        run(700);
    }
    else if (!strcmp(name, "toast")) {
        dashboard_init(scr);
        dashboard_update(sample(3200, -1100, -900, 78, 1200));
        pagination_dots_init(scr, 5); pagination_dots_set(0);
        run(300); dashboard_tick();
        ui_toast_show("Toca dos veces la pantalla para volver al inicio");
        run(400);
    }
    else if (!strcmp(name, "clock")) {
        dashboard_init(scr);
        dashboard_update(sample(3200, -1100, -900, 78, 1200));
        run(300);
        clock_mode_set_energy(sample(3200, -1100, -900, 78, 1200));
        clock_mode_tick(true); run(300);
    }
    else if (!strcmp(name, "stats")) {
        stats_screen_init(scr);
        DailyStats d{}; d.valid = true; d.pv_kwh = 9.8f; d.load_kwh = 8.4f; d.import_kwh = 2.1f; d.export_kwh = 3.0f; d.batt_charge_kwh = 1.9f; d.batt_discharge_kwh = 2.2f;
        stats_screen_update(d);
        run(400);
    }
    else if (!strcmp(name, "chart")) {
        chart_screen_init(scr); chart_screen_set_active(true); run(400);
    } else if (!strcmp(name, "chart_week")) {
        chart_screen_init(scr); chart_screen_set_active(true); run(200);
        click_label(scr, "24h"); run(400);
    } else if (!strcmp(name, "chart_hidden")) {   // semana con PV y Carga ocultas
        chart_screen_init(scr); chart_screen_set_active(true); run(200);
        click_label(scr, "24h"); run(200);
        click_label(scr, "PV"); click_label(scr, "Carga"); run(400);
    }
    else if (!strncmp(name, "wiz", 3)) {
        wizard_start(); run(300);
        lv_obj_t* top = lv_layer_top();
        auto ph = [&](const char* p) -> lv_obj_t* { return find_ph(top, p); };
        auto next = [&]() { click_label(top, "Siguiente " LV_SYMBOL_RIGHT); run(200); };
        int n = atoi(name + 3);                       // wiz0..wiz4, wiz5 = lista de redes, wiz6 = teclado, wiz7 = error
        if (n >= 1) { click_label(top, "Empezar " LV_SYMBOL_RIGHT); run(200); }
        if (n == 5) { click_label(top, LV_SYMBOL_REFRESH); run(100); extern void wz_scan_done(); WiFi.scanNetworks(true, false); lv_timer_handler(); g_now_ms += 400; run(500); }
        if (n == 6) { lv_obj_send_event(ph("Nombre de tu red"), LV_EVENT_FOCUSED, nullptr); run(200); }
        if (n == 7) { next(); }                       // WiFi vacío → error rojo + toast
        if (n >= 2 && n != 5 && n != 6 && n != 7) { lv_textarea_set_text(ph("Nombre de tu red"), "MiRed_5G"); lv_textarea_set_text(ph("Clave de tu WiFi"), "secreta123"); next(); }
        if (n >= 3 && n < 5) { lv_textarea_set_text(ph("192.168.1.xxx"), "192.168.1.50"); lv_textarea_set_text(ph("Ver etiqueta del stick"), "2712345678"); next(); }
        if (n >= 4 && n < 5) { lv_textarea_set_text(ph("M\xC3\xADnimo 8 caracteres"), "claveweb1"); lv_textarea_set_text(ph("Repite la clave"), "claveweb1"); next(); }
        if (n >= 8) {                                 // wiz8 = 1 logger, wiz9 = 2 loggers (lista), wiz10 = ninguno
            g_scan_mode = (n == 8) ? 1 : (n == 9 ? 2 : 0);
            lv_textarea_set_text(ph("Ver etiqueta del stick"), "2712345678");
            click_label(top, LV_SYMBOL_GPS); run(700);
        }
        run(300);
    }
    else if (!strcmp(name, "cfg_inv_scan")) {          // Configuración → Inversor con el botón de búsqueda
        config_screen_init(scr); click_label(scr, LV_SYMBOL_EDIT "  Inversor"); run(300);
    }
    else if (!strcmp(name, "cfg_net") || !strcmp(name, "cfg_net_open")) {   // sección Red con la zona horaria
        config_screen_init(scr); click_label(scr, LV_SYMBOL_GPS "  Red"); run(300);
        if (!strcmp(name, "cfg_net_open")) {
            lv_obj_t* dd = lv_obj_get_child(lv_obj_get_child(scr, 0), 0);   // se localiza abajo
            extern lv_obj_t* find_dropdown(lv_obj_t*); dd = find_dropdown(scr);
            if (dd) { lv_dropdown_open(dd); run(300); }
        }
    }
    else if (!strncmp(name, "cfgs", 4)) {               // cfgs0..cfgs7 = secciones de Configuración
        static const char* ic[] = { LV_SYMBOL_WIFI, LV_SYMBOL_EDIT, LV_SYMBOL_CHART, LV_SYMBOL_GPS,
                                    LV_SYMBOL_BELL, LV_SYMBOL_EYE_OPEN, LV_SYMBOL_WARNING, LV_SYMBOL_TRASH };
        static const char* ti[] = { "WiFi", "Inversor", "Gr\xC3\xA1\x66ica", "Red", "Telegram", "Pantalla", "Acceso web", "Datos" };
        config_screen_init(scr);
        const int n = atoi(name + 4);
        if (n >= 0) {
            char lbl[48]; snprintf(lbl, sizeof lbl, "%s  %s", ic[n], ti[n]);
            click_label(scr, lbl);
        }
        run(300);
    }
    else if (!strcmp(name, "profile")) {                // pantalla Perfil de energía
        extern bool g_rich_days; g_rich_days = true;
        energy_profile_init(scr); energy_profile_set_active(true); run(400);
    }
    else if (!strcmp(name, "config_menu")) {
        config_screen_init(scr); run(300);
    } else if (!strcmp(name, "config_wifi")) {
        config_screen_init(scr); click_label(scr, LV_SYMBOL_WIFI "  WiFi"); run(300);
    } else if (!strcmp(name, "config_inv_bad")) {   // IP inválida escrita a mano
        config_screen_init(scr); click_label(scr, LV_SYMBOL_EDIT "  Inversor"); run(100);
        lv_obj_t* ip = find_ta(scr, "192.168.1.50"); if (ip) { lv_textarea_set_text(ip, "192.168.1.30"); lv_textarea_add_text(ip, "0"); }
        run(300);
    }
    // Las pantallas que no son el dashboard llevan el botón «inicio» (como en main.cpp)
    static const char* const with_home[] = { "chart", "stats", "profile", "config", "cfg" };
    for (const char* p : with_home)
        if (!strncmp(name, p, strlen(p))) {
            home_button_init(scr, []() {}); home_button_set_dashboard(false); run(100);
            break;
        }
}
