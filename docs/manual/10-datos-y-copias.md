# Datos y copias de seguridad

El equipo guarda el historial de tu instalación en su propia memoria, sin depender de ningún
servidor. Este capítulo explica qué guarda, cuánto, y cómo sacarlo, copiarlo o borrarlo.

## Qué se guarda y durante cuánto tiempo

| Dato | Frecuencia | Para qué sirve |
|---|---|---|
| **Medidas de 5 minutos** | Cada 5 minutos, en punto (00, 05, 10…) | La [gráfica diaria de la web](07-interfaz-web.md#grafica-diaria). Cada registro es la **media** de ese periodo, no una lectura puntual. |
| **Resúmenes horarios** | Cada hora | La [gráfica](04-grafica.md) del dispositivo (24 h y 7 días). |
| **Totales diarios** | Cada día | [Estadísticas](03-estadisticas.md), [perfil de energía](05-perfil-de-energia.md), CSV y comandos de Telegram. |

El historial cubre **730 días (dos años)**. Pasado ese tiempo, los datos más antiguos se **sobrescriben
automáticamente** con los nuevos (es un registro circular): nunca se llena ni deja de grabar. Ocupa
alrededor de 3,9 MB de los 8 MB reservados; si el espacio libre llegara a bajar del 10 % (algo
anómalo), el equipo te avisa por pantalla y por Telegram.

Los datos **sobreviven a reinicios y cortes de luz**. Si el equipo estuvo apagado un rato, al
volver a arrancar completa lo que puede con lo que ya tenía guardado de esa hora.

!!! note "Hora y día correctos"
    El historial depende de la hora del equipo, que se sincroniza por internet (NTP) al conectar la
    WiFi, y de la **zona horaria** elegida en [Configuración → Red](06-configuracion.md#red): de ella
    depende dónde empieza y termina cada día. Los días de cambio de hora (23 o 25 horas) se tratan
    correctamente.

## Exportar a CSV

Desde el panel web (**CSV** en el pie) puedes descargar los **totales diarios** de un rango de fechas
o de un mes completo, en un fichero que se abre en cualquier hoja de cálculo. Ver
[Descargar los datos en CSV](07-interfaz-web.md#descargar-los-datos-en-csv).

## Copia de seguridad de la configuración

En `/admin`, la tarjeta **Copia de seguridad** permite:

- **Descargar la copia**: un fichero `deye_config_backup.json` con tu configuración (red, inversor,
  gráfica, Telegram, pantalla…).
- **Restaurar la copia**: sube ese fichero y pulsa **Restaurar copia** (pide confirmación, porque
  sobrescribe la configuración actual).

Es útil antes de una actualización arriesgada o al **sustituir el equipo** por otro.

!!! attention "Guarda el fichero en un lugar seguro"
    La copia contiene datos sensibles **en claro**: la contraseña de tu WiFi y el token de Telegram.
    La clave de administración **no** se incluye (solo se define desde la pantalla táctil).

## Copia de seguridad del historial

La tarjeta **Copia de seguridad de datos históricos** descarga **todo el historial** en un único
fichero y permite restaurarlo después, por ejemplo en un equipo nuevo:

- **Descargar datos**: genera el fichero con las medidas de 5 minutos, las horarias y las diarias.
- **Restaurar**: elige el fichero y súbelo. Antes de escribir nada, el equipo **comprueba que es
  compatible** (mismo formato y misma capacidad de almacenamiento); si no lo es, lo rechaza y no
  toca los datos actuales.

!!! note "Solo entre equipos con la misma versión de formato"
    La restauración funciona entre equipos con el mismo formato de almacenamiento. Si cambió entre
    versiones del firmware, la copia antigua puede no ser aceptada; en las notas de cada versión
    (*Releases* en GitHub) se indica si el formato cambió.

## Borrar el historial

Si quieres empezar de cero, **Configuración → Datos → Borrar todo el historial** (o la tarjeta
**Borrar historial** de la web) elimina todas las medidas. Es **irreversible**, por eso pide **dos
confirmaciones seguidas**. La configuración no se toca. El equipo se reinicia para recrear el
almacenamiento vacío.
