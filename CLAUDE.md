# Deye Monitor — Contexto completo del proyecto

Este documento resume todas las decisiones de diseño, arquitectura, bugs resueltos y estado actual del proyecto para continuar el desarrollo en otra conversación.

---

## Hardware

- **MCU:** ESP32-S3 con 8 MB PSRAM (OPI) y 16 MB Flash
- **Pantalla:** 480×272 px táctil capacitiva (resolución base; soporta otras ≥480×270)
- **Pin backlight:** `TFT2_BL` (PWM con `ledcAttach`)
- **Inversor:** Deye SUN-6K-SG05
- **Datalogger:** Stick WiFi LSW3 → protocolo SolarmanV5 sobre TCP puerto 8899
- **Framework:** Arduino (arduino-esp32 3.x) con PlatformIO
- **UI:** LVGL 9 (no LVGL 8 — APIs distintas)

---

## Soporte multi-placa

Tres entornos PlatformIO (`platformio.ini`), autodetectados por CI vía `get_envs.py`:

| Entorno | Board | MCU | Panel | Interfaz |
|---|---|---|---|---|
| `sunton_4827s043` | esp32s3box | ESP32-S3 | 480×270/272, RGB paralelo | `Arduino_RGB_Display` |
| `guition_jc1060p470` | esp32-p4-evboard | ESP32-P4 | 1024×600 nativo horizontal, ST7701/JD9165 | `Arduino_DSI_Display`, rotation=0 |
| `guition_jc4880p443c` | esp32-p4-evboard | ESP32-P4 | 480×800 nativo **vertical**, ST7701 | `Arduino_DSI_Display`, rotation=1 (girado a 800×480 lógico por software) |

Selección de placa vía `-DBOARD_GUITION_JC1060P470` / `-DBOARD_GUITION_JC4880P443C` (build_flags). Sin ninguna de las dos definida, `main.cpp`/`touch.h` caen al bloque `#else` (Sunton ESP32-S3).

Las tres placas usan la misma plataforma, fijada en `[common]` de `platformio.ini`: pioarduino 55.03.38 (Arduino 3.3.8, IDF 5.5) y `GFX Library for Arduino@1.6.9`. Los paneles ESP32-P4 aplican dos sustituciones de texto sobre la GFX (`scripts/patch_gfx_p4.py`, pre-script; falla si la librería cambia): `getPanelHandle()` y `num_fbs=2` (doble framebuffer DMA2D sin tearing). Al subir la versión de la plataforma o de la GFX, compilar las tres placas.

### JC4880P443C — parámetros verificados en hardware real

El datasheet oficial ("device-specs.md" de terceros) traía timings DSI **incorrectos** que colgaban el panel a mitad de la secuencia de inicialización. Los valores correctos (verificados por la comunidad en `esphome/esphome/esphome/components/mipi_dsi/models/guition.py`, modelo `"JC4880P443"`, y confirmados por bring-up real) son:

```cpp
// main.cpp — Arduino_ESP32DSIPanel(...)
hsync_pulse_width=12, hsync_back_porch=42, hsync_front_porch=42
vsync_pulse_width=2,  vsync_back_porch=8,  vsync_front_porch=166   // ojo: NO 16, es 166
prefer_speed=34000000 /* 34MHz, no 40MHz */, lane_bit_rate=500 /* Mbps, no 750 default */
rst=5  // GPIO5 — pin de reset físico del panel, imprescindible (con GFX_NOT_DEFINED el panel
       // arranca en estado inconsistente y se cuelga durante la secuencia de comandos de init)
```

La secuencia de comandos de init (`st7701_dsi_init_operations`, ya incluida en la librería GFX vendorizada) sí es la correcta para este panel — coincide byte a byte con la de ESPHome.

**Flush con rotación de software:** `esp_lcd_panel_draw_bitmap()` (usado directamente en `my_disp_flush()` para JC1060P470, con DMA2D) solo es válido cuando `rotation=0` — el framebuffer nativo del panel tiene el mismo stride que el buffer de LVGL. Con `rotation=1` (JC4880P443C, panel nativo vertical girado a horizontal) el stride nativo del panel (480px) no coincide con el del buffer de LVGL (800px), y copiar en bruto produce una imagen entrelazada/rota. Para esta placa el flush pasa por `gfx->draw16bitRGBBitmap()`, que sí aplica la rotación correctamente (más lento que el DMA2D directo, pero correcto).

**Táctil GT911:** mismo cableado I2C que JC1060P470 (SDA=7, SCL=8, INT=21, RST=22). El chip GT911 se configura con las dimensiones **físicas** del panel (480×800, constantes `TOUCH_GT911_CFG_W/H` en `touch.h`) — **no** con las dimensiones lógicas rotadas. Confundir ambas (usar 800×480 en el constructor de `TAMC_GT911`) hace que la librería aplique `width - x_raw` con el `width` equivocado, produciendo valores que a veces desbordan como `uint16_t` (wraparound tipo 65224). El mapeo final a pantalla usa `TOUCH_SWAP_XY` con `TOUCH_MAP_X1=800,X2=0,Y1=0,Y2=480` — calibrado empíricamente tocando las esquinas físicas reales y verificado correcto.

---

## Particionado

Ver `partitions.csv` (NVS + OTA doble + LittleFS 8MB + coredump) y los `board_build.*` de `platformio.ini`.

---

## Tiempo y cambios de hora (DST)

Un día local dura 86.400 s **salvo** el domingo de cambio de hora (23 h en primavera, 25 h en otoño).
**Nunca** sumar/restar `86400` ni dividir por `86400` sobre epochs de medianoche local: usar los helpers
de `src/time_utils.h` (`next_local_midnight`, `local_midnight_plus_days`, `local_days_between`,
`local_day_offset`), que normalizan con `mktime(tm_isdst=-1)`. Bugs que esto corrigió: la hora 0 del día
siguiente se colaba en la gráfica del día de cambio de primavera, los slots de caché quedaban 1 h
desfasados, y el calendario mostraba 30 días en marzo.

---

## Seguridad web

- Sin contraseña de administración (NVS `web_pass`, solo editable desde la pantalla táctil) las rutas
  sensibles (`/admin`, `/update`, import/export/wipe de datos, config import/export, `/api/restart`)
  responden **403**; ya no quedan abiertas. Con contraseña: Basic Auth `admin` + lockout con backoff + same-origin.
- Las rutas de solo lectura (`/api/data`, `/api/history`, `/api/export`, `/chart`) siguen sin auth.

## Actualización de firmware

Dos vías en `/update`, ambas tras contraseña de administración:
- **Manual:** subida multipart del `.bin` (`handle_upload`).
- **Desde GitHub** (`fw_update.cpp`, lógica pura en `fw_update_logic.h`, probada en nativo): `POST /api/fw/check` consulta
  `releases/latest` de `FW_UPDATE_REPO` (config.h); `POST /api/fw/install` descarga el asset `deye_monitor_<FW_ENV_NAME>_<tag>.bin`
  (nombre del CI en `build_release.yml`; `FW_ENV_NAME` lo fija cada entorno en `platformio.ini`), comprueba TLS (raíces en
  `fw_update_ca.h`), tamaño, cabecera 0xE9 y SHA-256 (`digest` de la API) y solo entonces llama a `Update.end()`.
  Corre en tareas de 16 KB (TLS); el estado se consulta con `GET /api/fw/status`.
- Protocolo con el hilo LVGL: `g_ota_request` → overlay y render congelado (`g_ota_active`); `g_ota_cancel` lo retira si falla.
- Si GitHub cambia de CA, la descarga falla "cerrada" (la subida manual sigue funcionando): añadir la raíz nueva a `fw_update_ca.h`.
- Si se renombran los `.bin` del CI hay que actualizar `fw_asset_matches()` (y sus tests).

---

## Documentación (manual de usuario)

- Fuente en `docs/manual/` (Markdown, MkDocs Material para la web y pandoc + Paged.js para el PDF); `make docs` genera ambos. `docs/manual/README.md` lo explica; `.github/workflows/docs.yml` publica web (GitHub Pages, solo la última versión estable), PDF y ZIP adjuntos a la release.
- Orden: lo que ve la persona usuaria primero; la instalación del firmware (`13-…`) es el último capítulo técnico. **Al cambiar una pantalla, un ajuste o un comando de Telegram/web, actualizar el capítulo correspondiente en el mismo cambio.**
- Capturas en `docs/manual/img/`, regeneradas sin hardware con `tools/screenshots/capture-device.sh` (simulador LVGL del firmware, escenas en `sim/scenes.inc`) y `capture-web.sh` (páginas reales de `web_server.cpp` extraídas con `web/gen.py`). Un módulo de UI nuevo puede necesitar stubs en `sim/stubs.cpp`. `/admin` no tiene captura (HTML dinámico).

---

## CI, versión y constantes de ajuste

- `.github/workflows/ci.yml` (push a main y PR): `pio test -e native`, `pio check -e sunton_4827s043` (cppcheck sobre `src/`, config en `platformio.ini`; falla con defectos altos/medios) y build de las 3 placas con informe de tamaño. `build_release.yml` sigue siendo solo para tags.
- `APP_VERSION` lo fija `scripts/git_version.py` desde `git describe` (`3.7.0` sobre el tag; `3.6.0+24.gSHA[.dirty]` en desarrollo; el `+…` lo ignora `fw_parse_version`). El valor de `config.h` es el respaldo.
- Umbrales/antirrebotes/tiempos con nombre en `src/tuning.h` (`TUNE_*`, sobreescribibles con `-D`); no volver a escribir números mágicos en alertas, doble toque o timeouts del logger.

---

## Tests

`pio test -e native` ejecuta tests Unity en el PC (sin hardware) sobre la lógica pura en headers sin
dependencias de Arduino: `solarman_proto.h` (CRC, frame, parseo de respuesta), `slot_avg.h`, `time_utils.h`,
`ring_index.h` (anillo circular + índice de días con búsqueda binaria + búsqueda por epochs, usados por `DataStore`),
`alerts_logic.h` (máquinas de estado de alertas solar/batería/red/logger + backoff, usadas por `solarmanTask`) y
`daily_tracker.h` (última lectura diaria buena por día, para cerrar el día). Ojo con los nombres: las cabeceras
Xtensa definen macros como `EPS` (rompe un `constexpr EPS`) y Arduino define `LOW`/`HIGH`.
No cubren la E/S real de LittleFS ni Telegram. Los headers deben compilar con C++11 (core Arduino del Sunton).
El entorno `native` lleva `ci_skip = true`. Toda lógica nueva que pueda ser pura debería ir en un header así.

---

## Protocolo SolarmanV5

- Frame de 36 bytes: header A5 + payload (inner header 15B + Modbus RTU 8B) + checksum + 0x15
- **Respuesta:** inner header es 14 bytes (no 15) → `DATA_OFFSET = 28`
- CRC Modbus FC=03 sobre registros Holding
- Conexión TCP, timeout 3s, sin keep-alive

### Registros verificados (SUN-6K-SG05)

**Instantáneos (base 169, count 23):**
```
Reg 169: grid_power   (signed, W)
Reg 178: load_power   (unsigned, W)
Reg 184: batt_soc     (%)
Reg 186: pv1_power    (W)
Reg 187: pv2_power    (W)   ← era 188, corregido
Reg 190: batt_power   (signed: positivo=descargando, negativo=cargando)
```

**⚠️ Signo de batería:** positivo = descargando, negativo = cargando. El código original lo tenía al revés. Corregido en dashboard, web server y stats.

**Diarios (base 70, count 39, unidad 0.1 kWh):**
```
Reg 70:  batt_charge_kwh      (era 78, corregido)
Reg 71:  batt_discharge_kwh   (era 79, corregido)
Reg 76:  import_kwh           (estaban intercambiados con 77)
Reg 77:  export_kwh           (estaban intercambiados con 76)
Reg 84:  load_kwh
Reg 108: pv_kwh               (encontrado mediante dump, era calculado)
```

---

## Estructuras de datos

- `EnergyData` (solarman.h) — instantáneos live
- `DailyStats` (solarman.h) — totales diarios live
- `Record5Min` (data_store.h, 16 bytes, `#pragma pack(1)`) — registro cada 5 min en flash
- `HourlyRecord` (data_store.h, 32 bytes) — agregación horaria
- `DailyRecord` (data_store.h, 32 bytes) — totales del día

El signo de `batt_power`/`batt_w` y `grid_power`/`grid_w` sigue la convención de la tabla de bugs resueltos (positivo=descargando/importando).

**Conversión:** `daily_record_to_stats(dr)` → `DailyStats`

---

## Sistema de almacenamiento

### Capacidad (`STORE_DAYS` = 730 días = 2 años, en `data_store.h`)

| Fichero | Registros | Tamaño |
|---|---|---|
| `/raw.bin` | 730×288 = 210.240 | 3.285 KB |
| `/hrly.bin` | 730×24 = 17.520 | 547 KB |
| `/day.bin` | 730 | 23 KB |
| Total | | ~3.855 KB de 8.192 KB |

Cambiar `STORE_DAYS` cambia la capacidad de los tres ficheros, del índice y de la caché
(y hace incompatibles las copias de seguridad de otra capacidad).

### `DataStore` — optimizaciones implementadas

- **File handles abiertos permanentemente** (no open/close en cada lectura)
- **Sin pre-alocación**: los ficheros crecen bajo demanda (pre-alocar agotaba los bloques LittleFS necesarios para los COW de metadatos)
- **Índice de días en PSRAM** (`DayIdx[]`, ordenado y estrictamente ascendente): `readDay()` busca por búsqueda binaria (`dayidx_find`)
- **Meta guardada cada 12 pushes** en raw; en hourly solo al añadir registro nuevo (las actualizaciones in situ del `DailyRecord` no reescriben la meta)
- **Tres buffers circulares independientes** con mismo número de días

### `PsramCache` — caché en PSRAM

| Caché | Días | Tamaño | Carga |
|---|---|---|---|
| Raw | 90 | 405 KB | Eager últimos 7 + background |
| Hourly | 730 | 547 KB | Eager últimos 7 + background |
| Daily | 730 | 23 KB | Eager completo en `begin()` |
| DayIdx | 730 | 6 KB | En `DataStore` |
| Bitmap has_data | 730 bits | <1 KB | Desde daily cache |

**Total PSRAM usado:** ~1.000 KB aprox. (sin contar buffers de LVGL)

La historia horaria completa está en PSRAM → **la gráfica diaria nunca toca flash**.

### Grabación y alertas (`solarman_task.cpp`, `recorder.cpp`, `alert_engine.cpp`)

Reparto (antes todo era una sola función en `main.cpp`):
- `solarman_task.cpp` — bucle de la tarea de red: WiFi/NTP/mDNS, sondeo del logger, publicación en `g_energy`/`g_daily`, backoff y redescubrimiento. Orquesta a los otros dos.
- `recorder.cpp` (`Recorder`) — todo el histórico: arranque, cierre de slot de 5 min, de hora y de día. Su estado (acumuladores, snapshots, `DailyTracker`) son miembros, no `static` de función.
- `alert_engine.cpp` (`AlertEngine`) — envuelve las máquinas de estado de `alerts_logic.h` con Telegram, banner, NVS y el aviso de espacio libre (`FsSpaceMonitor`, cada 15 min).
- `app_state.h` — globals compartidos con `main.cpp` (`g_cfg`, `g_mutex`, `g_energy`…) y `FlashGuard` (RAII sobre `g_flash_display_mutex`).

Detalles:
- Alineación exacta a slots de 5 min: `slot = epoch / 300`
- Registro en cambio de slot (`slot != _cur_5min_slot`): guarda la **media** de las muestras del periodo (`SlotAverager`, `slot_avg.h`), no la lectura puntual. El timestamp sigue siendo el inicio del slot que se abre
- `saveSessionState` (NVS) se escribe como mucho cada hora y en cada cambio de estado solar
- Agregación horaria al cambio de hora (media de potencias + acumulados al final). Tras un reinicio a mitad de hora el acumulador se siembra con los registros de 5 min ya guardados de esa hora
- `DailyRecord` actualizado en cada cambio de hora (parcial) y en medianoche (completo). Los totales salen de `DailyTracker` (`daily_tracker.h`): última lectura diaria BUENA por día local, no de `local_d` (a medianoche puede ser un fallo o ya los ceros del día nuevo)
- Los diarios se leen cada `POLL_DAILY_MS` (60 s; cada 10 s entre 23:58 y 00:02) **en la misma conexión TCP** que los instantáneos (`SolarmanClient::fetchAll`) — **crítico: deben seguir leyéndose para que `g_daily` y la cache estén actualizados**
- Con el logger caído: backoff del sondeo desde el 5º fallo (×2 hasta 60 s) y, tras ≥8 fallos, redescubrimiento por UDP 48899 (`logger_discovery.cpp`) verificado con lectura Modbus real
- UI ↔ red: `g_energy_seq`/`g_daily_seq` (contadores incrementados bajo `g_mutex`); `loop()` copia bajo el mutex y redibuja fuera de él
- `loop()` alimenta el watchdog (`feedLoopWDT`, activado con `enableLoopWDT()` al final de `setup()`)
- `DataStore` mantiene en PSRAM los epochs de hourly y daily por posición física (`_hrly_eps`/`_day_eps`) para buscar sin leer flash
- Montaje de LittleFS: 3 reintentos; si ya montó antes (NVS `fs_ok`) y falla, solo formatea con pulsación larga de 2 s en la splash

---

## Arquitectura LVGL en PSRAM

`lv_conf.h` configura `LV_USE_STDLIB_MALLOC=LV_STDLIB_CUSTOM` apuntando a `psram_alloc.h/.cpp`, que implementa los 5 símbolos `lv_mem_*`/`lv_malloc_core`/`lv_free_core`/`lv_realloc_core` que LVGL 9 requiere (ver tabla de bugs — LVGL 9 necesita 5 símbolos, no 3). El draw buffer de LVGL se reserva en `main.cpp` (`heap_caps_malloc`, tamaño según `screenWidth`/`screenHeight`).

---

## Pantallas (5 tiles horizontales)

```
[Dashboard] ←→ [Chart] ←→ [Stats] ←→ [Perfil energía] ←→ [Config]
     0             1          2              3                4
```

Indicador de posición: puntos de paginación en el borde inferior.

### Dashboard (tile 0)
- Barra superior 22px: reloj + autoconsumo instantáneo con color (verde≥80%, amarillo≥50%, rojo<50%)
- 4 tarjetas: Solar, Red, Batería (con barra SOC), Carga
- Autoconsumo = `(PV - export) / PV × 100` (solo cuando PV > 10W)

### Stats screen (tile 2)
- Dos donuts SVG: CONSUMO y PRODUCCIÓN
- Navegación día a día (± 6 días)
- Tap en título → volver a hoy (título en azul si no es hoy)
- Botón 📅 → `CalendarPopup`
- Hoy usa `g_daily` live; días anteriores usan `Cache.getDaily()`

**Fórmulas:**
- Solar directo = `load - discharge - import` (mín 0)
- Autoconsumo = `pv - export - batt_charge` (mín 0)

### Perfil de energía (tile 3) — `energy_profile.cpp`
- Reemplaza a la antigua pantalla Summary semanal. Vista mensual: una barra por día
- Mitad superior: PV + importación + descarga de batería; mitad inferior (negativa):
  consumo + exportación + carga de batería (valores diarios de `Cache.getDaily()`)
- Navegación mes a mes; escala Y automática con gridlines cada `EP_GRID_STEP` kWh

### Chart screen (tile 1)
- `lv_chart` con 5 series: PV, Grid, Bat, Carga, SOC
- Datos: `HourAgg` desde `Cache.getHourly()` (sin agregación en tiempo real)
- Labels eje Y con `add_y_labels()` en contenedor con `LV_OBJ_FLAG_OVERFLOW_VISIBLE`
- Popup al pulsar: línea vertical + valores de la hora
- **Fix medianoche:** `chart_screen_tick()` compara `day_epoch_from_offset(0)` con `s_day_epoch_loaded`
- **Fix eventos fantasma:** flag `s_chart_updating` durante `update_charts()`
- **Fix click:** usar `LV_EVENT_PRESSED` (no `LV_EVENT_CLICKED`) con `lv_chart_get_pressed_point()`
- **Fix scroll accidental:** `lv_obj_remove_flag(chart, LV_OBJ_FLAG_GESTURE_BUBBLE)`

### Config screen (tile 4)
- Scroll vertical habilitado (contenido total ~600px)
- Secciones: WiFi, Inversor, Gráfica, Pantalla, Telegram, Estado Red
- **Fix secciones:** `lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE)` para propagar gestos al padre
- **Teclado en `lv_layer_top()`** (NO en `lv_scr_act()` — causa crash al eliminar splash)
- Escáner WiFi: `WiFi.scanNetworks(async=true)` + `lv_timer` de 300ms para poll
- Guardar: solo reinicia si cambiaron WiFi/IP/serial; brillo y Telegram se aplican en caliente

### Splash screen
- Creada sobre pantalla separada, cargada con `lv_scr_load()`
- `splash_finish()` usa `lv_scr_load_anim(..., auto_del=true)` → elimina el splash
- `lv_timer_handler()` + `delay(5)` en cada `splash_update()` para render inmediato
- **Pasos:** LittleFS → DataStore → PSRAM Cache → NTP → WiFi (no bloqueante) → Webserver → Telegram

### Calendar popup
- Creado sobre `lv_layer_top()` con overlay semitransparente
- 42 celdas pre-creadas y reutilizadas al navegar meses
- Color verde = tiene datos (`Cache.dayHasData()` via bitmap O(1))
- Días desactivados: futuros y anteriores al primer registro disponible
- `cache.getOldestDailyEpoch()` para el límite inferior

---

## Sistema de backlight

```cpp
// arduino-esp32 3.x:
ledcAttach(pin, 10000, 8);   // 10kHz, 8bit
ledcWrite(pin, duty);
```

**Lógica de prioridad:**
```
¿Toque reciente (< inactividad_timeout)?
  → brillo normal (ignora horario nocturno)
¿Sin toque y (horario_nocturno OR inactividad_activa)?
  → brillo reducido
```

Transición suave: ±8 duty cada 16ms → ~160ms de rango completo.

**`begin()` debe llamarse DESPUÉS de `setup_display()`** porque el driver puede llamar a `digitalWrite()` que desconecta LEDC.

---

## Servidor web

### Rutas
- `GET /` — Dashboard HTML con AJAX (no recarga)
- `GET /chart` — Gráfica Chart.js con actualización incremental
- `GET /api/data` — JSON live + daily
- `GET /api/history` — Histórico (5min/hourly/daily, con `from_ts` para incrementales)
- `GET /api/status` — Estado del almacenamiento
- `GET /update` — Formulario OTA
- `POST /update` — Recepción del .bin y flasheo

### Bug conocido y resuelto
`handle_api_data()` usaba `s_daily` (puntero a `g_daily`) que puede estar desactualizado. Fix: leer primero desde `Cache.getDaily(today_ep)` y hacer fallback a `s_daily`.

### Gráfica `/chart`
- Granularidad 5min con actualización incremental (`from_ts`)
- `window._hourly_n[h]` calculado en cliente contando registros de 5min por hora
- Cambio de día automático a las 00:00:05 via `setTimeout`

---

## Bot de Telegram

**Librería:** `witnessmenow/Universal-Arduino-Telegram-Bot @ ^1.3.0` + `bblanchon/ArduinoJson @ ^7.0.0`

```cpp
WiFiClientSecure client;
client.setInsecure();
client.setBufferSizes(4096, 1024);   // evita OOM en handshake TLS
UniversalTelegramBot bot(token, client);
```

**Stack tarea:** 16 KB (requerido por TLS + ArduinoJson)

**Seguridad:** solo responde al `chat_id` configurado.

### Comandos implementados
`/estado`, `/bateria`, `/hoy`, `/dia YYYYMMDD`, `/semana`, `/sistema`, `/umbral N`, `/silenciar`, `/activar`, `/ayuda`

### Alertas proactivas
Encoladas via `xQueueSend` desde `solarmanTask`, procesadas en la tarea del bot:
`BATT_LOW`, `BATT_RECOVERED`, `SOLAR_START`, `SOLAR_STOP`, `LOGGER_FAIL`, `GRID_OUTAGE`, `GRID_RESTORED`, `TEST`

---

## NVS (namespace "cfg")

| Clave | Tipo | Contenido |
|---|---|---|
| `ssid`, `pass` | String | Credenciales WiFi |
| `lip`, `lserial` | String/ULong | IP y serie del datalogger |
| `ch_auto`, `ch_kw` | Bool/UChar | Config gráfica |
| `bl_norm`, `bl_red` | UChar | Brillo normal/reducido % |
| `bl_inact`, `bl_isecs` | Bool/UChar | Inactividad |
| `bl_night`, `bl_nstart`, `bl_nend` | Bool/UChar | Horario nocturno |
| `tg_token`, `tg_chatid` | String | Credenciales Telegram |
| `tg_batt`, `tg_solar`, `tg_grid`, `tg_logger` | UChar/Bool | Config alertas |
| `session` | Bytes | Último epoch grabado (recovery) |

---

## Constantes configurables y macros de escala

Ver `src/config.h` (defaults de app/WiFi/logger/NTP/polling, y fuentes/resolución sobreescribibles desde `platformio.ini` con `-D`) y `src/ui_constants.h` (macros `SX()`/`SY()`/`SS()` para escalar la UI a la resolución de cada placa).

---

## Tareas FreeRTOS

| Tarea | Core | Stack | Función |
|---|---|---|---|
| `loop()` | 1 | Sistema | LVGL render, UI updates, backlight |
| `solarmanTask` | 0 | 8 KB | Polling inversor cada 5s, grabación |
| `webserver_task` | 0 | 8 KB | HTTP server con `server.handleClient()` |
| `telegram_bot` | 0 | 16 KB | Polling bot + alertas proactivas |
| `cache_bg` | 0 | 4 KB | Carga background de caché hourly |

**Mutex `g_mutex`:** protege `g_energy`, `g_daily`, `g_energy_ready`, `g_daily_ready`.

**Variables globales compartidas:**
```cpp
EnergyData        g_energy{};
DailyStats        g_daily{};
SemaphoreHandle_t g_mutex;
volatile bool     g_energy_ready = false;
volatile bool     g_daily_ready  = false;
AppConfig         g_cfg{};           // cargada desde NVS en setup()
volatile bool     g_wifi_connected = false;
uint32_t          g_uptime_start = 0;
lv_obj_t*         g_main_screen = nullptr;
lv_obj_t*         g_tile_chart  = nullptr;
lv_obj_t*         g_tile_stats  = nullptr;
lv_obj_t*         g_tile_summary= nullptr;
```

---

## WiFi no bloqueante

`setup()` solo llama `WiFi.begin()` y continúa. `solarmanTask` gestiona:
1. Si `WL_CONNECTED` → continuar
2. Si no → reintentar cada 15s con `WiFi.disconnect()` + `WiFi.begin()`
3. Al conectar → esperar NTP hasta 10s (no bloqueante para LVGL)

---

## CI/CD (GitHub Actions)

- Trigger: `push` de tags `v*`
- Detecta entornos automáticamente desde `platformio.ini` via `.github/scripts/get_envs.py`
- Builds en paralelo por entorno con cache del toolchain
- Crea GitHub Release con todos los `.bin` adjuntos
- `ci_skip = true` en un entorno lo excluye del CI
- Prereleases automáticos si el tag contiene guión (`v1.1.0-beta`)

---

## Bugs importantes resueltos

| Bug | Causa | Fix |
|---|---|---|
| Respuesta incompleta 78/79 bytes | Inner header respuesta V5 es 14B, no 15B | `DATA_OFFSET = 28` |
| PV2 siempre 0 | Registro era 188, debe ser 187 | `IDX_PV2_POWER = (187-REG_BASE)` |
| Import/Export intercambiados | Reg 76=import, 77=export (al revés) | Swap de defines |
| Signo batería invertido | El código interpretaba positivo=cargando | Corregido: positivo=descargando |
| Crash al abrir teclado | Teclado creado en splash, eliminado con él | Usar `lv_layer_top()` |
| Eventos fantasma en chart | `lv_chart` dispara PRESSED al actualizar datos | Flag `s_chart_updating` |
| Scroll accidental entre tiles | Gesto burbujea del chart al tileview | `remove_flag(GESTURE_BUBBLE)` |
| Pantalla negra con canvas LVGL 9 | `lv_canvas_draw_*` eliminado en v9 | Revertido a `lv_chart` |
| `ledcAttach` no definido | API arduino-esp32 2.x vs 3.x | Ifdef por `ESP_ARDUINO_VERSION_MAJOR` |
| Donuts web vacíos | `fetchDailyStats()` no se llamaba | Añadida llamada + `g_daily` actualizado |
| `vTaskDelay` fuera del bucle | Código inalcanzable → CPU al 100% | Movido dentro de `for(;;)` |
| Crash SummaryScreen al deslizar | `lv_obj_clean()` durante animación | Patrón `s_needs_reload` en `tick()` |
| LVGL linker error `lv_mem_init` | LVGL 9 requiere 5 símbolos, no 3 | Añadir `lv_mem_init` y `lv_mem_deinit` |
| Cursor textarea no parpadea | Teclado sin grupo de foco | `lv_group_create()` + `lv_indev_set_group()` |
| JC4880P443C: pantalla negra, sin log, sin backlight | Timings DSI del datasheet oficial incorrectos + sin pin RST → `gfx->begin()` colgado enviando comando 0xBC de la secuencia ST7701 | Timings verificados de ESPHome (`vsync_front_porch=166`, no 16) + `rst=GPIO5` |
| JC4880P443C: imagen entrelazada/rota | `esp_lcd_panel_draw_bitmap` copia en bruto con stride nativo del panel (480px), incompatible con rotation=1 (buffer LVGL a 800px) | Flush vía `gfx->draw16bitRGBBitmap()` en vez de DMA2D directo |
| JC4880P443C: táctil totalmente descalibrado | `TAMC_GT911` configurado con dimensiones lógicas (800×480) en vez de físicas (480×800) → `width-x_raw` con width erróneo, valores desbordan uint16 | Constantes `TOUCH_GT911_CFG_W/H` separadas de `TOUCH_MAP_X1/X2/Y1/Y2`, calibradas empíricamente |

---

## Ficheros del proyecto

Todo el código vive en `src/` (un fichero `.h/.cpp` por subsistema: pantallas, protocolo Solarman, almacenamiento, backlight, web server, Telegram, etc. — ver `ls src/`). CI en `.github/workflows/build_release.yml` + `.github/scripts/get_envs.py`.

### Convenciones

- **Logs:** usar `APP_LOGE/W/I/D("Tag", "fmt", ...)` (`safe_serial.h`), no `DBGSERIAL.printf` suelto. Nivel de compilación `APP_LOG_LEVEL` (defecto INFO=3; `-DAPP_LOG_LEVEL=4` para ver DEBUG). Lo que ocurre en cada ciclo de 5 s va en DEBUG.
- **Configuración:** `Storage.load*()` devuelve copias de una caché en RAM; cualquier `Storage.save*()` la invalida. No escribir claves del namespace `cfg` por fuera de `Storage` o la caché quedaría obsoleta.
- **Servir contenido grande por web:** `server.send_P(...)` (o `sendContent` por trozos), nunca `server.send(code, type, const char*)`: ese copia todo el contenido a un `String`.
- **Compilar varias placas seguidas:** PlatformIO reinstala `tool-esptoolpy`/toolchain al saltar entre la plataforma de la Sunton y la de las P4 y la primera pasada puede dar `TypeError ... NoneType`; repetir el comando. No pasa en CI (un job por entorno).

---

## Estado actual del proyecto

El proyecto está **funcional y estable**. Las últimas sesiones de trabajo se centraron en:

1. Optimización del almacenamiento (Record5Min 32→16 bytes, file handles persistentes, índice días)
2. Ampliación a 1.461 días con las tres estructuras sincronizadas
3. Bot de Telegram con UniversalTelegramBot sustituyendo implementación propietaria
4. Calendario mensual en tres pantallas con días coloreados
5. Volver a hoy con tap en el título (color azul cuando no es hoy)
6. Fix medianoche en chart_screen
7. GitHub Actions para CI/CD con releases automáticos
8. README.md completo

**Pendiente / posibles mejoras futuras:**
- Exportación de datos a CSV desde la web
- Favicon y título en pestaña del navegador web
- Doble tap para volver al dashboard desde cualquier pantalla

**Backlog de mejoras numeradas (DEYE-NN):** ver la memoria del proyecto (`mejoras_backlog.md`).
