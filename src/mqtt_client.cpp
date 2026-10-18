#include "mqtt_client.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "app_state.h"
#include "config.h"
#include "storage.h"
#include "solarman.h"
#include "mqtt_ha.h"
#include "safe_serial.h"

static volatile bool s_connected = false;
bool mqtt_connected() { return s_connected; }

static const uint32_t RECONNECT_MS = 15000;   // entre intentos de conexión
static const uint32_t MIN_PUBLISH_MS = 5000;  // entre publicaciones de estado
static const uint32_t HEARTBEAT_MS = 60000;   // republicar aunque no haya lectura nueva

// Publica el descubrimiento de todos los sensores (retained). false si alguna publicación falla.
static bool publish_discovery(PubSubClient& c, const char* node, const char* prefix) {
    char topic[96], payload[768];
    for (size_t i = 0; i < MQTT_SENSOR_COUNT; i++) {
        mqtt_discovery_topic(topic, sizeof topic, node, MQTT_SENSORS[i].key);
        if (mqtt_discovery_payload(payload, sizeof payload, node, prefix, MQTT_SENSORS[i], APP_VERSION) < 0) return false;
        if (!c.publish(topic, payload, true)) return false;
    }
    return true;
}

static bool publish_state(PubSubClient& c, const char* prefix) {
    EnergyData e{};
    DailyStats d{};
    if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        e = g_energy; d = g_daily; xSemaphoreGive(g_mutex);
    }
    if (!e.valid) return true;   // aún sin datos: nada que publicar

    MqttState s;
    s.pv_w = (int)e.pv_power; s.grid_w = (int)e.grid_power; s.batt_w = (int)e.batt_power;
    s.batt_soc = (int)e.batt_soc; s.load_w = (int)e.load_power; s.batt_v = e.batt_voltage_v;
    if (inv_temp_available(d)) s.temp_c = d.inv_temp_c;
    if (d.valid) {
        s.daily_valid = true;
        s.pv_kwh = d.pv_kwh; s.load_kwh = d.load_kwh; s.import_kwh = d.import_kwh;
        s.export_kwh = d.export_kwh; s.chg_kwh = d.batt_charge_kwh; s.dis_kwh = d.batt_discharge_kwh;
    }
    char topic[64], payload[384];
    snprintf(topic, sizeof topic, "%s/state", prefix);
    if (mqtt_state_payload(payload, sizeof payload, s) < 0) return false;
    return c.publish(topic, payload, false);
}

static void mqtt_task(void*) {
    WiFiClient net;
    PubSubClient client(net);
    client.setBufferSize(768);
    client.setSocketTimeout(5);

    MqttConfig cfg{};
    char applied_key[200] = "";      // huella de la configuración con la que se conectó
    uint32_t last_cfg_ms = 0, last_try_ms = 0, last_pub_ms = 0;
    uint32_t seen_seq = 0;
    bool need_discovery = true;

    for (;;) {
        const uint32_t now = millis();

        if (last_cfg_ms == 0 || now - last_cfg_ms > 5000) {
            cfg = Storage.loadMqttConfig();
            last_cfg_ms = now ? now : 1;
        }
        char key[200];
        snprintf(key, sizeof key, "%d|%s|%u|%s|%s|%s", (int)cfg.enabled, cfg.host, (unsigned)cfg.port,
                 cfg.user, cfg.pass, cfg.prefix);
        if (strcmp(key, applied_key) != 0) {            // configuración nueva: reconectar
            if (client.connected()) client.disconnect();
            s_connected = false;
            applied_key[0] = '\0';
            last_try_ms = 0;
        }

        const bool usable = cfg.enabled && cfg.host[0] && mqtt_valid_prefix(cfg.prefix) &&
                            WiFi.status() == WL_CONNECTED;
        if (!usable) {
            if (client.connected()) client.disconnect();
            s_connected = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (!client.connected()) {
            s_connected = false;
            if (last_try_ms == 0 || now - last_try_ms >= RECONNECT_MS) {
                last_try_ms = now ? now : 1;
                client.setServer(cfg.host, cfg.port);
                char node[40];
                strlcpy(node, g_cfg.mdns_hostname[0] ? g_cfg.mdns_hostname : MDNS_HOSTNAME, sizeof node);
                char cid[64], status[64];
                snprintf(cid, sizeof cid, "deye-%s", node);
                snprintf(status, sizeof status, "%s/status", cfg.prefix);
                APP_LOGI("MQTT", "Conectando a %s:%u...", cfg.host, (unsigned)cfg.port);
                const bool ok = cfg.user[0]
                    ? client.connect(cid, cfg.user, cfg.pass, status, 0, true, "offline")
                    : client.connect(cid, nullptr, nullptr, status, 0, true, "offline");
                if (ok) {
                    client.publish(status, "online", true);
                    need_discovery = true;
                    strlcpy(applied_key, key, sizeof applied_key);
                    seen_seq = 0;                       // publicar el estado enseguida
                    last_pub_ms = 0;
                    s_connected = true;
                    APP_LOGI("MQTT", "Conectado");
                } else {
                    APP_LOGW("MQTT", "No se pudo conectar (rc=%d)", client.state());
                    strlcpy(applied_key, key, sizeof applied_key);   // no reintentar en bucle por cambio de config
                }
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        client.loop();

        if (need_discovery) {
            need_discovery = !publish_discovery(client, g_cfg.mdns_hostname[0] ? g_cfg.mdns_hostname : MDNS_HOSTNAME,
                                                cfg.prefix);
            if (need_discovery) APP_LOGW("MQTT", "Descubrimiento HA incompleto: se reintentará");
        }

        const uint32_t seq = g_energy_seq;
        if ((seq != seen_seq && now - last_pub_ms >= MIN_PUBLISH_MS) || now - last_pub_ms >= HEARTBEAT_MS) {
            if (publish_state(client, cfg.prefix)) { seen_seq = seq; last_pub_ms = now ? now : 1; }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void mqtt_start() {
    xTaskCreatePinnedToCore(mqtt_task, "mqtt", 6144, nullptr, 1, nullptr, 0);
}
