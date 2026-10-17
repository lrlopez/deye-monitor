#pragma once
#include <stdint.h>
#include <string.h>

// Lógica pura del protocolo SolarmanV5 + Modbus RTU (sin dependencias de
// Arduino/WiFi) para poder probarla en nativo (`pio test -e native`).

static constexpr int SOLARMAN_FRAME_LEN  = 36;   // petición FC03 completa
// 11 (cabecera V5) + 14 (inner header respuesta) + 3 (unit, FC, byte_count)
static constexpr int SOLARMAN_DATA_OFFSET = 28;

inline uint16_t solarman_crc16(const uint8_t* data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

//  [A5] [len_lo len_hi] [10 45] [seq_lo seq_hi] [serial x4 LE]
//  Payload: inner header (0x02 + 14 ceros) + Modbus RTU FC03 (8 B)
//  [checksum = suma buf[1..]] [15]
// `buf` debe tener al menos SOLARMAN_FRAME_LEN bytes. Devuelve la longitud.
inline int solarman_build_frame(uint8_t* buf, uint8_t unitId, uint32_t serial,
                                uint16_t seq, uint16_t startReg, uint16_t count) {
    uint8_t mb[8];
    mb[0] = unitId;
    mb[1] = 0x03;
    mb[2] = (startReg >> 8) & 0xFF;
    mb[3] =  startReg       & 0xFF;
    mb[4] = (count    >> 8) & 0xFF;
    mb[5] =  count          & 0xFF;
    uint16_t crc = solarman_crc16(mb, 6);
    mb[6] = crc & 0xFF;
    mb[7] = (crc >> 8) & 0xFF;

    const uint16_t payloadLen = 15 + sizeof(mb);   // 23
    int i = 0;
    buf[i++] = 0xA5;
    buf[i++] = payloadLen & 0xFF;
    buf[i++] = (payloadLen >> 8) & 0xFF;
    buf[i++] = 0x10;
    buf[i++] = 0x45;
    buf[i++] = seq & 0xFF;
    buf[i++] = (seq >> 8) & 0xFF;
    buf[i++] = serial        & 0xFF;
    buf[i++] = (serial >>  8) & 0xFF;
    buf[i++] = (serial >> 16) & 0xFF;
    buf[i++] = (serial >> 24) & 0xFF;
    buf[i++] = 0x02;
    memset(&buf[i], 0x00, 14);
    i += 14;
    memcpy(&buf[i], mb, sizeof(mb));
    i += sizeof(mb);
    uint8_t cs = 0;
    for (int j = 1; j < i; j++) cs += buf[j];
    buf[i++] = cs;
    buf[i++] = 0x15;
    return i;
}

enum class SolarmanParse : uint8_t { OK = 0, SHORT, BAD_FRAME, BAD_FC, BAD_CRC, EXCEPTION };

// Longitud esperada de la respuesta a una lectura de `count` registros.
inline int solarman_expected_len(uint16_t count) {
    return SOLARMAN_DATA_OFFSET + count * 2 + 4;   // +2 CRC Modbus +1 checksum +1 0x15
}

// Longitud TOTAL de la trama V5 según su propia cabecera (A5 + len LE + ...):
// 11 bytes de cabecera + payload + checksum + 0x15. Devuelve 0 si aún no han
// llegado los 3 primeros bytes o el inicio no es 0xA5.
inline int solarman_frame_total_len(const uint8_t* r, int n) {
    if (n < 3 || r[0] != 0xA5) return 0;
    return (int)(r[1] | (r[2] << 8)) + 13;
}

// true si la respuesta es una excepción Modbus (FC con el bit 7 activo, p.ej.
// 0x83). Sirve para abortar de inmediato en vez de esperar el timeout: la
// trama de excepción es más corta que la esperada.
inline bool solarman_frame_is_exception(const uint8_t* r, int n) {
    return n >= SOLARMAN_DATA_OFFSET - 1 && r[0] == 0xA5 &&
           (r[SOLARMAN_DATA_OFFSET - 2] & 0x80);
}

// Valida la respuesta y extrae los `count` registros (big-endian) a `values`.
inline SolarmanParse solarman_parse_response(const uint8_t* resp, int received,
                                             uint16_t count, uint16_t* values) {
    if (solarman_frame_is_exception(resp, received)) return SolarmanParse::EXCEPTION;
    if (received < solarman_expected_len(count)) return SolarmanParse::SHORT;
    if (resp[0] != 0xA5 || resp[received - 1] != 0x15) return SolarmanParse::BAD_FRAME;
    if (resp[SOLARMAN_DATA_OFFSET - 2] != 0x03) return SolarmanParse::BAD_FC;

    // CRC Modbus: unit_id + FC + byte_count + datos, LSB primero tras los datos
    const int mb_start    = SOLARMAN_DATA_OFFSET - 3;
    const int mb_data_len = 3 + count * 2;
    uint16_t crc_recv = (uint16_t)resp[mb_start + mb_data_len]
                      | ((uint16_t)resp[mb_start + mb_data_len + 1] << 8);
    if (crc_recv != solarman_crc16(&resp[mb_start], (uint8_t)mb_data_len))
        return SolarmanParse::BAD_CRC;

    for (uint16_t r = 0; r < count; r++) {
        int pos = SOLARMAN_DATA_OFFSET + r * 2;
        values[r] = ((uint16_t)resp[pos] << 8) | resp[pos + 1];
    }
    return SolarmanParse::OK;
}

// ── Descubrimiento del logger (UDP 48899) ─────────────────────────────────
// Los sticks Solarman responden a un broadcast con "IP,MAC,SERIAL" (texto
// ASCII). Solo se usa la IP: el candidato siempre se VERIFICA después con una
// lectura Modbus real usando nuestro nº de serie, así que un formato de
// respuesta inesperado simplemente no produce candidato.
inline bool solarman_is_valid_ipv4(const char* s) {
    int octets = 0, val = 0, digits = 0;
    for (const char* p = s;; p++) {
        char c = *p;
        if (c == '.' || c == '\0') {
            if (digits == 0 || val > 255) return false;
            octets++; val = 0; digits = 0;
            if (c == '\0') break;
        } else if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            if (++digits > 3) return false;
        } else {
            return false;
        }
    }
    return octets == 4;
}

// Extrae la IP (campo antes de la primera coma) de la respuesta. `out` debe
// tener >= 16 bytes. Devuelve false si no es una IPv4 válida.
inline bool solarman_parse_discovery_reply(const char* reply, int len, char* out, int out_size) {
    int n = 0;
    while (n < len && reply[n] != ',' && reply[n] != '\0' && reply[n] != '\r' && reply[n] != '\n') n++;
    if (n == 0 || n >= out_size || n > 15) return false;
    memcpy(out, reply, n);
    out[n] = '\0';
    return solarman_is_valid_ipv4(out);
}
