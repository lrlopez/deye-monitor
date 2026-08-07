#pragma once
#include <Arduino.h>
#include "telegram.h"   // AlertType

// Registro en RAM de las últimas alertas del sistema, consultable desde la
// web vía GET /api/alerts?since_id=N — lo usa el dashboard para mostrar
// notificaciones del navegador (Notification API) mientras la pestaña está
// abierta, sin necesidad de Web Push (sin claves VAPID ni service worker).
//
// Se alimenta desde screen_banner_enqueue(), que ya se llama en cada punto
// del código donde se dispara una alerta — no hace falta tocar más sitios.

struct WebAlertEntry {
    uint32_t  id;          // monótono creciente, empieza en 1
    uint32_t  timestamp;   // epoch UTC
    AlertType type;
    int32_t   value;
};

void WebAlerts_init();
void WebAlerts_push(AlertType type, int32_t value);

// Copia en out (capacidad max) las alertas con id > since_id, de más
// antigua a más reciente. Devuelve cuántas ha copiado.
uint32_t WebAlerts_getSince(uint32_t since_id, WebAlertEntry* out, uint32_t max);

// Último id asignado (0 si aún no hay ninguna alerta).
uint32_t WebAlerts_lastId();

// Texto y nivel ("info"/"warn"/"critical") en español plano (sin símbolos
// LVGL) para mostrar en la web.
const char* WebAlerts_text(const WebAlertEntry& e, char* buf, size_t buf_size);
const char* WebAlerts_level(AlertType type);
