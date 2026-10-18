#pragma once

// Publicación MQTT con autodescubrimiento de Home Assistant (DEYE-30).
//
// Tarea propia (núcleo 0) que, si MQTT está activado y configurado (Storage.loadMqttConfig,
// editable en /admin), mantiene la conexión con el broker y publica el estado en
// <prefix>/state cada vez que llega una lectura nueva (como mucho cada 5 s), junto con la
// disponibilidad en <prefix>/status (Last Will «offline») y la configuración de los sensores
// en homeassistant/sensor/... al conectar. Solo MQTT sin TLS.
//
// mqtt_start() puede llamarse siempre: si MQTT está desactivado la tarea solo vigila la
// configuración. Los cambios de configuración se aplican en caliente (reconecta).
void mqtt_start();

// true mientras hay conexión con el broker (para mostrarlo en la web)
bool mqtt_connected();
