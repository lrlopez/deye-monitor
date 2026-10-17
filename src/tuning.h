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

// ── Comunicación con el logger (solarman.cpp) ────────────────────────────
#ifndef TUNE_LOGGER_TIMEOUT_MS
  #define TUNE_LOGGER_TIMEOUT_MS  3000     // ms: conexión TCP y espera de la respuesta
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
