# Changelog

Todos los cambios importantes de este proyecto están documentados en este fichero.

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el proyecto usa [Versionado Semántico](https://semver.org/lang/es/).

---

## [v2.0.5] - 2026-05-23

### Modificado

- **Dashboard web en escritorio:** en pantallas ≥ 700 px las 4 tarjetas se muestran en una sola fila.
- **Pantalla de gráfica:** el SOC de la batería ya no se muestra en una gráfica separada; se superpone a la gráfica de potencias como serie secundaria con eje Y de porcentaje a la derecha. La zona aprovecha toda la altura liberada por la gráfica de SOC anterior.
- **Gráfica:** el eje horario aparece inmediatamente debajo de la gráfica y la leyenda va al final, dejando espacio para los puntos de navegación.

### Corregido

- **Pantalla de gráfica:** ya no aparece una barra de desplazamiento horizontal debajo de los puntos de navegación.

---

## [v2.0.4] - 2026-05-23

### Modificado

- **Dashboard web rediseñado:** las 4 tarjetas (Solar, Red, Batería, Carga) tienen ahora el mismo layout que la pantalla táctil: arco indicador grande centrado con el valor principal en blanco en el interior y texto secundario debajo. Solar muestra el desglose PV1/PV2 bajo el arco; Red muestra el estado Importando/Exportando/En reposo; Batería conserva el gradiente rojo/amarillo/verde con máscara gris proporcional al SOC; Carga muestra "Consumo actual".

### Corregido

- `web_server.cpp`: `/api/data` devolvía `daily.valid=true` con todos los valores a cero cuando la caché PSRAM tenía un registro recién creado para el día (sin energía acumulada todavía). El endpoint usa ahora `g_daily` (datos vivos de `fetchDailyStats()`) como fuente primaria, con la caché como fallback; el mismo comportamiento que la pantalla táctil.

---

## [v2.0.3] - 2026-05-23

### Añadido

- **Eliminación de alertas duplicadas de producción solar:** Ya no se repiten alarmas tras un reinicio cuando no corresponde.

### Corregido

- Solucionado un problema al registrar las medidas de 5 minutos.
- Aumentado framebuffer en Sunton para mejorar la suavidad en las animaciones.

---

## [v2.0.2] - 2026-05-21

### Añadido
- **Arcos indicadores en el dashboard:** cada tarjeta muestra ahora un arco estilo gauge que representa visualmente el valor en tiempo real. Solar: arco amarillo de 0 a la potencia máxima del inversor. Red: arco bipolar verde (exportando) / rojo (importando) centrado en cero. Batería: arco verde (cargando) / rojo (descargando) / gris (reposo) con SOC% y potencia W en el interior. Carga: arco azul de 0 al máximo entre red e inversor. Los valores numéricos se muestran centrados dentro del arco.
- **Capacidades de la instalación configurables:** tres nuevos parámetros en la sección Inversor de la pantalla táctil y del panel web `/admin`: *Inv. máx. W* (potencia pico del inversor, por defecto 6000 W), *Red máx. W* (potencia máxima de red, por defecto 6000 W) y *Cap. bat. Wh* (capacidad de la batería en Wh, por defecto 16000 Wh). Rango válido 1-65535 para los tres. El comando `/bateria` de Telegram usa ahora el valor real configurado para calcular la estimación de tiempo de carga/descarga en lugar del valor fijo anterior.

### Corregido
- `web_server.cpp`: la página `/admin` aparecía cortada cuando el token de Telegram no estaba configurado. 

---

## [v2.0.1] — 2026-05-21

### Corregido
- **Parpadeo en arranque ESP32-P4 (causa real: tearing, no caché de flash):** `draw16bitRGBBitmap` copiaba el buffer de LVGL directamente sobre el framebuffer activo mientras el controlador MIPI DSI lo leía en paralelo; en el primer arranque con datos reales la diferencia de contenido era máxima y el fotograma corrupto resultaba visible. Reemplazado por `esp_lcd_panel_draw_bitmap` que usa DMA2D para copiar al back-buffer (front-buffer sigue mostrándose intacto) y programa el swap atómico en el siguiente vsync. El mutex `s_flash_display_mutex` se mantiene para serializar el DMA2D con las escrituras flash de Core 0

### Añadido
- **Mecanismo de parches para la GFX Library en CI:** `patches/Arduino_ESP32DSIPanel.h/.cpp` contiene las modificaciones necesarias para ESP32-P4 (`num_fbs=2`, `use_dma2d=true`, método `getPanelHandle()`). El script `scripts/patch_gfx_p4.py` (referenciado como `extra_scripts = pre:scripts/patch_gfx_p4.py` en el entorno `guition_jc1060p470`) los aplica automáticamente sobre la versión del registro antes de la compilación, tanto en local como en CI

---

## [v2.0.0] — 2026-05-21

### Añadido
- **Soporte para Guition JC1060P470 (ESP32-P4, 1024×600 px MIPI DSI):** nuevo entorno `guition_jc1060p470` en `platformio.ini` con ESP32-P4 RISC-V @ 360 MHz, 8 MB PSRAM y pantalla MIPI DSI de 1024×600 px con touch GT911. Fuentes Montserrat adaptadas a la mayor resolución (14/28/32/48 px). El dispositivo se comporta funcionalmente igual que el ESP32-S3
- **Renderizado sin tearing en ESP32-P4:** doble framebuffer DSI (`num_fbs=2`) con vsync swap atómico en `esp_lcd_panel_draw_bitmap` y modo `LV_DISPLAY_RENDER_MODE_FULL` con buffer completo en PSRAM. Las actualizaciones de UI y los scrolls son tear-free
- **Mensaje de progreso durante el formateo de LittleFS:** la splash screen muestra "Formateando flash..." durante el primer arranque sin bloquear el renderizado LVGL (el formateo se ejecuta en una tarea separada en Core 0 mientras Core 1 sigue procesando eventos)
- **Macro `DBGSERIAL`** en `config.h` que se resuelve a `Serial` en ESP32-P4 y a `Serial0` en ESP32-S3, unificando todos los mensajes de diagnóstico sin condicionales dispersos

### Corregido
- **Parpadeo de pantalla en ESP32-P4 durante accesos a flash:** en ESP32-P4 las escrituras a LittleFS/NVS en Core 0 deshabilitan interrupciones, impidiendo que `esp_cache_msync` entregue el IPI necesario a Core 1 durante el flush del display. Añadido mutex `s_flash_display_mutex` que serializa todas las escrituras a flash (`Store.push`, `Cache.pushHourly`, `Cache.pushDaily`, `Storage.saveSessionState`, `Store.getLastHourly`) con el `esp_lcd_panel_draw_bitmap` del flush callback
- **WiFi crash en ESP32-P4:** llamar a `WiFi.disconnect()` antes de la primera conexión provoca un crash en ESP32-P4. Añadida bandera `s_wifi_ever_connected` para omitir el disconnect en el primer intento de conexión
- **LittleFS "No more free space" en el primer write real:** la pre-alocación de ficheros con `seek + write(0)` rellenaba de ceros hasta 3 ficheros (raw 6.7 MB + hrly 1.1 MB + day 46 KB = 94 % de los 2046 bloques disponibles), dejando sin margen para los COW de metadatos que necesita `flush()`. Eliminada la pre-alocación; los ficheros crecen on-demand. Bumpeada la versión de meta a 5 para forzar un reset limpio del almacenamiento en dispositivos que tenían los ficheros pre-alocados
- `web_server.cpp`: el campo `logger_ip` del panel `/admin` aceptaba cualquier cadena sin validar el formato; añadida función `is_valid_ipv4()` que comprueba que el valor sea exactamente 4 octetos decimales en rango 0–255 antes de guardarlo en NVS

### Modificado
- **Historial reducido de 1.461 a 730 días** (2 años): el tamaño máximo de los ficheros LittleFS pasa de 7,9 MB a 3,8 MB, dejando siempre más del 50 % del espacio libre incluso con el histórico completo. La caché PSRAM horaria pasa de 1.121 KB a 548 KB

---

## [v1.0.10] — 2026-05-20

### Corregido
- `storage.cpp`: `toCharArray` en `loadTelegramConfig()` usaba `sizeof(field)` sin restar 1 — si el token o el chat_id tenían exactamente la longitud máxima (64/32 bytes), el null-terminator no se escribía; corregido a `sizeof - 1`
- `web_server.cpp`: `/api/history?granularity=daily` aceptaba rangos arbitrariamente amplios; añadido rechazo 400 si `to − from > 366 días`
- `solarman.cpp`: la respuesta del datalogger se aceptaba sin validar el CRC Modbus RTU; añadida verificación con `modbusCRC()` antes de extraer los valores de los registros

---

## [v1.0.9] — 2026-05-19

### Añadido
- Dashboard web: versión de la aplicación mostrada en el encabezado y el pie de página (obtenida en tiempo de carga desde `/api/status`)
- Dashboard web: porcentajes de **autosuficiencia** (consumo cubierto sin importar red) y **autoconsumo** (solar aprovechado localmente) visibles en la sección "Hoy", actualizados junto al resto de datos cada 5 segundos
- `/api/status`: nuevo campo `version` en la respuesta JSON con la versión del firmware compilado

---

## [v1.0.8] — 2026-05-19

### Añadido
- PWA (Progressive Web App): el dashboard (`/`) y la gráfica (`/chart`) incluyen ahora `<link rel="manifest">` y las etiquetas `apple-mobile-web-app-*`; el dispositivo sirve `/manifest.json` (nombre, colores, icono) e `/icon.svg` (rayo amarillo sobre fondo oscuro). En Android/Chrome e iOS/Safari aparece la opción "Añadir a inicio" que instala la web como aplicación sin navegador visible
- Dos niveles de alerta de batería: `batt_warn` (aviso, por defecto 25 %) y `batt_threshold` (crítico, ya existente, por defecto 20 %). La máquina de estados de tres niveles (NORMAL → WARN → CRIT) envía ⚠️ *Aviso batería* al cruzar el umbral de aviso y 🔴 *Batería crítica* al cruzar el crítico, con recuperación única al superar warn+5 %. El umbral de aviso es configurable desde el panel web `/admin` y desde la pantalla táctil (sección TELEGRAM, slider amarillo *Aviso bat.*)

---

## [v1.0.7] — 2026-05-19

### Añadido
- Soporte mDNS: el dispositivo anuncia su presencia en la red local bajo el nombre `<hostname>.local` (por defecto `inversor.local`). El hostname es configurable desde el panel web `/admin` y desde la pantalla táctil (sección ESTADO RED); requiere reinicio para aplicar. La biblioteca `ESPmDNS` es parte del core de Arduino para ESP32 (sin dependencias adicionales). Se anuncia también el servicio HTTP (`_http._tcp`) para que los descubridores de servicios de red lo detecten automáticamente. El valor se sanitiza automáticamente a `[a-z0-9-]` sin guiones al inicio/fin, con fallback a `inversor` si el campo queda vacío

### Corregido
- `main.cpp`: las alertas proactivas de Telegram (`SOLAR_START`, `SOLAR_STOP`, `BATT_LOW`, `BATT_RECOVERED`, `GRID_OUTAGE`, `GRID_RESTORED`, `LOGGER_FAIL`) nunca se disparaban porque la lógica de detección no estaba implementada en `solarmanTask`. Añadida máquina de estados en el bucle de polling con debounce de 3 lecturas (15 s) para solar y red, e histéresis de 5 puntos para la batería. La configuración Telegram (`notify_solar`, `notify_grid`, `notify_logger`, `batt_threshold`) se cachea en RAM y se refresca desde NVS cada 60 s para no bloquear la tarea con lecturas de flash frecuentes

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
