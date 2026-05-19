#pragma once

// ── WiFi ──────────────────────────────────────────────────────────────────
#define WIFI_SSID   ""
#define WIFI_PASS   ""

// ── Datalogger Deye (stick WiFi LSW3) ────────────────────────────────────
// IP asignada por tu router al stick WiFi del inversor
#define LOGGER_IP     "192.168.1.x"
#define LOGGER_PORT   8899
// Nº de serie del datalogger en DECIMAL (está en la etiqueta del stick
// o en la app SolarmanPV → Dispositivo → S/N)
#define LOGGER_SERIAL 0UL

// ── Aplicación ────────────────────────────────────────────────────────────
#define APP_NAME        "Deye Monitor"
#define APP_VERSION     "1.0.4"

// ── NTP ───────────────────────────────────────────────────────────────────
#define NTP_SERVER1     "es.pool.ntp.org"
#define NTP_SERVER2     "time.cloudflare.com"
#define TIMEZONE        "CET-1CEST,M3.5.0,M10.5.0/3"   // España peninsula

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
#define IDX_BATT_POWER    (190 - REG_BASE)  // 21 – W signed (+descarga / −carga)

// ── Registros energía diaria (FC=03, unidad: 0.1 kWh) ────────────────────
#define REG_DAILY_BASE    70
#define REG_DAILY_COUNT   39         // registros 70-108

#define IDX_D_BATT_CHG   (70 - REG_DAILY_BASE)  // 8  carga batería
#define IDX_D_BATT_DIS   (71 - REG_DAILY_BASE)  // 9  descarga batería
#define IDX_D_GRID_BUY   (76 - REG_DAILY_BASE)  // 7  importado
#define IDX_D_GRID_SELL  (77 - REG_DAILY_BASE)  // 6  exportado
#define IDX_D_LOAD       (84 - REG_DAILY_BASE)  // 14 consumo carga
#define IDX_D_PV         (108 - REG_DAILY_BASE)  // 23 producción FV total
// ── Polling ───────────────────────────────────────────────────────────────
#define POLL_INTERVAL_MS  5000
#define POLL_DAILY_MS     60000UL

// ── Escala gráfica ────────────────────────────────────────────────────────
#define CHART_AUTOSCALE_DEF  true
#define CHART_MAX_KW_DEF     6

// ── Pantalla (configurables desde platformio.ini con -D) ───────────────
#ifndef SCREEN_WIDTH
  #define SCREEN_WIDTH    480
#endif
#ifndef SCREEN_HEIGHT
  #define SCREEN_HEIGHT   270
#endif
#ifndef FONT_SMALL_SIZE
  #define FONT_SMALL_SIZE  12
#endif
#ifndef FONT_SMALL
  #define FONT_SMALL  lv_font_montserrat_12
#endif
#ifndef FONT_NORMAL_SIZE
  #define FONT_NORMAL_SIZE 14
#endif
#ifndef FONT_NORMAL
  #define FONT_NORMAL lv_font_montserrat_14
#endif
#ifndef FONT_LARGE_SIZE
  #define FONT_LARGE_SIZE  28
#endif
#ifndef FONT_LARGE
  #define FONT_LARGE  lv_font_montserrat_28
#endif
