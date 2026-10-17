#include "logger_discovery.h"
#include "solarman.h"
#include "solarman_proto.h"
#include "config.h"
#include <WiFiUdp.h>

static const uint16_t DISCOVERY_PORT   = 48899;
static const uint16_t DISCOVERY_LOCAL  = 48898;
static const uint32_t DISCOVERY_WAIT_MS = 2000;
static const int      MAX_CANDIDATES   = 4;

bool logger_discover(uint32_t serial, uint8_t unit_id, char* ip_out, size_t ip_out_size) {
    if (serial == 0 || ip_out_size < 16) return false;

    char cand[MAX_CANDIDATES][16];
    int  n_cand = 0;

    {
        WiFiUDP udp;
        if (!udp.begin(DISCOVERY_LOCAL)) {
            DBGSERIAL.println("[Discovery] No se pudo abrir el socket UDP");
            return false;
        }

        // Dos sondas habituales según el módulo WiFi del stick
        static const char* PROBES[] = { "WIFI-KIT", "HF-A11ASSISTHREAD" };
        for (const char* p : PROBES) {
            udp.beginPacket(WiFi.broadcastIP(), DISCOVERY_PORT);
            udp.write((const uint8_t*)p, strlen(p));
            udp.endPacket();
        }

        uint32_t t0 = millis();
        while (millis() - t0 < DISCOVERY_WAIT_MS && n_cand < MAX_CANDIDATES) {
            int len = udp.parsePacket();
            if (len <= 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
            char buf[64];
            int r = udp.read((uint8_t*)buf, sizeof(buf) - 1);
            if (r <= 0) continue;
            buf[r] = '\0';
            char ip[16];
            if (!solarman_parse_discovery_reply(buf, r, ip, sizeof(ip))) continue;
            bool dup = false;
            for (int i = 0; i < n_cand; i++) if (strcmp(cand[i], ip) == 0) dup = true;
            if (!dup) strcpy(cand[n_cand++], ip);
        }
        udp.stop();
    }

    DBGSERIAL.printf("[Discovery] %d candidato(s)\n", n_cand);

    // Verificar cada candidato con una lectura real firmada con nuestro nº de serie
    for (int i = 0; i < n_cand; i++) {
        SolarmanClient probe(cand[i], LOGGER_PORT, serial, unit_id);
        EnergyData e{};
        if (probe.fetchEnergyData(e) && e.valid) {
            strncpy(ip_out, cand[i], ip_out_size - 1);
            ip_out[ip_out_size - 1] = '\0';
            DBGSERIAL.printf("[Discovery] Logger encontrado: %s\n", ip_out);
            return true;
        }
    }
    return false;
}
