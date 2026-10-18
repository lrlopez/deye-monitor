# Changelog

Todos los cambios importantes de este proyecto están documentados en este fichero.

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el proyecto usa [Versionado Semántico](https://semver.org/lang/es/).

## [Unreleased]

### Añadido

- **CO2 evitado y autosuficiencia (DEYE-35):** la pantalla Estadísticas muestra la autosuficiencia del día junto al título «CONSUMO» y el CO2 evitado junto a «PRODUCCIÓN»; el dashboard web añade un indicador «CO2 evitado» a los de autosuficiencia y autoconsumo, y `/hoy` y `/dia` de Telegram añaden la línea de CO2. El CO2 es producción × factor de emisión del mix eléctrico (`TUNE_CO2_KG_PER_KWH`, 0,15 kg/kWh por defecto, ajustable con `-D`). Cálculos en `energy_metrics.h` con tests.
- **Exportación CSV de un mes completo (DEYE-38):** en el panel CSV del pie del dashboard web hay un selector de mes y un enlace «Mes completo», que descarga `deye_AAAAMM.csv` (también como `/api/export?month=AAAAMM`). Corrige de paso un fallo de la exportación por rango: recorría los días sumando 86 400 s, y en el domingo de cambio de hora (23/25 h) podía saltarse o repetir un día; ahora avanza por medianoche local.
- **Zona horaria configurable (DEYE-74):** Configuración → Red tiene un desplegable «Zona hor.» con 11 zonas (España/Europa central —por defecto—, Canarias/Portugal/R. Unido, Europa oriental, UTC, México, Colombia/Perú, Argentina/Uruguay, Chile y EE. UU. este/centro/Pacífico), cada una con sus reglas de cambio de hora. Un cambio reinicia el equipo, porque los epochs de medianoche del histórico se calculan con la zona vigente al arrancar. Ya no es una constante de `config.h`.
- **Autodetección de la IP del datalogger:** el asistente de primer arranque y Configuración → Inversor tienen un botón junto al campo «IP Logger» que busca el stick WiFi (broadcast UDP 48899). Si el nº de serie ya está escrito, verifica cada candidato con una lectura Modbus real y solo ofrece los verificados; un resultado se rellena directamente y varios se ofrecen en una lista. El asistente conecta a la WiFi al salir del paso 1 para que la búsqueda funcione.
- **Tarjeta «Sistema» en la web (DEYE-76, parcial):** la página `/update` muestra tiempo en marcha, memoria libre (y mínimo histórico), PSRAM libre, almacenamiento usado, señal WiFi e IP, refrescados cada 10 s. `/api/status` incluye ahora estos campos. La página lleva también icono y color de tema.
- **Asistente de primer arranque (DEYE-59):** un equipo sin WiFi configurada ofrece, tras la pantalla de inicio, un asistente a pantalla completa en cuatro pasos: red WiFi (con búsqueda de redes cercanas ordenadas por señal), IP y nº de serie del datalogger, clave de la web de administración (opcional, con repetición) y resumen. Valida cada paso con las reglas de Configuración, guarda y reinicia. «Omitir» (con confirmación) lo descarta para siempre; los equipos ya configurados no lo ven.
- **Vista de flujo de energía en el dashboard (DEYE-50):** tocando la barra superior (icono de la derecha) se alterna entre las tarjetas de siempre y un esquema Solar / Red / Batería alrededor de «Casa» con puntos animados que muestran el sentido de la energía (solar → casa, importando o exportando a la red, batería cargando o descargando) y su intensidad (3 velocidades). Solo se anima cuando fluyen más de 50 W y se detiene si los datos están obsoletos. La vista elegida se recuerda tras reiniciar.
- **Configuración en subpáginas con validación inmediata (DEYE-57):** la pantalla de Configuración deja de ser una lista de nueve secciones con scroll: ahora muestra un menú en rejilla (WiFi, Inversor, Gráfica, Red, Telegram, Pantalla, Acceso web, Datos), cada entrada con un resumen de su estado («MiRed», «Autoescalado», «Sin clave: web bloqueada»…), y al tocarla abre solo esa sección (el título superior vuelve al menú). Guardar sigue guardando todo a la vez desde cualquier subpágina.
  - Los campos se validan al teclear: borde rojo y un aviso flotante con el motivo (IP del logger, nº de serie, capacidades 1–65535, clave WiFi vacía o de 8–63 caracteres, nombre mDNS, token y chat ID de Telegram, clave web ≥ 8) y solo se pueden teclear los caracteres válidos (p. ej. dígitos y puntos en la IP). Guardar se niega mientras haya un campo inválido y abre la sección con el error. Las reglas (`input_validators.h`) tienen tests nativos.
  - Nuevo ajuste **«Reloj en reposo»** (DEYE-54) en Pantalla, también en la web de administración y en la exportación/importación de configuración (`ui_clock_mode`).
- **Gráfica: rango 24 h / 7 días y leyenda tocable (DEYE-52):** un botón junto al calendario alterna entre un día (24 puntos) y los 7 días que terminan en el día elegido (168 puntos, con separadores de día y rótulos «Lun, Mar…»; las flechas avanzan de 7 en 7 días). Tocar una entrada de la leyenda (PV, Red, Bat, Carga, SOC) oculta o muestra esa serie, y el autoescalado del eje solo cuenta las series visibles, de modo que ocultar las que no interesan equivale a ampliar la que sí. El popup de valores sirve en ambos rangos. El eje Y usa un decimal («0.4k») cuando el rango es corto. No hay zoom táctil con los dedos (LVGL 9 no trae zoom del chart): el selector de rango y la leyenda lo sustituyen.
- **Tendencias frente a ayer en Estadísticas (DEYE-53):** bajo cada donut aparecen ahora dos comparativas con flecha ▲/▼ y color (verde = bueno, rojo = malo): contra el día anterior y contra la semana anterior. Hoy se compara con ayer **a la misma hora** (usando los acumulados de los registros horarios), no con el día completo, que mostraría un −70 % engañoso por la mañana; un día pasado se compara completo con completo.
- **Modo reloj en reposo (DEYE-54):** con la misma inactividad que atenúa el brillo, la pantalla se cubre con un reloj grande, la fecha y tres datos (solar, consumo de la casa y batería). Cualquier toque lo retira y ese toque no llega a la pantalla de debajo. Activado por defecto; el ajuste se guarda en NVS (`ui_clock`).
- **Navegación más descubrible (DEYE-56):** los puntos de paginación son tocables (saltan a esa pantalla) y se ven bien (antes eran casi invisibles); la primera vez que se sale del dashboard aparece una pista («Toca dos veces la pantalla para volver al inicio») y otra, también una sola vez, avisa de que la barra superior cambia de vista. Las pistas ya mostradas se guardan en NVS.
- **Aviso persistente de datos obsoletos (DEYE-51):** pasados 30 s sin datos (o 30 s tras arrancar sin haber recibido ninguno) las tarjetas se atenúan y la barra superior muestra «Sin conexión WiFi» o «Sin datos hace X» en rojo hasta que vuelven a llegar datos.
- **Actualización del firmware desde GitHub:** en `/update` hay una nueva tarjeta «Actualizar desde GitHub»: el propio dispositivo consulta la última release publicada, la compara con la instalada y, si es posterior, descarga el `.bin` de SU placa y lo instala (doble confirmación). La subida manual del `.bin` sigue disponible justo debajo.
  - TLS con validación de la cadena contra raíces embebidas (`fw_update_ca.h`: Sectigo E46 e ISRG X1, que son las que usa GitHub hoy, más otras alternativas); nunca `setInsecure()`.
  - Verificación del SHA-256 publicado por la API (`digest`), tamaño y cabecera de imagen antes de activarla; si algo falla no se toca el firmware actual. Solo se ofrece una versión estrictamente posterior.
  - Corre en tareas propias (el servidor web sigue respondiendo y muestra el progreso) y congela el render de la pantalla durante la escritura, como la subida manual. Endpoints nuevos: `GET /api/fw/status`, `POST /api/fw/check`, `POST /api/fw/install` (contraseña de administración + anti-CSRF).
  - Cada entorno declara su nombre (`-DFW_ENV_NAME`) para elegir el binario `deye_monitor_<entorno>_<tag>.bin` que publica el CI.

### Mejorado

- **Borrar el historial pide dos confirmaciones (DEYE-58):** tanto en la pantalla táctil (segundo diálogo «Confirmación final» con «No, cancelar» como primer botón) como en la web (segundo `confirm`). Antes bastaba un solo diálogo, a pesar de que el CHANGELOG de la v3.5.0 hablaba de «doble confirmación». Reiniciar sigue con una confirmación (no es destructivo).
- **Contraste del texto secundario (DEYE-55):** los grises de las etiquetas pasan de 2,5:1 y 4,0:1 a 5,8:1 y 7,0:1 sobre las tarjetas (WCAG AA exige 4,5:1).

### Corregido

- **Pantalla congelada tras una actualización fallida:** si la subida manual del `.bin` fallaba al iniciar o al finalizar, el overlay «Actualizando firmware» y el render congelado se quedaban hasta reiniciar. Ahora se retira el overlay y se descongela (`g_ota_cancel`).

- **`/assets/chart.js` ya no reserva ~240 KB de heap por petición (DEYE-40):** `WebServer::send(code, tipo, const char*)` copia TODO el contenido a un `String` (el propio framework avisa «Use send_P for long arrays»). Chart.js, su plugin y la página `/update` se sirven ahora con `send_P`, directamente desde flash. Antes, en un heap fragmentado, esa reserva podía fallar y dejar `/chart` sin gráfica.

### Añadido

- **Alerta de poco espacio en la flash (DEYE-46):** si el espacio libre de LittleFS baja del 10 % se muestra un banner en pantalla y, si Telegram está configurado, se envía un aviso (también aparece en las notificaciones del navegador). Se comprueba cada 15 min y se rearma al volver por encima del 15 %.

### Mejorado

- **Configuración cacheada en RAM (DEYE-43, DEYE-22):** `Storage` mantiene copias de la config de red, gráfica, Telegram, brillo y contraseña de administración; cualquier `save*` las invalida. Ya no se lee NVS en cada petición web protegida ni cada 60 s para la alerta de Telegram.
- **Logs por niveles (DEYE-42):** nuevos `APP_LOGE/W/I/D` (`safe_serial.h`) con nivel de compilación `APP_LOG_LEVEL` (por defecto INFO). Las trazas de cada ciclo (lectura cada 5 s, slots de 5 min, datos diarios) pasan a DEBUG y por defecto ya no salen por el puerto serie; `-DAPP_LOG_LEVEL=4` en `build_flags` las restaura.
- **Sunton: LVGL renderiza en franjas de SRAM interna (DEYE-44):** dos buffers de 27 líneas (~26 KB cada uno) en lugar de dos buffers de pantalla completa en PSRAM (2 × 259 KB), que competían con el DMA del panel RGB. Libera ~490 KB de PSRAM; si no hubiera SRAM suficiente se vuelve al comportamiento anterior. Pendiente de validar visualmente en hardware.
- **`cache_bg` con prioridad explícita (DEYE-45):** `tskIDLE_PRIORITY` a propósito (solo usa el tiempo que dejan solarman/webserver/telegram) y al terminar registra el margen de pila.

### Interno

- **CI en cada push y pull request (DEYE-63):** nuevo `.github/workflows/ci.yml` (el de releases sigue disparándose solo con tags): tests nativos, análisis estático con cppcheck sobre `src/` (`pio check -e sunton_4827s043`, configurado en `platformio.ini`; falla con defectos altos o medios) y compilación de las tres placas con un informe del tamaño del binario y del uso de RAM/Flash en el resumen de la ejecución. El análisis encontró y se corrigió un campo sin inicializar en `DataStore` (`currentDaily`).
- **`APP_VERSION` desde `git describe` (DEYE-66):** `scripts/git_version.py` (pre-script de los tres entornos) fija la versión al compilar: `3.7.0` sobre la etiqueta `v3.7.0`, `3.6.0+24.g1a2b3c4` (y `.dirty` con cambios sin commitear) en builds de desarrollo. El sufijo `+…` lo ignora el comparador de versiones, así que una build posterior a `v3.6.0` no se ofrece como actualizable a `v3.6.0`. El valor de `config.h` queda como respaldo si no hay git/etiquetas, y el CI hace checkout completo para que funcione.
- **Constantes de ajuste en `tuning.h` (DEYE-64):** los umbrales y tiempos que estaban como números mágicos (inicio/fin de producción solar 50/20 W y 3 lecturas, corte de red 12 lecturas, fallos del logger, avisos de espacio, doble toque 600 ms, timeout del logger 3 s, umbral de flujo 50 W, datos obsoletos 10/30 s) tienen nombre `TUNE_*`, valor por defecto y se pueden sobreescribir con `-DTUNE_xxx=…` desde `platformio.ini`. Sin cambios de comportamiento.

- **`main.cpp` dividido (DEYE-41):** pasa de ~1190 a ~660 líneas. La tarea de red vive en `solarman_task.cpp`; el histórico en `recorder.cpp` (`Recorder`: slots de 5 min, cierre de hora y de día) y las alertas en `alert_engine.cpp` (`AlertEngine`), en lugar de ~470 líneas de una sola función con variables `static`. Estado global compartido declarado en `app_state.h` (`FlashGuard` sustituye a los `if (mutex) Take/Give` repetidos). Sin cambio de comportamiento.
- 49 tests nativos (+3 del monitor de espacio libre).

---

## [v3.6.0] - 2026-10-17

### Seguridad

- **Contraseña de administración obligatoria (DEYE-20):** sin contraseña definida, `/admin`, `/update` (OTA), importar/exportar/borrar datos, importar/exportar configuración y `/api/restart` responden `403` en lugar de quedar abiertas a toda la LAN. La contraseña se sigue definiendo solo desde la pantalla táctil (Config → Acceso web), que ahora avisa de que la web admin/OTA está bloqueada mientras no exista. **Cambio de comportamiento** para quien usaba la web sin clave.

### Corregido

- **Cambios de hora (DST) (DEYE-05):** los límites de día usaban `+86400 s`, incorrecto el domingo de cambio de hora (23/25 h): la hora 0 del día siguiente aparecía en la gráfica del día de primavera, los slots de caché quedaban desfasados 1 h tras un cambio de hora y el calendario mostraba 30 días en marzo. Nuevo `time_utils.h` con aritmética de días robusta.

- **Cierre de día fiable (DEYE-01):** el `DailyRecord` de ayer y el último `HourlyRecord` (23:00) usaban `local_d`, que a medianoche podía ser un fallo de lectura (`valid=false`) o ya la del día nuevo con contadores a cero, y el cierre salía del último registro horario (hasta 1 h de energía menos). Ahora se guarda la última lectura diaria BUENA por día local (`daily_tracker.h`), se sondea cada 10 s entre 23:58 y 00:02 y se descartan los reinicios anticipados de contadores en los últimos 5 min del día. Tras un reinicio a mitad de hora, el acumulador horario se siembra con los registros de 5 min de esa hora en lugar de empezar de cero.

### Mejorado

- **Una sola conexión TCP por ciclo (DEYE-08):** los registros instantáneos y los diarios se leen sobre el mismo socket (si el logger lo cierra, se reconecta una vez). Con el logger caído, el intervalo de sondeo se duplica a partir del 5º fallo hasta 60 s (el aviso de Telegram sigue saltando a los ~25 s).
- **Lectura del logger más rápida y robusta (DEYE-07):** lectura por bloques en vez de byte a byte; una excepción Modbus (FC 0x83…) o un socket cerrado abortan al instante en lugar de esperar los 3 s del timeout; la longitud esperada se ajusta a la que anuncia la propia trama.
- **Redescubrimiento automático del logger (DEYE-09):** tras ≥8 fallos seguidos (máx. cada 5 min) se lanza un broadcast UDP al puerto 48899 y cada candidato se verifica con una lectura Modbus real firmada con el nº de serie; si es el logger y su IP cambió, se guarda y se usa.
- **Sin bloqueos entre UI y red (DEYE-11):** `loop()` copia los datos nuevos bajo el mutex y lo suelta antes de redibujar (antes lo mantenía durante `dashboard_update`/`stats_screen_update`, pudiendo bloquear o descartar lecturas de `solarmanTask`); los flags `ready` pasan a contadores de secuencia.
- **Watchdog en `loop()` (DEYE-12):** la tarea de LVGL se suscribe al watchdog al final de `setup()` y lo alimenta en cada vuelta; si se cuelga, el chip se reinicia (timeout por defecto del core, 5 s).
- **Búsquedas del histórico sin leer flash (DEYE-04):** `pushDaily`, `readDaily` y la búsqueda binaria horaria usan una copia en PSRAM de los epochs (≈74 KB), construida al arrancar, en lugar de leer de flash bajo el mutex.
- **Registro de 5 min = media del periodo (DEYE-02):** antes guardaba la lectura puntual del instante de cambio de slot (un pico de nube/carga distorsionaba la curva); ahora promedia las ~60 muestras del slot.
- **Menos escrituras en flash/NVS (DEYE-03):** el estado de sesión en NVS se guarda como mucho cada hora (antes cada 5 min) y la actualización horaria del `DailyRecord` ya no reescribe `/meta2.bin`.

### Seguridad de datos

- **El histórico ya no se formatea por un fallo de montaje transitorio (DEYE-10):** el montaje de LittleFS se reintenta 3 veces. Si el dispositivo ya había montado bien alguna vez y sigue fallando, NO se formatea solo: hay que mantener pulsada la pantalla 2 s durante los 10 s siguientes; sin confirmación se arranca sin almacenamiento y los datos permanecen en flash. En el primer arranque se formatea como siempre.

### Interno

- Lógica pura extraída a headers sin dependencias de Arduino (`solarman_proto.h`, `slot_avg.h`, `time_utils.h`, `ring_index.h`, `alerts_logic.h`, `daily_tracker.h`) y 46 tests unitarios nativos (`pio test -e native`): protocolo, promedio de slot, DST, anillo/índice de días, búsqueda por epochs, backoff, cierre de día y máquinas de estado de alertas (DEYE-60). `DataStore` y `solarmanTask` usan esos headers sin cambio de comportamiento, salvo lo de abajo.
- Índice de días con búsqueda binaria (DEYE-06). Exige epochs estrictamente crecientes: un registro con fecha anterior al último día indexado (reloj que retrocede) ya no se añade al índice, y un `day_idx.bin` desordenado se reconstruye al arrancar.
- `CLAUDE.md` actualizado con las pantallas y capacidades reales (DEYE-61); `src/.DS_Store` sale del control de versiones (DEYE-62).

---

## [v3.5.0] - 2026-08-07

### Añadido

- **Copia de seguridad completa de datos históricos desde `/admin`:** nuevo `GET /api/data/export` (descarga TODO el historial de 5 min, horario y diario en un contenedor binario) y `POST /api/data/import` (lo restaura, con comprobación de compatibilidad de formato y de espacio libre en flash antes de cada sección). Solo funciona entre dispositivos con la misma versión de formato de almacenamiento.
- **Borrar todo el historial:** nueva acción con doble confirmación, disponible tanto en `/admin` (`POST /api/data/wipe`) como en la pantalla táctil (Configuración → Datos históricos), para vaciar por completo las medidas guardadas. Pensada para dejar sitio antes de importar una copia de otro dispositivo.

---

## [v3.4.0] - 2026-08-07

### Añadido

- **Exportar/importar configuración completa desde `/admin`:** nuevo `GET /api/config/export` (descarga un JSON con WiFi, inversor, gráfica, Telegram y pantalla) y `POST /api/config/import` (restaura desde ese JSON), con su UI en el panel de administración. La contraseña del propio panel se excluye deliberadamente — sigue siendo configurable solo desde la pantalla táctil.
- **Selector de rango de fechas para el CSV del dashboard:** el enlace «CSV» del pie de página despliega ahora un panel con dos selectores de fecha (por defecto los últimos 30 días, como antes) en lugar de exportar siempre un rango fijo. Usa el endpoint `/api/export?from=&to=`, que ya soportaba hasta 366 días.
- **Notificaciones del navegador:** mientras el dashboard está abierto (puede estar en segundo plano), muestra una notificación del sistema operativo con cada alerta nueva (batería, solar, red, logger), mediante la Notification API del navegador y un nuevo endpoint `GET /api/alerts`. No es Web Push real: no requiere service worker ni claves VAPID, y por tanto no funciona con la pestaña cerrada.
- **Rate-limit con backoff exponencial en la autenticación web:** a partir del 3er fallo consecutivo de contraseña en `/admin`, `/update` o la subida OTA, cada fallo adicional dobla la espera obligatoria (hasta un máximo de 5 minutos, respuesta `429` con `Retry-After`). Se resetea con un login correcto.

### Seguridad

- **Mitigación CSRF:** `/admin`, `/api/restart` y `/update` rechazan ahora las peticiones POST cuyo `Origin`/`Referer` no apunte al propio dispositivo. HTTP Basic Auth por sí solo no protege de CSRF, ya que el navegador reenvía las credenciales cacheadas también en peticiones disparadas desde otra web.
- **Sin CORS abierto en la API:** eliminada la cabecera `Access-Control-Allow-Origin: *` de todos los endpoints `/api/*`, que permitía leer datos de energía desde cualquier web de terceros si el navegador podía alcanzar la IP del dispositivo en la LAN.
- **Certificado TLS verificado en la conexión a Telegram:** sustituido `client.setInsecure()` (sin verificación de certificado, vulnerable a MITM) por `client.setCACert()` con la CA raíz real de `api.telegram.org`.

### Mejorado

- **Chart.js auto-alojado:** `/chart` cargaba chart.js y su plugin de anotaciones desde un CDN externo (`cdn.jsdelivr.net`), dejando de funcionar sin conexión a internet. Ahora se sirven desde el propio firmware.
- **Confirmación antes de reiniciar:** tanto el botón táctil «Reiniciar sin guardar» como «Reiniciar dispositivo» en `/admin` piden confirmación antes de ejecutar — un toque o clic accidental ya no reinicia el dispositivo sin avisar.
- **`pushDaily()` más rápido:** al actualizar el registro del día en curso (cada cambio de hora), recorre el buffer desde el más reciente en lugar de desde el principio; el caso común (actualizar «hoy») pasa de hasta 730 lecturas de flash a prácticamente O(1).

---

## [v3.3.0] - 2026-08-07

### Añadido

- **Soporte para Guition JC4880P443C (ESP32-P4, 480×800 px MIPI DSI):** nuevo entorno `guition_jc4880p443c` en `platformio.ini`. El panel es nativo vertical y se rota 90° por software a 800×480 lógico. Timings DSI y pin de reset (`GPIO5`) verificados en hardware real — los del datasheet oficial de terceros eran incorrectos y colgaban el panel a mitad de la secuencia de inicialización. El flush pasa por `draw16bitRGBBitmap()` en vez de la copia DMA2D en bruto usada en la JC1060P470, ya que esta última solo es válida sin rotación por software. Calibración táctil GT911 corregida separando las dimensiones físicas del panel (usadas para configurar el propio chip) de las dimensiones lógicas rotadas (usadas para el mapeo final a pantalla).

---

## [v3.2.1] - 2026-06-02

### Corregido

- **Alerta de posible corte de red eléctrica:** El aviso solo se dispara si se detecta 1 minuto sin exportación o importación.

---

## [v3.2.0] - 2026-05-26

### Añadido

- **Brillo de operación:** nuevo nivel de brillo independiente para cuando se está usando la pantalla táctil. Cuando pasa el período de inactividad, el brillo baja al nivel diurno o nocturno según el horario. Configurable desde la pantalla táctil y el panel web `/admin`.
- **Activación de brillo al recibir una alerta:** cuando aparece un banner de alerta en pantalla, el brillo sube automáticamente al nivel de operación como si se hubiera tocado la pantalla, garantizando que la notificación sea visible aunque el dispositivo estuviera en reposo.

### Corregido

- **Doble tap para volver al dashboard:** ahora funciona en todos los casos (se detecta en el flanco de subida del segundo press, con ventana de 600 ms y tileview guardado directamente en lugar de buscarlo por índice de hijo).
- **Fuentes:** La fuente más grande no se mostraba correctamente en todos los dispositivos.

---

## [v3.1.0] - 2026-05-25

### Añadido

- **Banner de alertas en pantalla:** las mismas alertas que se envían por Telegram (batería baja/crítica/recuperada, solar arranca/para, corte/restauración de red y fallo del logger) se muestran ahora también en pantalla como una franja temporal sobre `lv_layer_top()`, visible desde cualquier pantalla. El banner desaparece automáticamente a los 5 segundos. Thread-safe: `solarmanTask` lo encola desde Core 0 y el hilo LVGL lo procesa en Core 1.
- **Doble tap para volver al dashboard:** dos toques rápidos (< 400 ms entre tap y tap) desde cualquier pantalla navegan de vuelta al dashboard con animación de deslizamiento.
- **Indicador de frescura de datos:** la etiqueta de hora de última muestra en la barra superior del dashboard cambia de color según la antigüedad del dato: **verde** (< 10 s), **amarillo** (10–30 s) y **rojo** (> 30 s o sin datos aún).
- **Flechas de tendencia en batería y red:** las tarjetas de Batería y Red del dashboard muestran una flecha ↑ o ↓ junto al estado cuando la potencia varía más de 150 W respecto a la lectura anterior.
- **Comparativa semana anterior en estadísticas:** bajo cada donut de la pantalla de estadísticas diarias aparece la diferencia en porcentaje respecto al mismo día de la semana anterior (p.ej. `+12% sem.ant.`). Verde si mejora, rojo si empeora, gris si la variación es menor del 5 %.
- **Exportación de histórico a CSV:** nuevo endpoint `GET /api/export?from=YYYYMMDD&to=YYYYMMDD` que devuelve los totales diarios en formato CSV (fecha, PV, exportación, importación, consumo, carga y descarga de batería, SOC inicio/fin). Máximo 366 días por petición. El dashboard web incluye un enlace «CSV» en el pie de página que descarga por defecto los últimos 30 días.

### Mejorado

- **Botones de navegación unificados:** los botones anterior/siguiente de las pantallas de gráfica diaria y perfil de energía mensual tienen ahora el mismo tamaño, radio, fuente y colores que los de la pantalla de estadísticas, usando la constante compartida `C_GBTN` de `ui_constants.h`.

### Corregido

- **SafeSerial compatible con ESP32-P4:** `SafeSerial::begin()` era incompatible con `HWCDC` (la clase USB CDC que usa `Serial` en ESP32-P4), que hereda de `Stream` pero no de `HardwareSerial`. Se ha hecho el método genérico con un template `begin<T>(T& hw, ...)` que acepta cualquier tipo derivado de `Stream`.
- **Tabla de firmware en las notas de release:** el script de GitHub Actions generaba la cabecera de la tabla de firmwares tantas veces como entornos se compilaban; ahora se escribe una sola vez antes del bucle.

---

## [v3.0.0] - 2026-05-25

### Añadido

- **Fuentes personalizadas con caracteres españoles e iconos FA5:** Las fuentes Montserrat de la UI se han sustituido por versiones generadas a medida que incluyen tildes, eñes y caracteres especiales del español (á, é, í, ó, ú, Á, É, Í, Ó, Ú, ñ, Ñ, ç, ¿, ¡, º, ª…) además de los 23 codepoints de FontAwesome 5 Free utilizados en la interfaz. Las fuentes se compilan directamente en el firmware (`src/fonts/`), sin depender del subconjunto integrado en LVGL.
- **Indicador de cobertura WiFi en el dashboard:** Icono de señal WiFi en el extremo izquierdo de la barra superior del dashboard con código de color en tiempo real: gris (sin conexión), rojo (señal muy débil, ≤ −75 dBm), naranja (señal débil, −75…−60 dBm) y verde (buena señal, > −60 dBm). Se actualiza cada 5 segundos sin cargar la CPU.
- **Botón «Reiniciar sin guardar» en configuración:** Permite reiniciar el dispositivo de forma ordenada descartando los cambios pendientes del formulario. Antes de apagar llama a `Store.flush()` para persistir los metadatos del historial, garantizando que no se pierden registros de medición.
- **Horario nocturno con selector de rango:** El horario de brillo reducido nocturno se configura ahora con un único slider de rango en lugar de dos sliders independientes. El extremo izquierdo marca el fin de la noche (amanecer) y el derecho el inicio (tarde); la franja iluminada entre los dos knobs representa visualmente el período diurno. Los knobs son naranjos, el fondo es azul oscuro (noche) y el indicador central es gris claro (día).

### Mejorado

- **Iconos más descriptivos en la pantalla principal:** La tarjeta Solar muestra ahora un icono de sol en lugar del rayo de carga, y la tarjeta de Red usa un enchufe en lugar del símbolo WiFi, haciendo más intuitivo de un vistazo qué representa cada valor. El botón de calendario en las pantallas de estadísticas y gráfica muestra ahora un icono de calendario en lugar de una lista. La sección de gráfica en configuración usa un icono de gráfico de barras. La opción de horario nocturno incluye un icono de luna.
- **Textos en pantalla con tildes y eñes correctas:** Corregidas doce cadenas de texto visible en pantalla que carecían de acento o tilde: «Configuración», «BATERÍA», «Sin producción», «PRODUCCIÓN», «Exportación», «Sin datos para este día» (en dos pantallas), «Importación» y «Exportación» en el perfil energético mensual, «Sin conexión», «Caché PSRAM» en la pantalla de inicio, y «GRÁFICA» en la configuración.

### Corregido

- **Arranque instantáneo con historial completo:** El dispositivo ya no reconstruye el índice de días escaneando todo el historial en cada arranque; ahora lo carga de un fichero guardado, haciendo el inicio prácticamente inmediato incluso con años de datos almacenados.
- **Bot de Telegram más rápido al arrancar:** Los comandos del bot solo se registran en Telegram la primera vez (o cuando cambian); ya no se realiza una conexión segura innecesaria en cada reinicio.
- **Historial cargado más rápidamente en segundo plano:** La precarga del historial horario en RAM se completa ahora en ~3 segundos en lugar de ~29 segundos, reduciendo el tiempo hasta que la gráfica muestra datos históricos.
- **Código interno más claro:** Añadidos comentarios explicativos en los dos lugares donde se decrementa el día del mes para calcular la medianoche del día anterior, dependencia no obvia de la normalización de `mktime()`.
- **Bot de Telegram sin fragmentación de memoria:** La respuesta al comando /semana ya no construye el texto concatenando trozos uno a uno; ahora usa un único buffer, eliminando las reasignaciones de memoria SRAM que se acumulaban gradualmente.
- **Salida serie thread-safe:** Los mensajes de diagnóstico por puerto serie ya no pueden mezclarse si varias tareas escriben a la vez; cada llamada a `printf()` o `println()` es atómica.
- **Estabilidad interna del almacenamiento:** Corregida una inconsistencia en la inicialización del índice de días en memoria que podría haber causado un fallo al inicio si se hubiera ampliado la capacidad de historial.
- **Estabilidad interna de la caché:** Corregida una constante de tamaño hardcodeada en la caché de días que podría haber causado un fallo de memoria si se ampliaba la capacidad del historial.
- **Sin pérdida de datos al reiniciar:** Al reiniciar el dispositivo desde la web (botón de reinicio o actualización OTA), el historial se guarda correctamente antes de apagar; antes podían perderse hasta 12 registros de 5 minutos.
- **Fecha correcta en el cambio de hora:** La navegación por días históricos en las pantallas de estadísticas y gráfica ya calcula correctamente el día anterior al cambiar el horario de verano/invierno; antes podía mostrar el día equivocado en la madrugada del cambio.
- **Primer arranque más robusto:** Al inicializar la memoria interna por primera vez, el sistema ya no arriesga un reinicio inesperado por vigilante de hardware durante el proceso de formateo.
- **Protección de concurrencia en la caché:** Eliminada una ventana de tiempo en la lectura del historial de 5 minutos donde un escritor concurrente podría haber corrompido los datos en uso.
- **Credenciales de Telegram:** La pantalla de configuración ya no permite introducir un token o chat ID más largo de lo que el sistema puede almacenar; antes se guardaba silenciosamente truncado.
- **Diagnóstico de registros perdidos:** Si un registro horario o diario no se puede guardar por contención interna, ahora queda registrado en el log serie en lugar de descartarse silenciosamente.

---

## [v2.1.0] - 2026-05-24

### Corregido

- **Brillo nocturno:** El horario nocturno de reducción de brillo ya no se desactiva cuando la opción de reducción por inactividad también está habilitada; ambas opciones funcionan ahora de forma independiente y se aplican correctamente.
- **Menor desgaste de la memoria flash:** El dispositivo guardaba los metadatos del historial en flash en cada registro (cada 5 minutos); ahora los guarda una vez por hora, reduciendo significativamente el número de escrituras y alargando la vida útil de la memoria interna.
- **Reducción del consumo de memoria RAM:** Se eliminó un bloque de datos reservado en memoria que nunca se utilizaba, liberando aproximadamente 550 KB de RAM adicional para el resto de la aplicación.
- **Selección de red WiFi con nombre exacto:** Al pulsar una red en la lista de redes disponibles, el campo SSID se rellena ahora con el nombre exacto de la red, sin truncarlo aunque contenga paréntesis u otros caracteres especiales.
- **Número de serie del datalogger:** Se corrigió un error por el que números de serie mayores de 2.147.483.647 se guardaban incorrectamente; ahora se admiten todos los valores válidos (hasta 4.294.967.295).
- **Menor latencia al grabar el historial:** Los ficheros de historial horario y diario se mantienen abiertos permanentemente, igual que ya hacía el historial de 5 minutos, reduciendo el tiempo de escritura en cada registro.

---

## [v2.0.9] - 2026-05-24

### Añadido

- **Popup de detalle en perfil de energía:** Al pulsar sobre la barra de un día aparece un popup con los valores exactos en kWh de FV, importación, descarga de batería, consumo, exportación y carga de batería.
- **Volver al mes actual en perfil de energía:** El título del mes se muestra en azul cuando se está visualizando un mes anterior; pulsarlo regresa directamente al mes actual.

### Corregido

- **Perfil de energía — actualización automática:** La pantalla solo se refresca cuando llegan nuevos datos del inversor y únicamente si se está viendo el mes actual; los meses históricos no se recargan innecesariamente.

---

## [v2.0.8] - 2026-05-24

### Añadido

- **Aviso de actualización remota del firmware:** Muestra un mensaje en la pantalla táctil mientras se realiza una actualización OTA desde la web.

### Corregido

- **Etiquetas negativas en perfil de energía:** Se muestran correctamente las etiquetas del eje inferior.
- **Actualización del perfil de energía:** La pantalla se refresca automáticamente con los nuevos datos cuando está activa.

---

## [v2.0.7] - 2026-05-24

### Añadido

- **Pantalla "Perfil de energía":** nueva pantalla con gráfica de barras mensual. Cada día muestra dos barras apiladas simétricas: la superior acumula FV + Importación + Descarga batería; la inferior acumula Consumo + Exportación + Carga batería. Escala automática cada 20 kWh a partir de 60 kWh. Navegación mes a mes.

### Modificado

- **Orden de pantallas:** la gráfica diaria pasa a la segunda posición (justo después del dashboard), seguida de las estadísticas diarias y el nuevo perfil de energía.

---

## [v2.0.6] - 2026-05-23

### Modificado

- **Arco de batería (pantalla táctil y web):** el indicador de carga pasa de 3 segmentos de color fijo a 24 segmentos con degradado continuo rojo → amarillo → verde. El inicio del arco (0 %) es siempre rojo puro y el extremo (100 %) es verde puro.

### Corregido

- **Arco de batería (pantalla táctil):** eliminadas las discontinuidades visibles entre segmentos debidas al anti-aliasing del renderer de LVGL; cada segmento se extiende 1° para que el siguiente lo tape con su color sólido.
- **Pantalla de configuración:** eliminado el warning de compilación por conversión entre `lv_part_t` y `lv_state_t` en el estilo del cursor del área de texto.

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
