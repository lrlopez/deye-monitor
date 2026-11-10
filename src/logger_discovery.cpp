#include "logger_discovery.h"
#include "solarman.h"
#include "solarman_proto.h"
#include "config.h"
#include <WiFiUdp.h>
#include <lwip/sockets.h>
#include <errno.h>
#include <fcntl.h>

static const uint16_t DISCOVERY_PORT   = 48899;
static const uint16_t DISCOVERY_LOCAL  = 48898;
static const uint32_t DISCOVERY_WAIT_MS = 2000;
static const int      MAX_CANDIDATES   = 4;

// Barrido TCP de respaldo: algunos loggers no contestan al descubrimiento UDP (ni por broadcast ni
// por unicast) pero sí aceptan conexiones en el 8899. Se prueban en lotes pequeños porque lwIP solo
// tiene 16 sockets y los comparten el servidor web, Telegram y MQTT.
static const int      TCP_BATCH        = 6;
static const uint32_t TCP_WAIT_MS      = 350;

// Devuelve cuántas IP de la subred local aceptan una conexión TCP al puerto del logger (<= max).
static int logger_scan_tcp(char (*ips)[16], int max) {
    const uint32_t self = ntohl((uint32_t)WiFi.localIP());
    uint32_t first, last;
    if (!solarman_scan_range(self, ntohl((uint32_t)WiFi.subnetMask()), &first, &last)) return 0;

    int n = 0;
    uint32_t h = first;
    while (h <= last && n < max) {
        int fds[TCP_BATCH];
        uint32_t hosts[TCP_BATCH];
        int cnt = 0;
        while (cnt < TCP_BATCH && h <= last) {
            if (h == self) { h++; continue; }
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) break;                       // sin sockets libres: terminar este lote
            const uint32_t host = h++;
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
            sockaddr_in sa = {};
            sa.sin_family = AF_INET;
            sa.sin_port = htons(LOGGER_PORT);
            sa.sin_addr.s_addr = htonl(host);
            if (connect(fd, (sockaddr*)&sa, sizeof(sa)) < 0 && errno != EINPROGRESS) { close(fd); continue; }
            fds[cnt] = fd;
            hosts[cnt++] = host;
        }
        if (cnt == 0) break;                         // ni un socket disponible: no insistir

        bool done[TCP_BATCH] = {};
        bool open_[TCP_BATCH] = {};
        int pending = cnt;
        const uint32_t t0 = millis();
        while (pending > 0 && millis() - t0 < TCP_WAIT_MS) {
            fd_set w;
            FD_ZERO(&w);
            int maxfd = -1;
            for (int i = 0; i < cnt; i++)
                if (!done[i]) { FD_SET(fds[i], &w); if (fds[i] > maxfd) maxfd = fds[i]; }
            timeval tv = { 0, 50000 };
            if (select(maxfd + 1, nullptr, &w, nullptr, &tv) <= 0) continue;
            for (int i = 0; i < cnt; i++) {
                if (done[i] || !FD_ISSET(fds[i], &w)) continue;
                int err = 1;
                socklen_t el = sizeof(err);
                getsockopt(fds[i], SOL_SOCKET, SO_ERROR, &err, &el);
                open_[i] = (err == 0);
                done[i] = true;
                pending--;
            }
        }
        for (int i = 0; i < cnt; i++) {
            close(fds[i]);
            if (open_[i] && n < max) {
                snprintf(ips[n++], 16, "%u.%u.%u.%u", (unsigned)((hosts[i] >> 24) & 255),
                         (unsigned)((hosts[i] >> 16) & 255), (unsigned)((hosts[i] >> 8) & 255),
                         (unsigned)(hosts[i] & 255));
            }
        }
    }
    return n;
}

int logger_scan(char (*ips)[16], int max) {
    int n = 0;
    WiFiUDP udp;
    if (!udp.begin(DISCOVERY_LOCAL)) {
        DBGSERIAL.println("[Discovery] No se pudo abrir el socket UDP");
        return 0;
    }

    // Sondas habituales según el módulo WiFi del stick (la primera es la de la app de Solarman)
    static const char* PROBES[] = { "WIFIKIT-214028-READ", "WIFI-KIT", "HF-A11ASSISTHREAD" };
    for (const char* p : PROBES) {
        udp.beginPacket(WiFi.broadcastIP(), DISCOVERY_PORT);
        udp.write((const uint8_t*)p, strlen(p));
        udp.endPacket();
    }

    uint32_t t0 = millis();
    while (millis() - t0 < DISCOVERY_WAIT_MS && n < max) {
        int len = udp.parsePacket();
        if (len <= 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        char buf[64];
        int r = udp.read((uint8_t*)buf, sizeof(buf) - 1);
        if (r <= 0) continue;
        buf[r] = '\0';
        char ip[16];
        if (!solarman_parse_discovery_reply(buf, r, ip, sizeof(ip))) {
            DBGSERIAL.printf("[Discovery] Respuesta ignorada de %s: %.*s\n",
                             udp.remoteIP().toString().c_str(), r, buf);
            continue;
        }
        bool dup = false;
        for (int i = 0; i < n; i++) if (strcmp(ips[i], ip) == 0) dup = true;
        if (!dup) strcpy(ips[n++], ip);
    }
    udp.stop();
    DBGSERIAL.printf("[Discovery] UDP: %d candidato(s)\n", n);
    if (n == 0) {
        // Hay loggers que no contestan al UDP (comprobado con un LSW-3 real): buscar el puerto 8899 abierto
        n = logger_scan_tcp(ips, max);
        DBGSERIAL.printf("[Discovery] Barrido TCP %u: %d candidato(s)\n", (unsigned)LOGGER_PORT, n);
    }
    return n;
}

bool logger_verify(const char* ip, uint32_t serial, uint8_t unit_id) {
    if (serial == 0) return false;
    SolarmanClient probe(ip, LOGGER_PORT, serial, unit_id);
    EnergyData e{};
    return probe.fetchEnergyData(e) && e.valid;
}

bool logger_discover(uint32_t serial, uint8_t unit_id, char* ip_out, size_t ip_out_size) {
    if (serial == 0 || ip_out_size < 16) return false;

    char cand[MAX_CANDIDATES][16];
    const int n_cand = logger_scan(cand, MAX_CANDIDATES);

    // Verificar cada candidato con una lectura real firmada con nuestro nº de serie
    for (int i = 0; i < n_cand; i++) {
        if (logger_verify(cand[i], serial, unit_id)) {
            strncpy(ip_out, cand[i], ip_out_size - 1);
            ip_out[ip_out_size - 1] = '\0';
            DBGSERIAL.printf("[Discovery] Logger encontrado: %s\n", ip_out);
            return true;
        }
    }
    return false;
}
