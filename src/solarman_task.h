#pragma once

// Tarea de red (Core 0): mantiene el WiFi, sondea el datalogger cada
// POLL_INTERVAL_MS, publica los datos para la UI/web y delega el histórico en
// Recorder y las alertas en AlertEngine. Crear con xTaskCreatePinnedToCore()
// una vez inicializados g_cfg, g_mutex, Store y Cache.
void solarmanTask(void* pv);
