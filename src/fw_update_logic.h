#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ArduinoJson.h>

// Lógica pura de la actualización desde GitHub (sin Arduino/WiFi/Update), para
// poder probarla en nativo: comparar versiones, elegir el binario de esta placa
// y extraer los datos de la respuesta de la API de releases.

// ── Versiones ─────────────────────────────────────────────────────────────
struct FwVersion {
    int  major = 0, minor = 0, patch = 0;
    bool valid = false;
    bool prerelease = false;    // sufijo "-beta", "-rc1"…
};

// Acepta "3.6.0", "v3.6.0", "v3.7.0-beta" y "3.6" (patch = 0). Ignora "+build".
inline FwVersion fw_parse_version(const char* s) {
    FwVersion v;
    if (!s) return v;
    if (*s == 'v' || *s == 'V') s++;
    char* end = nullptr;
    long a = strtol(s, &end, 10);
    if (end == s) return v;
    v.major = (int)a;
    s = end;
    if (*s == '.') {
        long b = strtol(s + 1, &end, 10);
        if (end == s + 1) return v;
        v.minor = (int)b; s = end;
        if (*s == '.') {
            long c = strtol(s + 1, &end, 10);
            if (end == s + 1) return v;
            v.patch = (int)c; s = end;
        }
    }
    if (*s == '-') v.prerelease = true;
    else if (*s != '\0' && *s != '+') return v;     // basura tras el número
    v.valid = true;
    return v;
}

// -1 si a < b, 0 si iguales, 1 si a > b. A igual x.y.z, una prerelease es MENOR
// que la versión final.
inline int fw_compare(const FwVersion& a, const FwVersion& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.prerelease != b.prerelease) return a.prerelease ? -1 : 1;
    return 0;
}

// true si `latest` es estrictamente posterior a `current`. Si alguna no se puede
// interpretar, false: ante la duda no se ofrece actualizar.
inline bool fw_is_newer(const char* current, const char* latest) {
    FwVersion c = fw_parse_version(current), l = fw_parse_version(latest);
    if (!c.valid || !l.valid) return false;
    return fw_compare(c, l) < 0;
}

// ── Selección del binario ─────────────────────────────────────────────────
// El CI publica "deye_monitor_<entorno>_<tag>.bin" (ver build_release.yml).
inline bool fw_asset_matches(const char* asset_name, const char* env_name) {
    if (!asset_name || !env_name || !*env_name) return false;
    static const char PREFIX[] = "deye_monitor_";
    const size_t pl = sizeof(PREFIX) - 1, el = strlen(env_name), nl = strlen(asset_name);
    if (nl < pl + el + 3 + 4) return false;
    if (strncmp(asset_name, PREFIX, pl) != 0) return false;
    if (strncmp(asset_name + pl, env_name, el) != 0) return false;
    // Tras el entorno viene "_v<dígito>…" (la versión): así "sunton" no casa con
    // "sunton_4827s043" ni un entorno es prefijo de otro
    if (asset_name[pl + el] != '_' || asset_name[pl + el + 1] != 'v' ||
        asset_name[pl + el + 2] < '0' || asset_name[pl + el + 2] > '9') return false;
    return strcmp(asset_name + nl - 4, ".bin") == 0;
}

// ── Release ───────────────────────────────────────────────────────────────
struct FwRelease {
    bool     valid = false;
    char     tag[24]        = {0};
    char     html_url[128]  = {0};
    char     asset_name[80] = {0};
    char     asset_url[256] = {0};
    uint32_t asset_size     = 0;
    bool     has_digest     = false;
    char     sha256[65]     = {0};   // hex en minúsculas
    char     error[64]      = {0};   // motivo si !valid
};

inline void fw_copy(char* dst, size_t n, const char* src) {
    if (n == 0) return;
    strncpy(dst, src ? src : "", n - 1);
    dst[n - 1] = '\0';
}

inline bool fw_is_hex64(const char* s) {
    if (!s || strlen(s) != 64) return false;
    for (int i = 0; i < 64; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

// Interpreta la respuesta de GET /repos/<repo>/releases/latest y elige el
// binario de `env_name`. Solo se conservan los campos necesarios (filtro de
// ArduinoJson), así que no depende del tamaño del cuerpo de la release.
inline bool fw_parse_release(const char* json, const char* env_name, FwRelease& out) {
    out = FwRelease();

    JsonDocument filter;
    filter["tag_name"]  = true;
    filter["html_url"]  = true;
    filter["draft"]     = true;
    filter["prerelease"] = true;
    filter["assets"][0]["name"]                 = true;
    filter["assets"][0]["size"]                 = true;
    filter["assets"][0]["digest"]               = true;
    filter["assets"][0]["browser_download_url"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filter));
    if (err) { fw_copy(out.error, sizeof(out.error), "Respuesta de GitHub no válida"); return false; }

    const char* tag = doc["tag_name"] | "";
    if (!*tag) { fw_copy(out.error, sizeof(out.error), "La release no tiene etiqueta"); return false; }
    if (doc["draft"] | false) { fw_copy(out.error, sizeof(out.error), "La release es un borrador"); return false; }
    fw_copy(out.tag, sizeof(out.tag), tag);
    fw_copy(out.html_url, sizeof(out.html_url), doc["html_url"] | "");

    JsonArrayConst assets = doc["assets"];
    for (JsonObjectConst a : assets) {
        const char* name = a["name"] | "";
        if (!fw_asset_matches(name, env_name)) continue;
        const char* url = a["browser_download_url"] | "";
        if (strncmp(url, "https://", 8) != 0) continue;       // nunca descargar por HTTP
        fw_copy(out.asset_name, sizeof(out.asset_name), name);
        fw_copy(out.asset_url, sizeof(out.asset_url), url);
        out.asset_size = a["size"] | 0u;
        const char* dg = a["digest"] | "";
        if (strncmp(dg, "sha256:", 7) == 0 && fw_is_hex64(dg + 7)) {
            for (int i = 0; i < 64; i++) {
                char c = dg[7 + i];
                out.sha256[i] = (c >= 'A' && c <= 'F') ? (char)(c + 32) : c;
            }
            out.sha256[64] = '\0';
            out.has_digest = true;
        }
        break;
    }
    if (!*out.asset_url) {
        fw_copy(out.error, sizeof(out.error), "La release no incluye binario para esta placa");
        return false;
    }
    if (out.asset_size == 0) {
        fw_copy(out.error, sizeof(out.error), "El binario publicado tiene tamaño 0");
        return false;
    }
    out.valid = true;
    return true;
}

// ── Hash ──────────────────────────────────────────────────────────────────
// Compara un SHA-256 binario (32 bytes) con su representación hex (64 chars,
// mayúsculas o minúsculas).
inline bool fw_sha256_equals_hex(const uint8_t digest[32], const char* hex) {
    if (!fw_is_hex64(hex)) return false;
    static const char* H = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        char hi = hex[2 * i], lo = hex[2 * i + 1];
        if (hi >= 'A' && hi <= 'F') hi = (char)(hi + 32);
        if (lo >= 'A' && lo <= 'F') lo = (char)(lo + 32);
        if (hi != H[digest[i] >> 4] || lo != H[digest[i] & 0x0F]) return false;
    }
    return true;
}

// Una imagen de firmware ESP32 empieza por el byte mágico 0xE9
inline bool fw_looks_like_esp_image(const uint8_t* first_bytes, size_t n) {
    return n >= 1 && first_bytes[0] == 0xE9;
}
