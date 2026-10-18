# Introducción

**Deye Monitor** es un monitor de instalación solar fotovoltaica con inversor híbrido **Deye
SUN-6K-SG05** que funciona de forma autónoma: una pequeña placa con pantalla táctil que lee los datos
del inversor cada pocos segundos y los muestra en tiempo real, guarda el historial de **hasta dos
años**, ofrece una **web propia** accesible desde cualquier navegador de tu red, te avisa por
**Telegram** de lo importante y puede integrarse con **Home Assistant** por MQTT. No necesita
servidor, ni nube, ni suscripción: solo tu red WiFi.

![Panel principal del dispositivo](img/pantalla-dashboard.png)

## Qué puedes hacer con él

- **Ver de un vistazo** cuánto produce tu instalación, cuánto consumes, cómo está la batería y si
  estás importando o exportando energía a la red ([Panel principal](02-panel-principal.md)).
- **Entender el día**: totales de consumo y producción, autoconsumo, autosuficiencia, CO2 ahorrado y
  comparación con ayer y con la semana anterior ([Estadísticas](03-estadisticas.md)).
- **Revisar el historial**: gráfica de potencias de las últimas 24 horas o de la semana, y balance
  diario de todo el mes ([Gráfica](04-grafica.md), [Perfil de energía](05-perfil-de-energia.md)).
- **Configurar todo desde la propia pantalla** ([Configuración](06-configuracion.md)) o desde el
  navegador ([Interfaz web](07-interfaz-web.md)).
- **Recibir avisos y consultar datos desde el móvil** con el bot de Telegram
  ([Telegram](08-telegram.md)) o llevarlos a Home Assistant ([MQTT](09-home-assistant-mqtt.md)).
- **Exportar tus datos** a CSV y hacer copias de seguridad ([Datos y copias](10-datos-y-copias.md)).

## Quién es quién

- **Persona usuaria** — mira la pantalla o la web y consulta el historial. No necesita saber nada
  técnico: los capítulos 1 a 7 están escritos para ella.
- **Quien configura el equipo** — introduce la WiFi y los datos del inversor, define la clave de
  administración y activa Telegram o MQTT. Está cubierto desde el capítulo
  [Primeros pasos](01-primeros-pasos.md) en adelante.
- **Quien instala o actualiza el firmware** — vuelca el programa en la placa y lo mantiene al día.
  Es la única parte técnica y está al final: [Instalación y actualización del firmware](13-instalacion-del-firmware.md).
  Si tu equipo ya viene configurado, puedes saltártela por completo.

## Equipos compatibles

El mismo programa funciona en tres placas con pantalla táctil; la información que muestran es
idéntica y solo cambia la resolución:

| Placa | Pantalla |
|---|---|
| Sunton 4827S043 (ESP32-S3) | 480 × 272 |
| Guition JC4880P443C (ESP32-P4) | 800 × 480 (panel vertical girado) |
| Guition JC1060P470 (ESP32-P4) | 1024 × 600 |

Se comunica con el inversor a través del **stick WiFi LSW3** que acompaña al inversor (protocolo
SolarmanV5), así que no hace falta ningún cable entre la placa y el inversor: ambos solo necesitan
estar en la misma red WiFi.

!!! note "Sobre las capturas de este manual"
    Las capturas de la pantalla táctil se han generado con un simulador del propio firmware (mismas
    pantallas, mismos textos y mismas fuentes) a 800 × 480, con datos de ejemplo. Las de la web
    son del servidor integrado con los mismos datos. En tu equipo verás los tuyos, y en las placas
    de otra resolución los elementos se escalan sin cambiar de sitio.

## Cómo usar este manual

No hace falta leerlo de principio a fin:

- **¿Acabas de estrenar el equipo?** Empieza por [Primeros pasos](01-primeros-pasos.md).
- **¿Quieres entender lo que ves en pantalla?** [Panel principal](02-panel-principal.md),
  [Estadísticas](03-estadisticas.md), [Gráfica](04-grafica.md) y
  [Perfil de energía](05-perfil-de-energia.md).
- **¿Quieres cambiar un ajuste?** [Configuración](06-configuracion.md).
- **¿Prefieres usar el ordenador o el móvil?** [Interfaz web](07-interfaz-web.md).
- **¿Quieres avisos o integración con tu domótica?** [Telegram](08-telegram.md) y
  [MQTT y Home Assistant](09-home-assistant-mqtt.md).
- **¿Algo no funciona?** [Resolución de problemas](11-resolucion-de-problemas.md).
- **¿No conoces una palabra?** [Glosario](12-glosario.md).

## Convenciones

- Los nombres de botones, campos y secciones aparecen en **negrita** tal y como se ven en
  pantalla.
- Un **toque** es pulsar brevemente con el dedo; **deslizar** es arrastrar el dedo en horizontal
  para pasar de una pantalla a otra.
- Los recuadros de **nota** y **atención** señalan detalles que conviene no pasar por alto.

## Sobre el proyecto

Deye Monitor es software libre, publicado en
[github.com/lrlopez/deye-monitor](https://github.com/lrlopez/deye-monitor). Las versiones, con sus
novedades, están en la página de *Releases* del repositorio y en el fichero `CHANGELOG.md`. Este
manual describe la aplicación en su estado actual.
