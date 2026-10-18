#pragma once
#include <Arduino.h>
#include <freertos/queue.h>

// ── Tipos de alerta proactiva (sin cambios respecto a la API anterior) ────
enum class AlertType : uint8_t {
    BATT_LOW = 0,
    BATT_RECOVERED,
    BATT_CRITICAL,
    SOLAR_START,
    SOLAR_STOP,
    LOGGER_FAIL,
    GRID_OUTAGE,
    GRID_RESTORED,
    TEST,
    FS_LOW,         // poco espacio libre en la flash (value = % libre)
    OVERTEMP,       // radiador del inversor por encima del umbral (value = °C) — DEYE-71
    TEMP_OK,        // temperatura de vuelta a la normalidad (value = °C)
    DAILY_REPORT    // informe diario programado (solo Telegram; el texto es el de /hoy)
};

struct AlertMsg {
    AlertType type;
    int32_t   value;
};

// ── TelegramBot ───────────────────────────────────────────────────────────
class TelegramBot {
public:
    static TelegramBot& instance();

    // Llamar en setup() tras WiFi conectado
    void begin(const char* token, const char* chat_id);

    // Actualizar credenciales en caliente
    void setCredentials(const char* token, const char* chat_id);

    // Encolar alerta proactiva (thread-safe)
    bool enqueueAlert(AlertType type, int32_t value = 0);

    // Encolar mensaje (thread-safe)
    bool enqueueMessage(const String& msg);

    // Silenciar/activar alertas
    void silence(uint32_t seconds = 3600);
    void unsilence();
    bool isSilenced() const;

    bool isConfigured() const;

private:
    TelegramBot() = default;

    char          _token[64]   = {};
    char          _chat_id[32] = {};
    QueueHandle_t _alert_queue = nullptr;
    QueueHandle_t _message_queue = nullptr;
    uint32_t      _silence_until = 0;

    static void task(void* pv);

    // Procesado de comandos
    void handleCommand(const String& cmd, const String& from_id);

    // Respuestas a comandos
    String cmdEstado()   const;
    String cmdHoy()      const;
    String cmdDia(const String& date_str) const;
    String cmdSemana()   const;
    String cmdPeriodo(bool year, const String& arg) const;   // /mes /anio (DEYE-32)
    String cmdBateria()  const;
    String cmdSistema()  const;
    String cmdAyuda()    const;

    // Formateo de alertas proactivas
    String fmtAlert(const AlertMsg& msg) const;
};

#define Telegram TelegramBot::instance()
