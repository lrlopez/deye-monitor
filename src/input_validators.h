#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "solarman_proto.h"   // solarman_is_valid_ipv4

// Validadores de los campos de configuración (DEYE-57), sin dependencias de LVGL
// ni Arduino para poder probarlos en nativo. Todos aceptan una cadena terminada en
// '\0' y devuelven true si el valor es admisible. Las reglas "opcional" aceptan
// la cadena vacía.

static inline bool iv_all_digits(const char* s) {
    if (!s || !*s) return false;
    for (; *s; s++) if (*s < '0' || *s > '9') return false;
    return true;
}

// Dirección IPv4 del datalogger
static inline bool valid_logger_ip(const char* s) { return s && solarman_is_valid_ipv4(s); }

// Nº de serie del datalogger: decimal, 1..4294967295 (cabe en uint32_t)
static inline bool valid_logger_serial(const char* s) {
    if (!iv_all_digits(s) || strlen(s) > 10) return false;
    unsigned long long v = strtoull(s, nullptr, 10);
    return v >= 1 && v <= 4294967295ULL;
}

// Capacidades (W / Wh): 1..65535. Fuera de rango el guardado las sustituía en
// silencio por el valor por defecto; ahora se avisa antes.
static inline bool valid_capacity(const char* s) {
    if (!iv_all_digits(s) || strlen(s) > 5) return false;
    long v = atol(s);
    return v >= 1 && v <= 65535;
}

// Contraseña WiFi: vacía (red abierta) o de 8 a 63 caracteres (WPA2)
static inline bool valid_wifi_pass(const char* s) {
    if (!s) return false;
    size_t n = strlen(s);
    return n == 0 || (n >= 8 && n <= 63);
}

// Contraseña de administración web: vacía = no cambiar; si se escribe, 8..63
static inline bool valid_admin_pass(const char* s) { return valid_wifi_pass(s); }

// Nombre mDNS: vacío (se usa el de por defecto) o letras, dígitos y guiones, hasta
// 31 caracteres, sin guion al principio ni al final
static inline bool valid_mdns(const char* s) {
    if (!s) return false;
    size_t n = strlen(s);
    if (n == 0) return true;
    if (n > 31 || s[0] == '-' || s[n - 1] == '-') return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) return false;
    }
    return true;
}

// Token de bot de Telegram: vacío (sin Telegram) o "<dígitos>:<30+ caracteres [A-Za-z0-9_-]>"
static inline bool valid_tg_token(const char* s) {
    if (!s) return false;
    if (!*s) return true;
    const char* colon = strchr(s, ':');
    if (!colon || colon == s || colon - s > 12) return false;
    for (const char* p = s; p < colon; p++) if (*p < '0' || *p > '9') return false;
    const char* secret = colon + 1;
    size_t n = strlen(secret);
    if (n < 30 || n > 50) return false;
    for (size_t i = 0; i < n; i++) {
        char c = secret[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// Chat ID de Telegram: vacío o un entero con signo opcional ("-100123456789")
static inline bool valid_tg_chat(const char* s) {
    if (!s) return false;
    if (!*s) return true;
    if (*s == '-') s++;
    return iv_all_digits(s) && strlen(s) <= 19;
}
