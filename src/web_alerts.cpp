#include "web_alerts.h"
#include <time.h>

static constexpr uint8_t RING_SIZE = 24;
static WebAlertEntry     s_ring[RING_SIZE];
static uint8_t           s_count = 0;   // entradas válidas (satura en RING_SIZE)
static uint8_t           s_head  = 0;   // índice de la próxima escritura
static uint32_t          s_next_id = 1;
static SemaphoreHandle_t s_mutex = nullptr;

void WebAlerts_init() {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
}

void WebAlerts_push(AlertType type, int32_t value) {
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    s_ring[s_head] = WebAlertEntry{s_next_id++, (uint32_t)time(nullptr), type, value};
    s_head = (s_head + 1) % RING_SIZE;
    if (s_count < RING_SIZE) s_count++;

    xSemaphoreGive(s_mutex);
}

uint32_t WebAlerts_getSince(uint32_t since_id, WebAlertEntry* out, uint32_t max) {
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;

    // Recorrer el anillo de más antigua a más reciente.
    uint8_t start = (s_head + RING_SIZE - s_count) % RING_SIZE;
    uint32_t found = 0;
    for (uint8_t i = 0; i < s_count && found < max; i++) {
        const WebAlertEntry& e = s_ring[(start + i) % RING_SIZE];
        if (e.id > since_id) out[found++] = e;
    }

    xSemaphoreGive(s_mutex);
    return found;
}

uint32_t WebAlerts_lastId() {
    return s_next_id - 1;
}

const char* WebAlerts_level(AlertType type) {
    switch (type) {
        case AlertType::BATT_CRITICAL:
        case AlertType::LOGGER_FAIL:
        case AlertType::GRID_OUTAGE:
            return "critical";
        case AlertType::BATT_LOW:
        case AlertType::SOLAR_STOP:
        case AlertType::FS_LOW:
            return "warn";
        default:
            return "info";
    }
}

const char* WebAlerts_text(const WebAlertEntry& e, char* buf, size_t buf_size) {
    switch (e.type) {
        case AlertType::BATT_LOW:
            snprintf(buf, buf_size, "Batería baja: %d%%", (int)e.value); break;
        case AlertType::BATT_CRITICAL:
            snprintf(buf, buf_size, "Batería crítica: %d%%", (int)e.value); break;
        case AlertType::BATT_RECOVERED:
            snprintf(buf, buf_size, "Batería recuperada: %d%%", (int)e.value); break;
        case AlertType::SOLAR_START:
            snprintf(buf, buf_size, "Producción solar activa: %d W", (int)e.value); break;
        case AlertType::SOLAR_STOP:
            snprintf(buf, buf_size, "Producción solar parada"); break;
        case AlertType::LOGGER_FAIL:
            snprintf(buf, buf_size, "Sin datos del inversor"); break;
        case AlertType::GRID_OUTAGE:
            snprintf(buf, buf_size, "Corte de red eléctrica"); break;
        case AlertType::GRID_RESTORED:
            snprintf(buf, buf_size, "Red eléctrica restaurada"); break;
        case AlertType::TEST:
            snprintf(buf, buf_size, "Notificaciones activas"); break;
        case AlertType::FS_LOW:
            snprintf(buf, buf_size, "Poco espacio en la memoria flash: %d%% libre", (int)e.value); break;
        default:
            snprintf(buf, buf_size, "Alerta del sistema"); break;
    }
    return buf;
}
