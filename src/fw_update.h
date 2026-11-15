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
//  · Se ofrecen como actualización solo las versiones estables más nuevas, pero se puede elegir
//    cualquiera de las últimas releases publicadas (también una anterior) SIEMPRE que su modelo de
//    datos (DATA_MODEL_VERSION, ver fw_env_marker.h) no sea inferior al del firmware instalado.
//  · SHA-256 del binario contra el `digest` que publica la API (si lo hay).
//  · La imagen se escribe en la partición OTA inactiva y solo se activa tras
//    verificar tamaño, cabecera y hash; si algo falla, no se toca el firmware actual.
//
// Las dos operaciones largas (consultar, instalar) corren en una tarea propia
// para no bloquear el servidor web; el estado se consulta con fw_update_status().

#ifndef FW_MAX_RELEASES
#define FW_MAX_RELEASES 6    // releases que se conservan para elegir versión
#endif

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
    int      data_model;      // versión del modelo de datos de ESTE firmware
    char     target[24];      // versión que se está instalando (DOWNLOADING/INSTALLED)
    int      n_releases;      // cuántas releases devolvió la última consulta (fw_update_releases)
};

// Una release de la lista que devolvió la última consulta, para elegir versión.
struct FwReleaseInfo {
    char     tag[24];
    bool     prerelease;
    bool     installed;       // es la versión que está en marcha
    int8_t   cmp;             // frente a la instalada: -1 más antigua, 0 igual, 1 más nueva
    uint32_t size;
    bool     has_digest;
};

// Lanza la consulta en segundo plano. false si ya hay una operación en curso.
bool fw_update_check_async();

// Lanza la descarga+instalación de la release `tag` (una de fw_update_releases) o, con nullptr o
// vacío, de la última estable. false si no hay consulta válida, la etiqueta no está en la lista o hay
// otra operación en curso. El modelo de datos de la imagen se comprueba al descargarla (puede fallar
// después con state == ERROR).
bool fw_update_install_async(const char* tag = nullptr);

// Copia hasta `max` releases de la última consulta, de la más nueva a la más antigua; devuelve cuántas.
int  fw_update_releases(FwReleaseInfo* out, int max);

// Versión del modelo de datos de este firmware (DATA_MODEL_VERSION)
int  fw_data_model();

void fw_update_status(FwStatus& out);

const char* fw_state_name(FwState s);

// Marca «DEYE_FW_ENV:<entorno>» que lleva este binario (ver fw_env_marker.h). Quien reciba una imagen
// (subida manual, descarga) la recorre con un FwEnvScanner(fw_env_marker() + FW_ENV_MARK_PREFIX_LEN)
// y rechaza la de otro entorno. Referenciarla desde código garantiza que el enlazador no la descarte.
const char* fw_env_marker();

// Ídem para el modelo de datos («DEYE_DATA_MODEL:<n>»). Quien reciba una imagen la recorre con un
// FwModelScanner y la rechaza si !fw_model_allows(scanner.effective(), fw_data_model()).
const char* fw_model_marker();
