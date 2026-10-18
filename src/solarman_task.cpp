#include "solarman_task.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>

#include "app_state.h"
#include "config.h"
#include "solarman.h"
#include "recorder.h"
#include "alert_engine.h"
#include "battery_estimate.h"
#include "logger_discovery.h"
#include "psram_cache.h"
#include "safe_serial.h"

static const uint32_t NTP_VALID_EPOCH = 1700000000UL;   // por debajo, el reloj aún no está sincronizado

// ── WiFi: reconexión y puesta en marcha de servicios ──────────────────────
// Devuelve true si hay conexión y se puede sondear el logger. Si no, ya ha
// esperado lo necesario y el llamador debe volver a iterar.
static bool wifi_service() {
    static bool s_ever_connected = false;
    static uint32_t s_last_reconnect = 0;

    if (WiFi.status() != WL_CONNECTED) {
        g_wifi_connected = false;
        // Sin SSID configurado no hay nada que intentar
        if (g_cfg.wifi_ssid[0] == '\0') {
            vTaskDelay(pdMS_TO_TICKS(5000));
            return false;
        }
        if (millis() - s_last_reconnect > 15000) {
            APP_LOGI("WiFi", "Reconectando...");
            // Solo desconectar si alguna vez estuvo conectado;
            // en P4 llamar disconnect() antes de la primera conexión puede crashear.
            if (s_ever_connected) WiFi.disconnect();
            WiFi.begin(g_cfg.wifi_ssid, g_cfg.wifi_pass);
            s_last_reconnect = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        return false;
    }

    // WiFi recién conectado
    if (!g_wifi_connected) {
        g_wifi_connected = true;
        s_ever_connected = true;
        APP_LOGI("WiFi", "Conectado: %s", WiFi.localIP().toString().c_str());

        // Esperar NTP hasta 10 s (no bloquea LVGL: corre en el Core 1)
        uint32_t ntp_wait = millis();
        while (time(nullptr) < (time_t)NTP_VALID_EPOCH && millis() - ntp_wait < 10000)
            vTaskDelay(pdMS_TO_TICKS(500));
        if (time(nullptr) > (time_t)NTP_VALID_EPOCH) {
            APP_LOGI("NTP", "Sincronizado");
            // Corregir los slots del cache que se inicializaron en begin()
            // con el reloj en 1970 (antes de tener WiFi/NTP).
            Cache.reinitAfterNtp();
        } else {
            APP_LOGW("NTP", "Timeout");
        }
        if (MDNS.begin(g_cfg.mdns_hostname)) {
            MDNS.addService("http", "tcp", 80);
            APP_LOGI("mDNS", "Activo: %s.local", g_cfg.mdns_hostname);
        } else {
            APP_LOGW("mDNS", "Error al iniciar");
        }
    }
    return true;
}

// ── DEYE-09: redescubrir el logger si lleva un rato sin responder ─────────
// Posible cambio de IP por DHCP. Como máximo cada 5 min, y solo tras ≥8 fallos
// seguidos (el aviso de Telegram salta en el 5º).
static void maybe_rediscover_logger(uint16_t consecutive_failures) {
    static uint32_t s_last_ms = 0;
    if (consecutive_failures < 8 || g_cfg.logger_serial == 0) return;
    if (s_last_ms != 0 && millis() - s_last_ms < 300000UL) return;
    s_last_ms = millis();

    char found_ip[16];
    if (!logger_discover(g_cfg.logger_serial, MODBUS_UNIT_ID, found_ip, sizeof(found_ip))) return;
    if (strcmp(found_ip, g_cfg.logger_ip) == 0) return;

    APP_LOGI("Discovery", "IP del logger: %s -> %s (guardada)", g_cfg.logger_ip, found_ip);
    strncpy(g_cfg.logger_ip, found_ip, sizeof(g_cfg.logger_ip) - 1);
    g_cfg.logger_ip[sizeof(g_cfg.logger_ip) - 1] = '\0';
    Storage.saveConfig(g_cfg);   // el SolarmanClient apunta a g_cfg.logger_ip: ya usa la nueva
}

// ── Tarea ─────────────────────────────────────────────────────────────────
void solarmanTask(void* /*pv*/) {
    SolarmanClient client(g_cfg.logger_ip, LOGGER_PORT, g_cfg.logger_serial, MODBUS_UNIT_ID);
    EnergyData local_e{};
    DailyStats local_d{};   // última lectura diaria buena (fetchAll no la toca si falla)
    Recorder    recorder;
    AlertEngine alerts;
    BatteryEstimator batt_est;   // autonomía estimada (DEYE-33)

    // Los diarios se leen cada POLL_DAILY_MS; el primero justo tras la primera
    // lectura instantánea
    uint32_t last_daily_ms = millis() - POLL_DAILY_MS + POLL_INTERVAL_MS;

    for (;;) {
        if (!wifi_service()) continue;

        // ── Lectura del logger: UNA conexión TCP (DEYE-08) ────────────────
        // Diarios cada POLL_DAILY_MS; en los minutos alrededor de medianoche cada
        // 10 s, para que la última lectura del día esté lo más cerca posible del
        // cambio de día (DEYE-01).
        uint32_t daily_period = POLL_DAILY_MS;
        bool     near_midnight_end = false;   // últimos minutos del día: rechazar contadores a cero
        {
            time_t t_now = time(nullptr);
            if (t_now > (time_t)NTP_VALID_EPOCH) {
                struct tm lt; localtime_r(&t_now, &lt);
                if (lt.tm_hour == 23 && lt.tm_min >= 55) near_midnight_end = true;
                if ((lt.tm_hour == 23 && lt.tm_min >= 58) ||
                    (lt.tm_hour == 0  && lt.tm_min < 2))
                    daily_period = 10000;
            }
        }
        const bool want_daily = (millis() - last_daily_ms >= daily_period);
        bool daily_ok = false;

        if (client.fetchAll(local_e, local_d, want_daily, daily_ok)) {
            alerts.onLoggerOk();
            APP_LOGD("Live", "PV:%dW Grid:%dW Bat:%dW(%d%%) Load:%dW",
                     (int)local_e.pv_power, (int)local_e.grid_power,
                     (int)local_e.batt_power, (int)local_e.batt_soc,
                     (int)local_e.load_power);
            g_batt_eta = battery_eta_pack(batt_est.update((int)local_e.batt_soc, (int)local_e.batt_power,
                                                          (uint32_t)g_cfg.bat_cap_w));
            if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                g_energy = local_e;
                g_energy_seq++;
                xSemaphoreGive(g_mutex);
            } else {
                APP_LOGW("Live", "mutex ocupado: lectura no publicada");
            }
        } else {
            alerts.onLoggerFail();
            batt_est.reset();
            g_batt_eta = 0;   // sin datos vivos no hay estimación
        }

        const uint32_t now_ep = (uint32_t)time(nullptr);

        // ── Datos diarios (leídos arriba en la misma conexión) ─────────────
        if (daily_ok) {
            last_daily_ms = millis();
            // Actualizar g_daily para el servidor web
            if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                g_daily = local_d;
                g_daily_seq++;
                xSemaphoreGive(g_mutex);
            }
            recorder.onDailyFetched(local_d, now_ep, near_midnight_end);
        }

        // ── Grabación del histórico ───────────────────────────────────────
        if (now_ep >= NTP_VALID_EPOCH) {
            Recorder::Result res = recorder.onSample(local_e, local_d, now_ep,
                                                     alerts.solarActive());
            if (res.startup_done_now) alerts.restoreSolarState(now_ep);
            // Resetear el estado solar al cambiar de día: de noche no hay producción
            if (res.day_changed)      alerts.onNewDay();
        }

        // ── Alertas ───────────────────────────────────────────────────────
        if (recorder.started() && local_e.valid)
            alerts.evaluate(local_e, now_ep);
        alerts.checkFlashSpace();            // DEYE-46 (como mucho cada 15 min)
        if (daily_ok) alerts.evaluateTemp(local_d);       // DEYE-71 (cada lectura diaria, ~60 s)
        if (now_ep >= NTP_VALID_EPOCH) alerts.checkDailyReport(now_ep);   // DEYE-71

        maybe_rediscover_logger(alerts.loggerFailures());

        // DEYE-08: con el logger caído el intervalo se duplica (hasta 60 s) tras el
        // 5º fallo, en vez de reintentar cada 5 s indefinidamente.
        vTaskDelay(pdMS_TO_TICKS(alerts.pollDelayMs(POLL_INTERVAL_MS, 60000)));
    }
}
