# Resolución de problemas

Antes de nada, dos comprobaciones que resuelven la mayoría de dudas:

1. Mira el **estado en la pantalla**: el icono de WiFi del panel y el aviso de «Sin datos hace…»
   te dicen si el problema es la red o el inversor.
2. Abre la página `/update` de la web (o el comando `/sistema` de Telegram): el bloque **Sistema**
   muestra la señal WiFi, la memoria libre y el tiempo encendido.

## El panel muestra «Sin datos hace…» o los valores están atenuados

El equipo no recibe lecturas del inversor.

- **¿Tiene WiFi?** Si el icono de la barra superior está gris o el aviso lo indica, el problema es la
  red: comprueba tu router, la señal en el lugar donde está el equipo y que la contraseña no haya
  cambiado (en **Configuración → WiFi**).
- **¿Ha cambiado la IP del stick?** Si tu router le asigna una dirección distinta tras un reinicio,
  el equipo deja de encontrarlo. Se **redescubre solo** al cabo de un rato sin comunicación, pero
  puedes forzarlo pulsando el botón de localización en **Configuración → Inversor** (con el número
  de serie escrito). Para que no vuelva a pasar, reserva una IP fija para el stick en tu router.
- **¿Es correcto el número de serie?** Debe coincidir con el del stick, en decimal.
- **¿Hay otra aplicación consultando al stick?** El stick atiende a pocos clientes a la vez; si
  tienes otra herramienta conectada de forma permanente, puede competir con el equipo.
- **¿Está encendido el inversor y el stick conectado a la WiFi?** Mira sus luces o la aplicación
  SolarmanPV.

## Los datos no coinciden con la aplicación del inversor

Compara los totales del día con SolarmanPV. Pequeñas diferencias de décimas de kWh son normales (los
totales del inversor se actualizan por su cuenta). Si la diferencia es grande, comprueba que el número
de serie es el de **tu** inversor y que **Inv. máx W**, **Red máx W** y **Cap. bat. Wh** en
[Configuración → Inversor](06-configuracion.md#inversor) son correctos (afectan a las escalas y a la
estimación de autonomía, no a los totales).

## La hora o los días del historial no son correctos

Comprueba la **zona horaria** en [Configuración → Red](06-configuracion.md#red) y que el equipo tiene
salida a internet (la hora se sincroniza por NTP). Un equipo sin internet puede arrancar con la hora
en blanco hasta que la consigue.

## No consigo abrir la web

- Si `http://inversor.local` no responde, prueba con la **IP** (en **Configuración → Red**): algunos
  móviles y ordenadores no resuelven los nombres `.local`.
- Si la administración (`/admin`) o la actualización (`/update`) devuelven un **error 403**, es que
  **no hay clave de administración definida**: defínela en **Configuración → Acceso web**.
- Si aparece un **error 429**, has fallado la clave varias veces seguidas: espera el tiempo
  indicado.
- Comprueba que el dispositivo desde el que accedes está en **la misma red** que el equipo.

## Olvidé la clave de administración

La clave se puede **cambiar** (no consultar) desde **Configuración → Acceso web** en la pantalla
táctil, sin necesidad de conocer la anterior. Escribe una nueva y pulsa **Guardar**.

## No me llegan los avisos de Telegram

1. En **Configuración → Telegram** pulsa **Probar**. Si no llega nada, revisa el **token** y el
   **Chat ID** y que hayas pulsado *Iniciar* en la conversación con tu bot.
2. Comprueba que no estén **silenciados** (`/activar` los reactiva).
3. Comprueba que está marcado el tipo de aviso que esperabas (Solar, Red, Logger).
4. El equipo necesita **salida a internet** para llegar a Telegram.

## Los sensores no aparecen en Home Assistant

- En la tarjeta **MQTT / Home Assistant** de `/admin`, comprueba que dice **conectado**. Si no, revisa
  el broker, el puerto y las credenciales.
- Verifica en Home Assistant que la integración **MQTT** está activa y que usa el prefijo de
  descubrimiento estándar (`homeassistant`).
- Si cambias el prefijo o el nombre mDNS del equipo, los sensores antiguos quedan huérfanos y
  aparecen otros nuevos.

## La estimación de autonomía no aparece

Solo se muestra cuando la batería se carga o descarga con más de 100 W durante un rato, y si el
resultado es razonable (menos de 48 horas). Comprueba también la **capacidad de la batería** en
Configuración → Inversor.

## Aparece «Poco espacio en la memoria flash»

El historial debería ocupar menos de la mitad de la memoria reservada. Si salta el aviso, consulta
el bloque **Sistema** de la web, descarga una copia de los datos
([Datos y copias](10-datos-y-copias.md)) y valora borrar el historial para empezar de cero.

## Al arrancar pide «Pulsa 2 s para FORMATEAR»

Ocurre cuando la memoria de datos, que antes funcionaba, ya no se puede leer. **El equipo no la
formatea por su cuenta**, para no borrar un historial recuperable. Si no tienes copia o aceptas
perderlo, **mantén pulsada la pantalla dos segundos** y se formateará; el equipo arrancará con el
historial vacío pero conservará la configuración.

## El equipo se reinicia solo

Consulta en el bloque **Sistema** el tiempo encendido y la **memoria libre mínima**: si es muy baja,
puede haber un problema de memoria. Comprueba también la alimentación (un cargador débil provoca
reinicios). Si persiste, actualiza al último firmware ([Actualizar el firmware](13-instalacion-del-firmware.md#actualizar-el-firmware)).

## La pantalla táctil responde mal o está descalibrada

Casi siempre indica que se ha instalado el **firmware de otra placa**. Comprueba que el nombre de la
placa que aparece en la página `/update` (campo **Placa**) coincide con la tuya y reinstala el
firmware correcto.
