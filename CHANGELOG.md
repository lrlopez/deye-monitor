# Changelog

Todos los cambios importantes de este proyecto están documentados en este fichero.

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el proyecto usa [Versionado Semántico](https://semver.org/lang/es/).

---

## [Unreleased]

### Corregido
- `main.cpp`: las alertas proactivas de Telegram (`SOLAR_START`, `SOLAR_STOP`, `BATT_LOW`, `BATT_RECOVERED`, `GRID_OUTAGE`, `GRID_RESTORED`, `LOGGER_FAIL`) nunca se disparaban porque la lógica de detección no estaba implementada en `solarmanTask`. Añadida máquina de estados en el bucle de polling con debounce de 3 lecturas (15 s) para solar y red, e histéresis de 5 puntos para la batería. La configuración Telegram (`notify_solar`, `notify_grid`, `notify_logger`, `batt_threshold`) se cachea en RAM y se refresca desde NVS cada 60 s para no bloquear la tarea con lecturas de flash frecuentes

### Añadido
- Soporte mDNS: el dispositivo anuncia su presencia en la red local bajo el nombre `<hostname>.local` (por defecto `inversor.local`). El hostname es configurable desde el panel web `/admin` y desde la pantalla táctil (sección ESTADO RED); requiere reinicio para aplicar. La biblioteca `ESPmDNS` es parte del core de Arduino para ESP32 (sin dependencias adicionales). Se anuncia también el servicio HTTP (`_http._tcp`) para que los descubridores de servicios de red lo detecten automáticamente. El valor se sanitiza automáticamente a `[a-z0-9-]` sin guiones al inicio/fin, con fallback a `inversor` si el campo queda vacío

---

## [v1.0.6] — 2026-05-19

### Corregido
- `data_store.cpp`: doble llamada a `LittleFS.begin()` — `Store.begin()` volvía a montar el sistema de ficheros aunque `main.cpp` ya lo había montado; eliminada la llamada duplicada y añadido comentario indicando que el llamador es el responsable de montar LittleFS
- `data_store.cpp` / `web_server.cpp`: `/api/status` usaba capacidades hardcoded (`201600`, `17520`, `730`) en lugar de las constantes reales del buffer; sustituido por llamadas a `Store.getRawCapacity()`, `Store.getHourlyCapacity()` y `Store.getDailyCapacity()`
- `data_store.cpp`: `getLastRecord()` usaba `readAt()` (abría y cerraba el fichero cada vez) en lugar del handle permanente `_f_raw`; corregido usando `readRaw()` con el índice físico correcto y validación de `timestamp > 0`
- `psram_cache.cpp`: `begin()` llamaba a `_bitmap_build()` explícitamente después de `_day_load_all()`, que ya la invoca internamente; eliminada la llamada redundante
- `psram_cache.cpp`: `pushHourly()` incrementaba `hours_valid` sin cota superior, pudiendo superar 24; añadida comprobación `if (hours_valid < 24)` antes de incrementar
- `telegram.cpp`: `fmtAlert(BATT_LOW)` llamaba a `cmdBateria()` dos veces (doble consulta al inversor en el mismo mensaje); el resultado ahora se almacena en una variable local
- `main.cpp`: función `delta_wh()` declarada pero nunca llamada eliminada del código

---

## [v1.0.5] — 2026-05-19

### Corregido
- `data_store.cpp` / `data_store.h`: el índice de días en PSRAM (`_day_idx`) almacenaba índices **lógicos** (relativos al `head` del anillo). Cada push cuando el buffer está lleno desplaza todos los índices en −1; tras ≈4 años de funcionamiento continuo sin reinicio, `readDay()` devolvía solo 1 registro por día en lugar de hasta 288. Corregido guardando la posición **física** (inmutable) y convirtiéndola a lógica en `readDay()` con `(phys − head + capacity) % capacity`. Eliminados métodos privados muertos (`writeHrly`, `readHrly`, `writeDay_`, `readDay_`, `lowerBoundRaw`) y los file handles `_f_hrly` / `_f_day` que se abrían en `begin()` pero nunca se usaban

---

## [v1.0.4] — 2026-05-19

### Corregido
- `main.cpp`: acceso fuera de bounds en el array de tiles de navegación — el bucle iteraba hasta `i < 5` sobre un array de 4 elementos, accediendo a memoria adyacente en el stack (UB); corregido a `i < 4` y el array sacado fuera del bucle
- `solarman.cpp`: posible overflow del buffer de recepción TCP de 256 bytes si el datalogger enviaba más datos de los esperados; añadida comprobación `received < sizeof(resp)`. El bucle de recepción hacía busy-wait ocupando Core 0 durante hasta 3 segundos bloqueando el servidor web; añadido `vTaskDelay(1 ms)` cuando no hay bytes disponibles
- `config_screen.cpp`: la función `save_btn_cb()` cargaba la configuración antigua **después** de guardar la nueva, por lo que `needs_restart` era siempre `false` y el dispositivo nunca reiniciaba al cambiar WiFi o IP del logger. Corregido cargando `old_cfg` al inicio antes de cualquier escritura. Eliminada la doble escritura NVS (todas las secciones se guardaban dos veces por llamada al botón)
- `main.cpp`: el mutex principal de FreeRTOS no verificaba el retorno de `xSemaphoreCreateMutex()`; si la creación fallase (heap agotado), el sistema continuaría sin sincronización entre tareas. Añadida verificación con reinicio controlado
- `solarman.cpp`: `client.connect()` no tenía timeout de conexión TCP — podía bloquear hasta 75 s si el host existía pero el puerto 8899 no respondía. Añadido timeout explícito de 3 s mediante `connect(_ip, _port, 3000)`
- `web_server.cpp`: la flag `s_ota_authed` no se reiniciaba entre sesiones de subida OTA; si una sesión anterior autenticada quedaba interrumpida, un POST posterior sin credenciales podía reutilizar la flag. Corregido reiniciando `s_ota_authed = false` al cargar la página `/update`
- `psram_cache.cpp` / `psram_cache.h`: race condition crítica entre `solarmanTask` (escrituras vía `pushRaw`/`pushHourly`/`pushDaily`) y `webserver_task` (lecturas de datos en caché) sin ningún mutex. Implementado mutex recursivo interno en todos los métodos públicos de `PsramCache`; la tarea de carga en background (`_bg_task`) también adquiere el mutex antes de cargar datos
- `config.h`: credenciales WiFi y datos del datalogger hardcoded en el repositorio (`WIFI_SSID`, `WIFI_PASS`, `LOGGER_IP`, `LOGGER_SERIAL`) sustituidos por valores vacíos/placeholder; los valores reales se configuran desde la pantalla táctil y se persisten en NVS
- `data_store.cpp`: el límite del índice de días usaba el literal `730` en lugar de la constante `DAY_IDX_MAX`, lo que desincronizaba el tamaño real del buffer con el valor declarado en la cabecera
- `web_server.cpp`: `String::toInt()` truncaba números de serie del datalogger con 10 dígitos que superan `INT_MAX`; sustituido por `strtoul()` para parsear correctamente todo el rango `uint32_t`
- `web_server.cpp` / `telegram.cpp`: `parse_date()` aceptaba strings de 8 caracteres sin verificar que fueran dígitos ni que los valores estuvieran en rango, permitiendo que `mktime()` normalizara fechas absurdas; añadidas validación de dígitos y comprobación de rangos (año 2020–2100, mes 1–12, día 1–31)
- `web_server.cpp`: los campos `wifi_ssid`, `logger_ip`, `tg_token` y `tg_chat_id` se inyectaban sin escapar en el HTML del panel `/admin`, permitiendo XSS almacenado si el NVS contenía caracteres especiales; añadida función `html_escape()` aplicada a todos los puntos de salida

---

## [v1.0.3] — 2026-05-19

### Added
- Panel de administración web en `/admin`: permite configurar el inversor, la gráfica, Telegram y el brillo de la pantalla desde el navegador sin acceder a la pantalla táctil
- Protección por contraseña (HTTP Basic Auth) del panel `/admin` y de la página de actualización OTA `/update`, configurable exclusivamente desde la pantalla táctil (sección ACCESO WEB)
- Mensaje informativo en la gráfica web cuando no hay datos disponibles para el día seleccionado

### Fixed
- El cursor no se mostraba correctamente al escribir en los cuadros de texto de la pantalla de configuración

---

## [v1.0.2] — 2026-05-19

### Fixed
- Fallos en el guardado y la recuperación de medidas de 5 minutos que podían provocar pérdida o corrupción de registros

---

## [v1.0.1] — 2026-05-18

### Fixed
- Problema de transparencia en los puntos de navegación entre pantallas
- Errores en el flujo de compilación y publicación de releases en `build_release.yml`

---

## [v1.0.0] — 2026-05-18

Lanzamiento inicial del monitor solar Deye. Incluye el sistema completo de monitorización, almacenamiento, interfaz táctil, servidor web y notificaciones.

### Added

#### Monitor en tiempo real
- Comunicación con el datalogger mediante protocolo SolarmanV5 sobre TCP (puerto 8899), con polling cada 5 segundos
- Lectura de potencia solar (PV1 + PV2), intercambio con la red, batería (potencia + SOC) y carga del hogar
- Sincronización horaria por NTP

#### Interfaz táctil (LVGL 9)
- Dashboard de tiempo real con 4 tarjetas (solar, red, batería, carga) e indicador de autoconsumo con código de colores
- Pantalla de estadísticas diarias con donuts de consumo y producción, navegable día a día
- Pantalla de resumen semanal con totales de los últimos 7 días
- Gráfica diaria de líneas con eje Y configurable para 5 series temporales (PV, red, batería, carga, SOC)
- Pantalla de configuración con scroll y teclado virtual; detección y selección de redes WiFi disponibles
- Selector de fecha mediante calendario mensual emergente; tap en la fecha para volver al día actual
- Pantalla de inicio (splash screen) con progreso de inicialización
- Puntos de navegación para indicar la pantalla activa
- Soporte de resolución arbitraria mediante macros `SX()`, `SY()`, `SS()` en `ui_constants.h`
- Control del brillo de la pantalla por PWM, con modo de inactividad y horario nocturno configurables

#### Almacenamiento
- Registro de medidas cada 5 minutos alineado a intervalos exactos (XX:00, XX:05…) en LittleFS
- Agregaciones horarias pre-calculadas para generación instantánea de gráficas
- Totales diarios con porcentaje de autoconsumo y autosuficiencia
- Capacidad de almacenamiento para hasta 4 años de historial (1.461 días)
- Caché en PSRAM de toda la historia horaria y diaria para acceso sin latencia y sin acceder a flash
- Recuperación automática de datos en gaps provocados por cortes de alimentación

#### Servidor web
- Dashboard HTML con valores en tiempo real actualizados por AJAX cada 5 segundos sin recargar la página
- Donuts SVG animados de autoconsumo y producción
- Gráfica diaria interactiva con Chart.js, navegable día a día con actualización incremental
- API REST: `/api/data`, `/api/history` (5min / horario / diario), `/api/latest_date`, `/api/status`
- Actualización de firmware OTA desde el navegador en `/update`

#### Notificaciones Telegram
- Bot con comandos: `/estado`, `/bateria`, `/hoy`, `/dia`, `/semana`, `/sistema`, `/umbral`, `/silenciar`, `/activar`, `/ayuda`
- Alertas proactivas: batería baja/recuperada, solar arranca/para, corte y restauración de red, fallo de comunicación con el logger

#### Infraestructura
- CI/CD con GitHub Actions: compila un `.bin` por entorno y crea un release automático con cada tag `v*`
- Configuración persistida en NVS (WiFi, inversor, gráfica, brillo, Telegram)
- Particionado personalizado: NVS + OTA dual + LittleFS 8 MB

### Fixed
- Error al mostrar el teclado virtual sobre pantallas ya eliminadas
- Problema con el tamaño del buffer I2C del controlador táctil
- Pantalla de estadísticas con valores al 100% mostraba el arco cortado
- Problema con la generación de la gráfica del histórico diario
- Color incorrecto del indicador de carga de batería en el servidor web
- Fallos en la obtención de datos del inversor bajo ciertas condiciones de red
