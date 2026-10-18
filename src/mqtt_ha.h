#pragma once
#include <stdio.h>
#include <stddef.h>
#include <math.h>

// Mensajes MQTT y autodescubrimiento de Home Assistant (DEYE-30), sin dependencias de
// Arduino/PubSubClient para poder probarlos en `pio test -e native`.
//
// Un único topic de estado (<prefix>/state) con todas las medidas en JSON y un sensor de
// Home Assistant por clave, que extrae su valor con value_template. La disponibilidad se
// publica (retained) en <prefix>/status: "online" al conectar y "offline" como Last Will.

struct MqttSensor {
    const char* key;       // clave en el JSON de estado y parte del unique_id
    const char* name;
    const char* unit;      // "" = sin unidad
    const char* dev_cla;   // device_class de HA ("" = ninguna)
    const char* stat_cla;  // state_class ("measurement" / "total_increasing")
};

static const MqttSensor MQTT_SENSORS[] = {
    { "pv_w",       "Producci\xC3\xB3n solar",        "W",   "power",       "measurement"      },
    { "grid_w",     "Red (+importa / -exporta)",       "W",   "power",       "measurement"      },
    { "batt_w",     "Bater\xC3\xAD\x61 potencia",     "W",   "power",       "measurement"      },
    { "batt_soc",   "Bater\xC3\xAD\x61",              "%",   "battery",     "measurement"      },
    { "load_w",     "Consumo",                         "W",   "power",       "measurement"      },
    { "batt_v",     "Tensi\xC3\xB3n bater\xC3\xAD\x61", "V", "voltage",     "measurement"      },
    { "temp_c",     "Temperatura inversor",            "\xC2\xB0\x43", "temperature", "measurement" },
    { "pv_kwh",     "Producci\xC3\xB3n hoy",           "kWh", "energy",      "total_increasing" },
    { "load_kwh",   "Consumo hoy",                     "kWh", "energy",      "total_increasing" },
    { "import_kwh", "Importado hoy",                   "kWh", "energy",      "total_increasing" },
    { "export_kwh", "Exportado hoy",                   "kWh", "energy",      "total_increasing" },
    { "chg_kwh",    "Carga bater\xC3\xAD\x61 hoy",    "kWh", "energy",      "total_increasing" },
    { "dis_kwh",    "Descarga bater\xC3\xAD\x61 hoy", "kWh", "energy",      "total_increasing" },
};
static const size_t MQTT_SENSOR_COUNT = sizeof(MQTT_SENSORS) / sizeof(MQTT_SENSORS[0]);

struct MqttState {
    int   pv_w = 0, grid_w = 0, batt_w = 0, batt_soc = 0, load_w = 0;
    float batt_v = 0;             // 0 = no disponible
    float temp_c = NAN;           // NAN = no disponible
    bool  daily_valid = false;
    float pv_kwh = 0, load_kwh = 0, import_kwh = 0, export_kwh = 0, chg_kwh = 0, dis_kwh = 0;
};

// homeassistant/sensor/<node>/<key>/config
inline int mqtt_discovery_topic(char* out, size_t n, const char* node, const char* key) {
    return snprintf(out, n, "homeassistant/sensor/%s/%s/config", node, key);
}

// Carga útil de descubrimiento de un sensor. `node` es un identificador estable del equipo
// (el nombre mDNS), `prefix` el prefijo de los topics propios. Devuelve la longitud o <0/>=n
// si no cabe.
inline int mqtt_discovery_payload(char* out, size_t n, const char* node, const char* prefix,
                                  const MqttSensor& s, const char* sw_version) {
    int pos = snprintf(out, n,
        "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"stat_t\":\"%s/state\","
        "\"val_tpl\":\"{{ value_json.%s }}\",\"avty_t\":\"%s/status\"",
        s.name, node, s.key, prefix, s.key, prefix);
    if (pos < 0 || (size_t)pos >= n) return -1;
    if (s.unit[0])     pos += snprintf(out + pos, n - pos, ",\"unit_of_meas\":\"%s\"", s.unit);
    if (s.dev_cla[0])  pos += snprintf(out + pos, n - pos, ",\"dev_cla\":\"%s\"", s.dev_cla);
    if (s.stat_cla[0]) pos += snprintf(out + pos, n - pos, ",\"stat_cla\":\"%s\"", s.stat_cla);
    pos += snprintf(out + pos, n - pos,
        ",\"dev\":{\"ids\":[\"%s\"],\"name\":\"Deye Monitor\",\"mf\":\"Deye\","
        "\"mdl\":\"SUN-6K-SG05\",\"sw\":\"%s\"}}", node, sw_version);
    return ((size_t)pos < n) ? pos : -1;
}

// JSON de estado. Los valores no disponibles van como null (HA los muestra «desconocido»).
inline int mqtt_state_payload(char* out, size_t n, const MqttState& s) {
    char bv[16], tc[16];
    if (s.batt_v > 0)   snprintf(bv, sizeof bv, "%.1f", (double)s.batt_v); else snprintf(bv, sizeof bv, "null");
    if (!isnan(s.temp_c)) snprintf(tc, sizeof tc, "%.1f", (double)s.temp_c); else snprintf(tc, sizeof tc, "null");
    int pos = snprintf(out, n,
        "{\"pv_w\":%d,\"grid_w\":%d,\"batt_w\":%d,\"batt_soc\":%d,\"load_w\":%d,"
        "\"batt_v\":%s,\"temp_c\":%s",
        s.pv_w, s.grid_w, s.batt_w, s.batt_soc, s.load_w, bv, tc);
    if (pos < 0 || (size_t)pos >= n) return -1;
    if (s.daily_valid)
        pos += snprintf(out + pos, n - pos,
            ",\"pv_kwh\":%.2f,\"load_kwh\":%.2f,\"import_kwh\":%.2f,\"export_kwh\":%.2f,"
            "\"chg_kwh\":%.2f,\"dis_kwh\":%.2f",
            (double)s.pv_kwh, (double)s.load_kwh, (double)s.import_kwh, (double)s.export_kwh,
            (double)s.chg_kwh, (double)s.dis_kwh);
    pos += snprintf(out + pos, n - pos, "}");
    return ((size_t)pos < n) ? pos : -1;
}

// Prefijo y host válidos: sin barras al principio/final ni comodines (# +) ni espacios
inline bool mqtt_valid_prefix(const char* p) {
    if (!p || !*p || *p == '/') return false;
    size_t l = 0;
    for (; p[l]; l++) if (p[l] == '#' || p[l] == '+' || p[l] == ' ' || p[l] == '"') return false;
    return l <= 31 && p[l - 1] != '/';
}
