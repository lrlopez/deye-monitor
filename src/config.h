#pragma once

// ── WiFi ──────────────────────────────────────────────────────────────────
#define WIFI_SSID   "WiFi"
#define WIFI_PASS   "1234567890"

// ── Datalogger Deye (stick WiFi LSW3) ────────────────────────────────────
// IP asignada por tu router al stick WiFi del inversor
#define LOGGER_IP     "192.168.1.214"
#define LOGGER_PORT   8899
// Nº de serie del datalogger en DECIMAL (está en la etiqueta del stick
// o en la app SolarmanPV → Dispositivo → S/N)
#define LOGGER_SERIAL 1234567890UL

// ── Modbus ────────────────────────────────────────────────────────────────
#define MODBUS_UNIT_ID  1     // dirección del inversor en el bus

// ── Registros SUN-6K-SG05 (Holding Registers, FC=03) ─────────────────────
// Leemos un bloque contiguo: registros 169–191 (23 registros)
// NOTA: verifica con el mapa oficial de tu firmware; la comunidad usa:
//   https://github.com/kbialek/deye-inverter-mqtt/blob/main/config/deye_sg04lp3.yaml
#define REG_BASE         169
#define REG_COUNT         23

// Offsets desde REG_BASE:
#define IDX_GRID_POWER    (169 - REG_BASE)  //  0 – W signed (+import / −export)
#define IDX_LOAD_POWER    (178 - REG_BASE)  //  9 – W
#define IDX_BATT_SOC      (184 - REG_BASE)  // 15 – %
#define IDX_PV1_POWER     (186 - REG_BASE)  // 17 – W
#define IDX_PV2_POWER     (187 - REG_BASE)  // 19 – W
#define IDX_BATT_POWER    (190 - REG_BASE)  // 21 – W signed (+carga / −descarga)

// ── Polling ───────────────────────────────────────────────────────────────
#define POLL_INTERVAL_MS  5000
