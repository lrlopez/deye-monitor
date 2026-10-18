#pragma once
// Constantes de ajuste (umbrales, antirrebotes y tiempos) que antes estaban dispersas
// como números mágicos (DEYE-64). Sin dependencias de Arduino: las usan tanto el
// firmware como la lógica pura probada en `pio test -e native`.
//
// Cada una se puede sobreescribir desde platformio.ini con -DTUNE_xxx=valor.
// Las lecturas son una cada POLL_INTERVAL_MS (5 s por defecto).

// ── Alertas (alerts_logic.h) ─────────────────────────────────────────────
#ifndef TUNE_SOLAR_START_W
  #define TUNE_SOLAR_START_W        50     // W: la producción empieza por encima de esto
#endif
#ifndef TUNE_SOLAR_STOP_W
  #define TUNE_SOLAR_STOP_W         20     // W: y termina por debajo de esto
#endif
#ifndef TUNE_SOLAR_DEBOUNCE
  #define TUNE_SOLAR_DEBOUNCE        3     // lecturas seguidas para confirmar inicio/fin (15 s)
#endif
#ifndef TUNE_GRID_OUTAGE_READINGS
  #define TUNE_GRID_OUTAGE_READINGS 12     // lecturas con red = 0 W para dar un corte (60 s)
#endif
#ifndef TUNE_LOGGER_FAIL_READINGS
  #define TUNE_LOGGER_FAIL_READINGS  5     // fallos seguidos antes de avisar de que el logger no responde
#endif
#ifndef TUNE_FS_WARN_BELOW_PCT
  #define TUNE_FS_WARN_BELOW_PCT    10     // % libre de flash por debajo del cual se avisa
#endif
#ifndef TUNE_FS_REARM_ABOVE_PCT
  #define TUNE_FS_REARM_ABOVE_PCT   15     // % libre con el que se vuelve a armar el aviso
#endif

// ── Indicadores (energy_metrics.h) ───────────────────────────────────────
#ifndef TUNE_CO2_KG_PER_KWH
  #define TUNE_CO2_KG_PER_KWH     0.15f    // kg CO2 por kWh del mix eléctrico (orden de magnitud para España)
#endif

// ── Autonomía de la batería (battery_estimate.h) ─────────────────────────
#ifndef TUNE_BATT_MIN_SOC
  #define TUNE_BATT_MIN_SOC         10     // %: límite de descarga del inversor (ajústalo al de tu instalación)
#endif
#ifndef TUNE_BATT_ETA_MIN_W
  #define TUNE_BATT_ETA_MIN_W      100     // W: por debajo no se estima (batería en reposo)
#endif
#ifndef TUNE_BATT_ETA_MAX_H
  #define TUNE_BATT_ETA_MAX_H       48     // h: estimaciones mayores no se muestran
#endif

// ── Temperatura del inversor y informe diario (alerts_logic.h, alert_engine.cpp) ──
#ifndef TUNE_OVERTEMP_C
  #define TUNE_OVERTEMP_C           65     // °C del radiador: aviso por encima de esto
#endif
#ifndef TUNE_OVERTEMP_REARM_C
  #define TUNE_OVERTEMP_REARM_C     60     // °C: el aviso se rearma por debajo de esto (histéresis)
#endif
#ifndef TUNE_OVERTEMP_READINGS
  #define TUNE_OVERTEMP_READINGS     2     // lecturas seguidas (la temperatura se lee cada 60 s)
#endif
#ifndef TUNE_DAILY_REPORT_HOUR
  #define TUNE_DAILY_REPORT_HOUR    21     // hora local a la que sale el informe diario de Telegram
#endif

// ── Comunicación con el logger (solarman.cpp) ────────────────────────────
#ifndef TUNE_LOGGER_TIMEOUT_MS
  #define TUNE_LOGGER_TIMEOUT_MS  3000     // ms: conexión TCP y espera de la respuesta
#endif

// ── Alimentación ─────────────────────────────────────────────────────────
// Potencia máxima de transmisión WiFi (valor de wifi_power_t). Rebajarla reduce los picos de
// corriente que, con una fuente USB justa, disparan el «Brownout detector». Sin definir = la del
// SDK (la máxima). Para limitarla: -DTUNE_WIFI_TX_POWER=WIFI_POWER_15dBm en platformio.ini.
#ifndef TUNE_BL_SOFTSTART_MS
  #define TUNE_BL_SOFTSTART_MS     600     // ms: subida gradual del brillo al arrancar (0 = directo)
#endif

// ── Actualización de firmware desde GitHub ───────────────────────────────
// En las placas ESP32-P4 la WiFi pasa por un coprocesador (SDIO). Si la descarga llega más rápido
// de lo que se escribe en flash, los paquetes se acumulan y el driver se queda sin memoria
// («assert failed: sdio_push_data_to_queue»). Esta pausa tras cada bloque limita el ritmo.
#ifndef TUNE_FW_CHUNK_DELAY_MS
  #define TUNE_FW_CHUNK_DELAY_MS     4     // ms de pausa tras cada bloque escrito (0 = sin freno)
#endif

// ── Interfaz ─────────────────────────────────────────────────────────────
#ifndef TUNE_DOUBLE_TAP_MS
  #define TUNE_DOUBLE_TAP_MS       600     // ms entre pulsaciones para contar doble toque
#endif
#ifndef TUNE_FLOW_ACTIVE_W
  #define TUNE_FLOW_ACTIVE_W        50     // W: por debajo se considera «sin flujo» (flechas y animación)
#endif
#ifndef TUNE_STALE_WARN_MS
  #define TUNE_STALE_WARN_MS     10000     // ms sin datos: la hora de la muestra pasa a ámbar
#endif
#ifndef TUNE_STALE_LOST_MS
  #define TUNE_STALE_LOST_MS     30000     // ms sin datos: valores atenuados y aviso persistente
#endif
