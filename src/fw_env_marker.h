#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Marca de entorno dentro del binario (sin Arduino, probada en nativo).
//
// Cada firmware lleva una cadena «DEYE_FW_ENV:<entorno>\0» (FW_ENV_NAME). Antes de activar una imagen
// recibida (subida manual por /update o descarga de GitHub) se recorren sus bytes buscando esa marca:
// si es de otro entorno (p. ej. el .bin de la JC1060P470 original en una rev. 3) se rechaza.
//
// El chip no lo impide: las cabeceras de las imágenes de ambas JC1060P470 admiten cualquier revisión.
//
// Una imagen SIN marca (firmware anterior a 4.4.2) se acepta: no se puede saber de qué placa es.

#define FW_ENV_MARK_PREFIX "DEYE_FW_ENV:"
#define FW_ENV_MARK_PREFIX_LEN (sizeof(FW_ENV_MARK_PREFIX) - 1)   // sin generar otra copia del literal

enum class FwEnvCheck : uint8_t {
    NOT_FOUND,   // la imagen no trae marca (firmware antiguo)
    MATCH,       // todas las marcas halladas son de este entorno
    MISMATCH,    // alguna marca es de otro entorno
};

// Autómata byte a byte: no necesita guardar bloques, así que las marcas partidas entre dos trozos
// de la subida se detectan igual. Se alimenta con feed() mientras se escribe la imagen.
class FwEnvScanner {
public:
    explicit FwEnvScanner(const char* expected_env) : _exp(expected_env ? expected_env : "") {}

    void feed(const uint8_t* b, size_t n) {
        for (size_t i = 0; i < n; i++) step(b[i]);
    }

    FwEnvCheck result() const {
        if (_mismatch) return FwEnvCheck::MISMATCH;
        return _found ? FwEnvCheck::MATCH : FwEnvCheck::NOT_FOUND;
    }
    // Entorno de la primera marca que no coincide (para el mensaje de error)
    const char* foreign() const { return _foreign; }

private:
    static const size_t MAX_VAL = 47;

    const char* _exp;
    size_t _pos = 0;           // bytes del prefijo ya reconocidos
    bool   _in_val = false;
    size_t _len = 0;
    char   _val[MAX_VAL + 1] = {0};
    char   _foreign[MAX_VAL + 1] = {0};
    bool   _found = false, _mismatch = false;

    void step(uint8_t c) {
        if (_in_val) {
            if (c == 0) { finish_value(); return; }
            if (_len < MAX_VAL) { _val[_len++] = (char)c; return; }
            _in_val = false; _pos = 0; return;        // demasiado largo: no es una marca válida
        }
        // 'D' es el único byte que abre el prefijo: sirve para reenganchar tras un fallo
        if ((char)c == FW_ENV_MARK_PREFIX[_pos]) {
            if (++_pos == FW_ENV_MARK_PREFIX_LEN) { _in_val = true; _len = 0; _pos = 0; }
        } else {
            _pos = ((char)c == FW_ENV_MARK_PREFIX[0]) ? 1 : 0;
        }
    }

    void finish_value() {
        _in_val = false;
        if (_len == 0 || !*_exp) return;   // sin nombre es otra cosa; y un build sin entorno no sabe comparar
        _val[_len] = '\0';
        if (strcmp(_val, _exp) == 0) {
            _found = true;
        } else if (!_mismatch) {
            _mismatch = true;
            memcpy(_foreign, _val, _len + 1);
        }
    }
};
