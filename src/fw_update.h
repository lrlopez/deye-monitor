#pragma once
#include <Arduino.h>

// Actualización del firmware desde las releases de GitHub.
//
// Convive con la subida manual del .bin en /update: esta vía solo añade la
// posibilidad de que el propio dispositivo consulte la última release publicada
// en FW_UPDATE_REPO, compare versiones y descargue el binario de SU placa.
//
// Garantías:
//  · TLS con validación de la cadena contra raíces embebidas (fw_update_ca.h).
//  · Solo se ofrece una versión estrictamente posterior (no hay "bajar de versión").
//  · SHA-256 del binario contra el `digest` que publica la API (si lo hay).
//  · La imagen se escribe en la partición OTA inactiva y solo se activa tras
//    verificar tamaño, cabecera y hash; si algo falla, no se toca el firmware actual.
//
// Las dos operaciones largas (consultar, instalar) corren en una tarea propia
// para no bloquear el servidor web; el estado se consulta con fw_update_status().

enum class FwState : uint8_t {
    IDLE = 0,       // aún no se ha consultado nada
    CHECKING,       // consultando GitHub
    UP_TO_DATE,     // la última release no es más nueva que la instalada
    AVAILABLE,      // hay una versión más nueva con binario para esta placa
    DOWNLOADING,    // descargando y escribiendo (el dispositivo se reiniciará al acabar)
    INSTALLED,      // instalada; reiniciando
    ERROR           // ver `error`
};

struct FwStatus {
    FwState  state;
    char     env[32];         // placa (nombre del entorno de PlatformIO)
    char     current[32];     // versión instalada (en builds de desarrollo: "3.6.0+24.g1a2b3c4.dirty")
    char     latest[24];      // etiqueta de la última release (vacío si no consultada)
    char     notes_url[128];  // página de la release en GitHub
    char     asset[80];       // binario que se instalaría
    uint32_t size;            // bytes del binario
    uint32_t done;            // bytes ya escritos (DOWNLOADING)
    bool     has_digest;      // la API publicó SHA-256: se verificará
    char     error[96];       // motivo si state == ERROR
};

// Lanza la consulta en segundo plano. false si ya hay una operación en curso.
bool fw_update_check_async();

// Lanza la descarga+instalación de lo encontrado por la última consulta. false si
// no hay una versión AVAILABLE o hay otra operación en curso.
bool fw_update_install_async();

void fw_update_status(FwStatus& out);

const char* fw_state_name(FwState s);
