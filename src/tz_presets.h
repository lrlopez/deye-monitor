#pragma once
#include <stddef.h>

// Zonas horarias seleccionables desde la pantalla Configuración (DEYE-74).
// Cada una es una cadena TZ de POSIX (la que entiende setenv("TZ")/tzset), con sus reglas
// de cambio de hora. El índice guardado en NVS es la posición en esta tabla: NO reordenar
// ni borrar entradas, solo añadir al final. El 0 es el valor por defecto (config.h TIMEZONE).
struct TzPreset {
    const char* label;   // texto del desplegable (UTF-8; solo glifos presentes en las fuentes)
    const char* posix;
};

static const TzPreset TZ_PRESETS[] = {
    { "Espa\xC3\xB1\x61 / Europa central",     "CET-1CEST,M3.5.0,M10.5.0/3"   },
    { "Canarias / Portugal / R. Unido",        "WET0WEST,M3.5.0/1,M10.5.0"    },
    { "Europa oriental (Grecia, Ruman\xC3\xAD\x61)", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "UTC",                                   "UTC0"                         },
    { "M\xC3\xA9xico (centro)",                "CST6"                         },
    { "Colombia / Per\xC3\xBA",                "<-05>5"                       },
    { "Argentina / Uruguay",                   "<-03>3"                       },
    { "Chile",                                 "<-04>4<-03>,M9.1.6/24,M4.1.6/24" },
    { "EE. UU. este",                          "EST5EDT,M3.2.0,M11.1.0"       },
    { "EE. UU. centro",                        "CST6CDT,M3.2.0,M11.1.0"       },
    { "EE. UU. Pac\xC3\xAD\x66ico",            "PST8PDT,M3.2.0,M11.1.0"       },
};
static const size_t TZ_PRESET_COUNT = sizeof(TZ_PRESETS) / sizeof(TZ_PRESETS[0]);

// Índice válido (un valor guardado fuera de rango vuelve al por defecto)
static inline size_t tz_clamp_index(int idx) {
    return (idx >= 0 && (size_t)idx < TZ_PRESET_COUNT) ? (size_t)idx : 0;
}
