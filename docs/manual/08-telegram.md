# Telegram

El equipo puede hablar con tu móvil a través de **Telegram**: te manda **avisos** cuando pasa algo
importante y responde a **comandos** con los datos que le pidas, estés donde estés. No necesita abrir
puertos en tu router: es el propio equipo quien contacta con Telegram.

## Crear el bot y conectarlo

1. En Telegram, abre una conversación con **@BotFather** y envía `/newbot`. Sigue las instrucciones:
   al terminar te da el **token** del bot (algo como `123456789:AAH...`).
2. Abre una conversación con tu nuevo bot y pulsa **Iniciar** (o envía cualquier mensaje).
3. Averigua tu **Chat ID**: la forma más sencilla es escribir a un bot como **@userinfobot**, que te
   responde con tu identificador numérico.
4. En el equipo, abre **Configuración → Telegram** (o la tarjeta **Telegram** de la
   [web de administración](07-interfaz-web.md#administracion)), escribe el token y el Chat ID y
   guarda.
5. Pulsa **Probar**: el bot te enviará un mensaje de confirmación.

![Configuración de Telegram](img/config-telegram.png)

!!! note "Seguridad"
    El bot **solo responde al Chat ID configurado**: si alguien más le escribe, no obtiene nada. No
    compartas el token del bot.

## Comandos

Escríbelos en la conversación con tu bot:

| Comando | Qué responde |
|---|---|
| `/estado` | Los valores instantáneos: producción solar, red, batería y consumo. |
| `/bateria` | Nivel, estado, potencia, tensión (si el inversor la informa) y **autonomía estimada** (cuánto queda o cuánto falta para llenarse). |
| `/hoy` | Totales del día: producción, consumo, importación, exportación, carga y descarga de batería, y los porcentajes de **autoconsumo** y **autosuficiencia**, además del **CO2 evitado**. |
| `/dia AAAAMMDD` | Lo mismo que `/hoy` para una fecha pasada, p. ej. `/dia 20260508`. |
| `/semana` | Resumen de los últimos siete días. |
| `/mes [AAAAMM]` | Resumen del mes (por defecto, el actual) con totales, autoconsumo, autosuficiencia y CO2, y su **variación respecto al mismo mes del año anterior** si hay datos. |
| `/anio [AAAA]` | Lo mismo para un año completo, comparado con el anterior. |
| `/sistema` | Información del equipo: temperatura del inversor (si está disponible), IP, señal WiFi, tiempo encendido, memoria y registros guardados. |
| `/umbral N` | Cambia el umbral de alerta crítica de batería (entre 5 y 95 %). |
| `/silenciar` | Silencia los avisos durante una hora. |
| `/activar` | Reactiva los avisos. |
| `/ayuda` | Lista de comandos. |

## Avisos automáticos

Los avisos se disparan **una sola vez** cuando se da la condición, y no se repiten hasta que esta se
recupera. Con la casilla de cada tipo en la configuración eliges cuáles recibes.

| Aviso | Cuándo salta | Cuándo se da por recuperado |
|---|---|---|
| **Aviso de batería** | El nivel baja del umbral de aviso (25 % por defecto). | Al subir 5 puntos por encima de ese umbral. |
| **Batería crítica** | El nivel baja del umbral crítico (20 % por defecto). | Ídem. |
| **Batería recuperada** | — | Cuando se recupera de cualquiera de las dos. |
| **Producción solar iniciada / detenida** (casilla **Solar**) | La producción supera 50 W (o baja de 20 W) durante 15 segundos seguidos. | — |
| **Corte de red** y **Red restaurada** (casilla **Red**) | La potencia de red se queda en 0 W durante un minuto (solo tras haber visto red antes). | En cuanto vuelve a haber intercambio. |
| **Inversor sin comunicación** (casilla **Logger**) | Cinco lecturas seguidas fallan (unos 25 segundos). | En cuanto una lectura vuelve a funcionar. |
| **Poco espacio en la memoria flash** | El espacio libre baja del 10 %. | Se rearma al superar el 15 %. |
| **Inversor muy caliente** | El radiador supera 65 °C durante dos lecturas seguidas. | Al bajar de 60 °C. |

Los avisos de batería, producción, red, comunicación, espacio y temperatura aparecen también como
**franja sobre la pantalla** del equipo y en la lista de alertas de la web.

!!! note "Temperatura del inversor"
    La temperatura se lee de un registro del inversor que depende del modelo y del firmware. Si tu
    inversor no la informa o el valor no es verosímil, se ignora y no se generan avisos de
    temperatura.

## Informe diario

Con la casilla **Informe diario** activada, el bot te envía **cada día a las 21:00** (hora del
equipo) el mismo resumen que `/hoy`, sin que tengas que pedirlo. Viene desactivado por defecto.
