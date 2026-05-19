# Changelog

Todos los cambios importantes de este proyecto están documentados en este fichero.

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el proyecto usa [Versionado Semántico](https://semver.org/lang/es/).

---

## [Unreleased]

### Corregido
- `main.cpp`: acceso fuera de bounds en el array de tiles de navegación — el bucle iteraba hasta `i < 5` sobre un array de 4 elementos, accediendo a memoria adyacente en el stack (UB); corregido a `i < 4` y el array sacado fuera del bucle
- `solarman.cpp`: posible overflow del buffer de recepción TCP de 256 bytes si el datalogger enviaba más datos de los esperados; añadida comprobación `received < sizeof(resp)`. El bucle de recepción hacía busy-wait ocupando Core 0 durante hasta 3 segundos bloqueando el servidor web; añadido `vTaskDelay(1 ms)` cuando no hay datos disponibles

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
