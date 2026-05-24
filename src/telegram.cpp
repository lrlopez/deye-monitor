#include "telegram.h"
#include "config.h"
#include "psram_cache.h"
#include "data_store.h"
#include "storage.h"
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <time.h>

// ── Datos compartidos del inversor (extern desde main.cpp) ─────────────────
extern EnergyData g_energy;
extern DailyStats g_daily;
extern SemaphoreHandle_t g_mutex;
extern uint32_t g_uptime_start;   // millis() en el arranque

// ── Singleton ─────────────────────────────────────────────────────────────
TelegramBot& TelegramBot::instance() {
    static TelegramBot t; return t;
}

// ── Helpers de fecha ──────────────────────────────────────────────────────
static uint32_t today_midnight() {
    time_t now; time(&now);
    struct tm t; localtime_r(&now, &t);
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0; t.tm_isdst = -1;
    return (uint32_t)mktime(&t);
}

static uint32_t parse_date(const String& s) {
    if (s.length() != 8) return 0;
    for (int i = 0; i < 8; i++) if (!isdigit(s[i])) return 0;
    int y = s.substring(0,4).toInt();
    int m = s.substring(4,6).toInt();
    int d = s.substring(6,8).toInt();
    if (y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    struct tm t{};
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d; t.tm_isdst = -1;
    return (uint32_t)mktime(&t);
}

static String fmt_date(uint32_t ep) {
    static const char* MESES[] = {
        "Ene","Feb","Mar","Abr","May","Jun",
        "Jul","Ago","Sep","Oct","Nov","Dic"
    };
    time_t t = (time_t)ep;
    struct tm tm; localtime_r(&t, &tm);
    char buf[20];
    snprintf(buf, sizeof(buf), "%d %s %d",
             tm.tm_mday, MESES[tm.tm_mon], tm.tm_year+1900);
    return String(buf);
}

static String fmt_kwh(float v) {
    char buf[12]; snprintf(buf, sizeof(buf), "%.2f kWh", v); return buf;
}
static String fmt_w(int v) {
    char buf[12]; snprintf(buf, sizeof(buf), "%d W", v); return buf;
}
static String fmt_pct(float num, float den) {
    if (den < 0.01f) return "N/A";
    char buf[8]; snprintf(buf, sizeof(buf), "%.0f%%", num/den*100.0f);
    return buf;
}

// ── Comandos ──────────────────────────────────────────────────────────────
String TelegramBot::cmdEstado() const {
    EnergyData e{};
    if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        e = g_energy; xSemaphoreGive(g_mutex);
    }
    if (!e.valid) return "⚠️ Sin datos del inversor aún.";

    // Signo de batería: positivo = descargando, negativo = cargando
    const char* batt_icon = (e.batt_power < 0)  ? "🔋⬆️ Cargando" :
                            (e.batt_power > 0)   ? "🔋⬇️ Descargando" :
                                                   "🔋 En reposo";
    const char* grid_icon = (e.grid_power > 0)   ? "🔌 Importando" :
                            (e.grid_power < 0)   ? "🔌 Exportando" :
                                                   "🔌 Sin intercambio";

    char buf[512];
    snprintf(buf, sizeof(buf),
        "⚡ *Estado actual*\n\n"
        "☀️ Solar: *%d W*\n"
        "   PV1: %d W  •  PV2: %d W\n\n"
        "%s: *%d W*\n\n"
        "%s\n"
        "   SOC: *%d%%*  •  %+d W\n\n"
        "🏠 Carga: *%d W*",
        (int)e.pv_power,
        (int)e.pv1_power, (int)e.pv2_power,
        grid_icon, abs((int)e.grid_power),
        batt_icon, (int)e.batt_soc, (int)e.batt_power,
        (int)e.load_power);
    return String(buf);
}

String TelegramBot::cmdBateria() const {
    EnergyData e{};
    if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        e = g_energy; xSemaphoreGive(g_mutex);
    }
    if (!e.valid) return "⚠️ Sin datos.";

    char buf[512];
    const char* estado =
        e.batt_power < 0 ? "🔋⬆️ Cargando" :
        e.batt_power > 0 ? "🔋⬇️ Descargando" : "🔋 En reposo";

    AppConfig acfg{}; Storage.loadConfig(acfg);
    float bat_cap = (float)(acfg.bat_cap_w > 0 ? acfg.bat_cap_w : BAT_CAP_W_DEF);

    String autonomia = "N/A";
    if (e.batt_power > 50) {
        float wh_left = e.batt_soc / 100.0f * bat_cap;
        float hours = wh_left / e.batt_power;
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "~%.1f h", hours);
        autonomia = tmp;
    } else if (e.batt_power < -50) {
        float wh_empty = (100 - e.batt_soc) / 100.0f * bat_cap;
        float hours = wh_empty / (-e.batt_power);
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "~%.1f h para carga completa", hours);
        autonomia = tmp;
    }

    snprintf(buf, sizeof(buf),
        "🔋 *Batería*\n\n"
        "SOC: *%d%%*\n"
        "Estado: %s\n"
        "Potencia: *%+d W*\n"
        "Estimación: %s",
        (int)e.batt_soc, estado, (int)e.batt_power, autonomia.c_str());
    return String(buf);
}

static String fmt_daily_stats(uint32_t dep, const DailyStats& d,
                               const char* title) {
    if (!d.valid) return "⚠️ Sin datos para ese día.";

    float pv_direct = d.load_kwh - d.batt_discharge_kwh - d.import_kwh;
    if (pv_direct < 0) pv_direct = 0;
    float autosuf = (d.load_kwh > 0.01f)
                    ? (d.load_kwh - d.import_kwh) / d.load_kwh * 100.0f : 0;
    float autocon = (d.pv_kwh > 0.01f)
                    ? (d.pv_kwh - d.export_kwh) / d.pv_kwh * 100.0f : 0;
    if (autosuf < 0) autosuf = 0; if (autosuf > 100) autosuf = 100;
    if (autocon < 0) autocon = 0; if (autocon > 100) autocon = 100;

    char buf[768];
    snprintf(buf, sizeof(buf),
        "%s\n📅 %s\n\n"
        "☀️ Producción: *%.2f kWh*\n"
        "🏠 Consumo: *%.2f kWh*\n\n"
        "🔌 Exportado: *%.2f kWh*\n"
        "🔌 Importado: *%.2f kWh*\n\n"
        "🔋 Carga bat.: *%.2f kWh*\n"
        "🔋 Descarga bat.: *%.2f kWh*\n\n"
        "📊 Autoconsumo: *%.0f%%*\n"
        "📊 Autosuficiencia: *%.0f%%*",
        title, fmt_date(dep).c_str(),
        d.pv_kwh, d.load_kwh,
        d.export_kwh, d.import_kwh,
        d.batt_charge_kwh, d.batt_discharge_kwh,
        autocon, autosuf);
    return String(buf);
}

String TelegramBot::cmdHoy() const {
    uint32_t dep = today_midnight();
    DailyRecord dr{};
    DailyStats d{};
    if (Cache.getDaily(dep, dr)) {
        d = daily_record_to_stats(dr);
    } else {
        // Fallback a g_daily
        if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            d = g_daily; xSemaphoreGive(g_mutex);
        }
    }
    return fmt_daily_stats(dep, d, "📈 *Energía de hoy*");
}

String TelegramBot::cmdDia(const String& date_str) const {
    uint32_t dep = parse_date(date_str);
    if (!dep) return "❌ Formato incorrecto. Usa: /dia YYYYMMDD";

    uint32_t today = today_midnight();
    if (dep > today) return "❌ Esa fecha es en el futuro.";

    DailyRecord dr{};
    DailyStats d{};
    if (Cache.getDaily(dep, dr)) d = daily_record_to_stats(dr);
    else if (!Store.readDaily(dep, dr))
        return "ℹ️ Sin datos para " + fmt_date(dep) + ".";
    else d = daily_record_to_stats(dr);

    return fmt_daily_stats(dep, d, "📅 *Energía del día*");
}

String TelegramBot::cmdSemana() const {
    uint32_t today = today_midnight();
    static const char* DIAS[] = {"Dom","Lun","Mar","Mié","Jue","Vie","Sáb"};

    float total_pv = 0, total_exp = 0, total_imp = 0, total_load = 0;

    // Formatear a buffer único para evitar fragmentación SRAM por String += iterativo
    char buf[768];
    int pos = snprintf(buf, sizeof(buf), "📊 *Resumen semanal*\n\n");

    for (int i = 6; i >= 0 && pos < (int)sizeof(buf) - 2; i--) {
        uint32_t dep = today - (uint32_t)i * 86400;
        DailyRecord dr{};
        DailyStats d{};
        if (Cache.getDaily(dep, dr)) d = daily_record_to_stats(dr);

        time_t t = (time_t)dep;
        struct tm tm_d; localtime_r(&t, &tm_d);
        const char* dia = DIAS[tm_d.tm_wday];

        if (!d.valid) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "%s %d: _sin datos_\n", dia, tm_d.tm_mday);
            continue;
        }

        float autosuf = (d.load_kwh > 0.01f)
                        ? (d.load_kwh - d.import_kwh) / d.load_kwh * 100.0f : 0;
        if (autosuf < 0) autosuf = 0;

        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "%s %d: ☀️%.1f 🏠%.1f 🔌%+.1f (%0.f%%)\n",
            dia, tm_d.tm_mday,
            d.pv_kwh, d.load_kwh,
            d.export_kwh - d.import_kwh,
            autosuf);

        total_pv   += d.pv_kwh;
        total_exp  += d.export_kwh;
        total_imp  += d.import_kwh;
        total_load += d.load_kwh;
    }

    float autosuf_total = (total_load > 0.01f)
                          ? (total_load - total_imp) / total_load * 100.0f : 0;
    if (pos < (int)sizeof(buf) - 1)
        snprintf(buf + pos, sizeof(buf) - pos,
            "\n*Total:* ☀️%.1f kWh  🏠%.1f kWh\n"
            "Export: %.1f  Import: %.1f\n"
            "Autosuficiencia: *%.0f%%*",
            total_pv, total_load, total_exp, total_imp, autosuf_total);

    return String(buf);
}

String TelegramBot::cmdSistema() const {
    uint32_t uptime_s = (millis() - g_uptime_start) / 1000;
    uint32_t h = uptime_s / 3600;
    uint32_t m = (uptime_s % 3600) / 60;

    char buf[512];
    snprintf(buf, sizeof(buf),
        "🖥️ *Sistema*\n\n"
        "📡 IP: `%s`\n"
        "📶 WiFi: %d dBm\n"
        "⏱️ Uptime: %luh %lum\n\n"
        "💾 SRAM libre: %lu KB\n"
        "💾 PSRAM libre: %lu KB\n"
        "📂 Flash usada: %lu KB / %lu KB\n\n"
        "📊 Registros: raw=%lu hrly=%lu day=%lu",
        WiFi.localIP().toString().c_str(),
        (int)WiFi.RSSI(),
        (unsigned long)h, (unsigned long)m,
        (unsigned long)ESP.getFreeHeap() / 1024,
        (unsigned long)ESP.getFreePsram() / 1024,
        (unsigned long)LittleFS.usedBytes() / 1024,
        (unsigned long)LittleFS.totalBytes() / 1024,
        (unsigned long)Store.getRawCount(),
        (unsigned long)Store.getHourlyCount(),
        (unsigned long)Store.getDailyCount());
    return String(buf);
}

String TelegramBot::cmdAyuda() const {
    return
        "⚡ *" APP_NAME "* — Comandos disponibles\n\n"
        "/estado — Valores instantáneos\n"
        "/bateria — Estado detallado de la batería\n"
        "/hoy — Totales del día con %%\n"
        "/dia _YYYYMMDD_ — Totales de una fecha\n"
        "/semana — Resumen de los últimos 7 días\n"
        "/sistema — Info del dispositivo\n"
        "/umbral _N_ — Cambiar umbral alerta bat. (%%)\n"
        "/silenciar — Silenciar alertas 1 hora\n"
        "/activar — Reactivar alertas\n"
        "/ayuda — Este mensaje";
}

// ── Formateo de alertas proactivas ────────────────────────────────────────
String TelegramBot::fmtAlert(const AlertMsg& msg) const {
    switch (msg.type) {
    case AlertType::BATT_LOW:
        return "⚠️ *Aviso batería*\nSOC: *" + String(msg.value) + "%*\n"
               "Nivel de aviso alcanzado.";
    case AlertType::BATT_CRITICAL: {
        String bat = cmdBateria();
        return "🔴 *Batería crítica*\nSOC: *" + String(msg.value) + "%*\n"
               + bat.substring(bat.indexOf('\n')+1);
    }
    case AlertType::BATT_RECOVERED:
        return "✅ *Batería recuperada*\nSOC: *" + String(msg.value) + "%*";
    case AlertType::SOLAR_START:
        return "☀️ *Producción solar iniciada*\n" + fmt_w(msg.value);
    case AlertType::SOLAR_STOP:
        return "🌙 *Producción solar detenida*";
    case AlertType::LOGGER_FAIL:
        return "❌ *Sin comunicación con el inversor*\n"
               "Verifica la red y la IP del datalogger.";
    case AlertType::GRID_OUTAGE:
        return "🔴 *Posible corte de red eléctrica*\n"
               "Importación: " + fmt_w(msg.value);
    case AlertType::GRID_RESTORED:
        return "🟢 *Red eléctrica restaurada*";
    case AlertType::TEST:
        return "🔔 *Notificaciones activas*\n" APP_NAME " funcionando correctamente.";
    default:
        return "📡 Evento";
    }
}

// ── Procesado de comandos entrantes ───────────────────────────────────────
void TelegramBot::handleCommand(const String& text, const String& from_id) {
    // Seguridad: solo responder al chat_id configurado
    if (strcmp(from_id.c_str(), _chat_id) != 0) {
        DBGSERIAL.printf("[Bot] Mensaje de chat_id no autorizado: %s\n",
                      from_id.c_str());
        return;
    }

    String cmd = text;
    cmd.trim();

    String response;

    if (cmd == "/estado" || cmd == "/start") {
        response = cmdEstado();
    } else if (cmd == "/bateria") {
        response = cmdBateria();
    } else if (cmd == "/hoy") {
        response = cmdHoy();
    } else if (cmd.startsWith("/dia")) {
        String arg = cmd.substring(4); arg.trim();
        response = cmdDia(arg);
    } else if (cmd == "/semana") {
        response = cmdSemana();
    } else if (cmd == "/sistema") {
        response = cmdSistema();
    } else if (cmd.startsWith("/umbral")) {
        String arg = cmd.substring(7); arg.trim();
        int n = arg.toInt();
        if (n < 5 || n > 95) {
            response = "❌ Umbral debe estar entre 5 y 95.";
        } else {
            TelegramConfig cfg = Storage.loadTelegramConfig();
            cfg.batt_threshold = (uint8_t)n;
            Storage.saveTelegramConfig(cfg);
            char buf[64];
            snprintf(buf, sizeof(buf),
                "✅ Umbral de batería cambiado a *%d%%*", n);
            response = buf;
        }
    } else if (cmd == "/silenciar") {
        silence(3600);
        response = "🔕 Alertas silenciadas durante 1 hora.";
    } else if (cmd == "/activar") {
        unsilence();
        response = "🔔 Alertas reactivadas.";
    } else if (cmd == "/ayuda" || cmd == "/help") {
        response = cmdAyuda();
    } else {
        response = "❓ Comando no reconocido. Usa /ayuda para ver los disponibles.";
    }

    enqueueMessage(response);
}

// ── Envío de mensaje ──────────────────────────────────────────────────────
bool TelegramBot::sendMsg(const String& text) {
    // El bot object se crea en la tarea — no podemos usarlo aquí directamente
    // Se encola como alerta especial... pero con UniversalTelegramBot
    // el envío se hace desde la tarea directamente.
    // Esta función no se usa externamente — solo para compatibilidad.
    return false;
}

// ── Tarea FreeRTOS ────────────────────────────────────────────────────────
void TelegramBot::task(void* pv) {
    TelegramBot* self = static_cast<TelegramBot*>(pv);

    // Esperar NTP
    DBGSERIAL.println("[Bot] Esperando NTP...");
    uint32_t t0 = millis();
    while (time(nullptr) < 1700000000UL && millis() - t0 < 30000)
        vTaskDelay(pdMS_TO_TICKS(1000));

    // Inicializar cliente HTTPS
    WiFiClientSecure client;
    client.setInsecure();

    UniversalTelegramBot bot(self->_token, client);

    // Registrar comandos en BotFather solo cuando la lista cambia (evita TLS en cada boot)
    static constexpr uint8_t BOT_CMDS_VER = 1;
    {
        Preferences prefs;
        prefs.begin("cfg", true);
        uint8_t stored = prefs.getUChar("bot_ver", 0);
        prefs.end();
        if (stored != BOT_CMDS_VER) {
            bool ok = bot.setMyCommands(
                "[{\"command\":\"estado\",\"description\":\"Valores instantáneos\"},"
                "{\"command\":\"bateria\",\"description\":\"Estado de la batería\"},"
                "{\"command\":\"hoy\",\"description\":\"Totales del día\"},"
                "{\"command\":\"dia\",\"description\":\"Totales de una fecha (YYYYMMDD)\"},"
                "{\"command\":\"semana\",\"description\":\"Resumen semanal\"},"
                "{\"command\":\"sistema\",\"description\":\"Info del dispositivo\"},"
                "{\"command\":\"umbral\",\"description\":\"Cambiar umbral alerta batería\"},"
                "{\"command\":\"silenciar\",\"description\":\"Silenciar alertas 1h\"},"
                "{\"command\":\"activar\",\"description\":\"Reactivar alertas\"},"
                "{\"command\":\"ayuda\",\"description\":\"Lista de comandos\"}]"
            );
            if (ok) {
                prefs.begin("cfg", false);
                prefs.putUChar("bot_ver", BOT_CMDS_VER);
                prefs.end();
                DBGSERIAL.println("[Bot] Comandos registrados en BotFather.");
            }
        }
    }

    DBGSERIAL.println("[Bot] Iniciado y escuchando comandos.");

    uint32_t last_poll = 0;

    for (;;) {
        // ── Procesar alertas proactivas de la cola ────────────────────────
        AlertMsg alert{};
        while (xQueueReceive(self->_alert_queue, &alert, 0) == pdTRUE) {
            if (WiFi.status() != WL_CONNECTED) break;
            if (time(nullptr) < 1700000000UL) break;
            if (!self->isSilenced()) {
                String msg = self->fmtAlert(alert);
                bot.sendMessage(self->_chat_id, msg, "Markdown");
                vTaskDelay(pdMS_TO_TICKS(500));   // evitar flood
            }
        }

        String* msg_ptr = nullptr;
        while (xQueueReceive(self->_message_queue, &msg_ptr, 0) == pdTRUE) {
            if (msg_ptr != nullptr) {
                if (WiFi.status() == WL_CONNECTED && time(nullptr) > 1700000000UL) {
                    bot.sendMessage(self->_chat_id, *msg_ptr, "Markdown");
                    vTaskDelay(pdMS_TO_TICKS(500));   // evitar flood
                }
                delete msg_ptr; // Liberamos la memoria del String dinámico
            }
        }

        // ── Polling de mensajes entrantes cada 1 segundo ──────────────────
        if (millis() - last_poll >= 1000 &&
            WiFi.status() == WL_CONNECTED &&
            time(nullptr) > 1700000000UL) {
            last_poll = millis();

            int n = bot.getUpdates(bot.last_message_received + 1);
            while (n > 0) {
                for (int i = 0; i < n; i++) {
                    String chat_id = bot.messages[i].chat_id;
                    String text    = bot.messages[i].text;
                    DBGSERIAL.printf("[Bot] Cmd de %s: %s\n",
                                  chat_id.c_str(), text.c_str());
                    self->handleCommand(text, chat_id);
                }
                n = bot.getUpdates(bot.last_message_received + 1);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── API pública ───────────────────────────────────────────────────────────
void TelegramBot::begin(const char* token, const char* chat_id) {
    setCredentials(token, chat_id);
    _alert_queue = xQueueCreate(16, sizeof(AlertMsg));
    _message_queue = xQueueCreate(16, sizeof(String *));
    xTaskCreatePinnedToCore(task, "telegram_bot",
                             16384,   // 16 KB: UniversalTelegramBot + ArduinoJson
                             this, 1, nullptr, 0);
}

void TelegramBot::setCredentials(const char* token, const char* chat_id) {
    if (strlen(token) >= sizeof(_token))
        DBGSERIAL.printf("[Telegram] token demasiado largo (%u chars, max %u) — truncado\n",
                         strlen(token), sizeof(_token) - 1);
    strncpy(_token,   token,   sizeof(_token)   - 1);
    strncpy(_chat_id, chat_id, sizeof(_chat_id) - 1);
}

bool TelegramBot::enqueueAlert(AlertType type, int32_t value) {
    if (!_alert_queue) return false;
    AlertMsg msg{type, value};
    return xQueueSend(_alert_queue, &msg, 0) == pdTRUE;
}

bool TelegramBot::enqueueMessage(const String& msg) {
    if (!_message_queue) return false;
    
    // Creamos una copia dinámica del String en el Heap
    String* msg_ptr = new String(msg);
    
    // Enviamos el PUNTERO a la cola
    if (xQueueSend(_message_queue, &msg_ptr, 0) != pdTRUE) {
        delete msg_ptr; // Si la cola está llena, liberamos para evitar fugas
        return false;
    }
    return true;
}

void TelegramBot::silence(uint32_t seconds) {
    _silence_until = millis() + seconds * 1000;
}
void TelegramBot::unsilence() { _silence_until = 0; }
bool TelegramBot::isSilenced() const {
    return _silence_until > 0 && millis() < _silence_until;
}
bool TelegramBot::isConfigured() const {
    return _token[0] != '\0' && _chat_id[0] != '\0';
}
