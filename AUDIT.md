# Auditoría de código — 2026-08-07

Hallazgos de una revisión completa del código (~9.850 líneas: servidor web, protocolo
Solarman, Telegram, almacenamiento, caché PSRAM, pantallas). Se van marcando `[x]`
conforme se implementan, cada uno en su propio commit.

## Seguridad

- [x] **CSRF en endpoints que cambian estado** — `web_server.cpp`: `handle_admin_post`
  (~1431), `handle_restart` (~1494), `handle_update_post`/`handle_upload` (~1091/1105).
  Solo protegidos con HTTP Basic Auth, que el navegador adjunta automáticamente en
  peticiones cross-origin si hay credenciales cacheadas. Peor caso: una web maliciosa
  visitada por el admin podría forzar un flasheo de firmware vía `/update`.
  Fix: comprobar `Origin`/`Referer` en las peticiones POST antes de aceptarlas.

- [x] **`Access-Control-Allow-Origin: *` en todos los `/api/*`** — `web_server.cpp`
  líneas 1559, 1605, 1621, 1648, 1831. Permite leer datos de energía desde cualquier
  web de terceros si el navegador puede alcanzar la IP del dispositivo. El propio
  dashboard usa fetch same-origin, no lo necesita. Fix: quitar la cabecera.

- [x] **`client.setInsecure()` en la conexión a Telegram** — `telegram.cpp:411`.
  Sin verificación de certificado TLS — vulnerable a MITM si alguien controla la red.
  Fix: `client.setCACert(...)` con la CA raíz de Telegram.

- [ ] **`s_ota_authed` como variable global mutable** — `web_server.cpp:15,1092,1111,1130,1137`.
  Patrón frágil (funciona con el `WebServer` síncrono actual, se rompería con uno
  asíncrono). Prioridad baja — no implementado, dejado documentado.

- [ ] **Credenciales en NVS sin cifrar** (WiFi, token Telegram, contraseña admin) —
  `storage.cpp`. Inherente a Basic Auth + NVS estándar de ESP32. Nota de contexto,
  no fix inmediato de código (requeriría NVS cifrado + secure boot) — no implementado.

## Rendimiento

- [x] **`pushDaily()` escanea linealmente hasta 730 registros en flash** —
  `data_store.cpp:405-415`. Se llama cada cambio de hora; el registro buscado
  ("hoy") casi siempre es el último escrito. Fix: recorrer desde el final.

- [x] **Logging de depuración muy verboso en rutas de UI** — `data_store.cpp`,
  `readDay()` (334-367) y `push()` (312). Cada `DBGSERIAL.printf` toma un mutex;
  se acumula en interacciones táctiles (navegación Stats/Chart). Fix: macro
  `DBG_VERBOSE(...)` que se compile a nada salvo flag activado (no borrar los logs).

## Refactorizaciones (no complican la lectura)

- [x] **`data_store.h:149-151`** — `_raw_count`/`_hrly_count`/`_day_count` son
  referencias-alias a `_raw.count` etc. Getters podrían devolver el campo
  directamente, un nivel menos de indirección.

- [x] **`telegram.cpp:391-397`** — `TelegramBot::sendMsg()` siempre devuelve `false`,
  no lo usa nadie. Código muerto, eliminar.

## UX

- [x] **Ninguna acción destructiva pide confirmación** (verificado, cero diálogos en
  todo el proyecto). «Reiniciar sin guardar» (táctil) y «Reiniciar dispositivo»
  (`/admin`) ejecutan al instante. Fix: `lv_msgbox` en táctil + `confirm()` JS en web.

- [x] **Chart.js cargado desde CDN externo** (`cdn.jsdelivr.net`) — `web_server.cpp:565-566`.
  `/chart` deja de funcionar sin internet, y añade una dependencia de cadena de
  suministro innecesaria (~200 KB, cabe en los 8 MB de LittleFS ya reservados).
  Fix: auto-alojar chart.js + chartjs-plugin-annotation.

## Nuevas funcionalidades propuestas (pendientes de acordar alcance — no implementar sin confirmación)

- [ ] Exportar/importar configuración completa (JSON) desde `/admin`.
- [ ] Selector de rango de fechas para el CSV en el dashboard (el endpoint
  `/api/export?from=&to=` ya soporta hasta 366 días, falta la UI).
- [ ] Rate-limit / backoff tras fallos de autenticación en `/admin` y `/update`.
- [ ] Notificaciones push del navegador (Web Push) — el dashboard ya es PWA instalable.
