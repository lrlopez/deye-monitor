# MQTT y Home Assistant

Si usas **Home Assistant** u otro sistema de domótica, el equipo puede **publicar sus medidas por
MQTT** y registrarse él solo como dispositivo, para que sus sensores aparezcan sin escribir nada en
ficheros de configuración.

!!! note "Requisitos"
    Necesitas un **broker MQTT** accesible desde tu red (por ejemplo, el complemento *Mosquitto
    broker* de Home Assistant). El equipo se conecta **sin cifrado (TLS)**, así que úsalo solo en
    una red de confianza.

## Activar MQTT

MQTT se configura desde la **web de administración** (`/admin`, ver [Interfaz web](07-interfaz-web.md#administracion)),
en la tarjeta **MQTT / Home Assistant**:

| Campo | Qué poner |
|---|---|
| **Publicar en MQTT** | Marca la casilla para activarlo. Desactivado por defecto. |
| **Broker** | La dirección del servidor MQTT (IP o nombre). |
| **Puerto** | Normalmente 1883. |
| **Usuario** y **Contraseña** | Los de tu broker; déjalos vacíos si no usa autenticación. La contraseña guardada no se muestra: déjala vacía para conservarla, o marca **Borrar la contraseña guardada**. |
| **Prefijo** | Prefijo de los *topics* propios (por defecto `deye`). Sin barras al principio ni al final, ni espacios. |

Al guardar, el equipo se conecta al broker **sin reiniciar**; la propia tarjeta indica si está
**conectado** o **desconectado**. Si pierde la conexión, la reintenta cada 15 segundos.

## Qué publica

- **`<prefijo>/state`**: un mensaje con todas las medidas actuales, cada vez que llega una lectura
  nueva del inversor (como mucho cada cinco segundos) y, como mínimo, una vez por minuto.
- **`<prefijo>/status`**: `online` mientras el equipo está conectado y `offline` si se desconecta
  (lo publica el propio broker en su nombre).

Los sensores que ofrece son:

| Sensor | Unidad | Notas |
|---|---|---|
| Producción solar, Red, Batería potencia, Consumo | W | La red es positiva al importar y negativa al exportar. |
| Batería | % | Nivel de carga. |
| Tensión batería | V | Si el inversor la informa. |
| Temperatura inversor | °C | Si el inversor la informa. |
| Producción hoy, Consumo hoy, Importado hoy, Exportado hoy, Carga batería hoy, Descarga batería hoy | kWh | Acumulados del día; se reinician a medianoche. |

## Home Assistant

Con la integración **MQTT** de Home Assistant activada, el equipo se anuncia automáticamente: en
**Ajustes → Dispositivos y servicios → MQTT** aparece el dispositivo **Deye Monitor** con sus 13
sensores. Los sensores de energía (kWh) tienen el tipo adecuado para añadirse al **panel de
Energía** de Home Assistant (producción solar, consumo de la red, retorno a la red, batería…).

Si cambias el nombre mDNS del equipo, Home Assistant lo verá como un dispositivo nuevo (el nombre se
usa como identificador único).
