#include "alert_engine.h"
#include "app_state.h"
#include "telegram.h"
#include "screen_banner.h"
#include "safe_serial.h"
#include <time.h>

// ── Datalogger ────────────────────────────────────────────────────────────
void AlertEngine::onLoggerFail() {
    // 5 fallos consecutivos (~25 s) → notificar una sola vez. Con Telegram sin
    // configurar no se marca como notificado (se reintentará al volver a 5).
    if (_logger.onFailure() && Telegram.isConfigured()) {
        if (Storage.loadTelegramConfig().notify_logger)
            Telegram.enqueueAlert(AlertType::LOGGER_FAIL);
        screen_banner_enqueue(AlertType::LOGGER_FAIL);
        _logger.markNotified();
    }
}

// ── Estado solar ──────────────────────────────────────────────────────────
// Restaurar el estado solar desde NVS comparando la fecha guardada con hoy. Si es
// de otro día, se resetea: la producción solar empieza de cero cada día.
void AlertEngine::restoreSolarState(uint32_t now_ep) {
    if (_solar_restored) return;
    _solar_restored = true;

    SessionState ss{};
    if (!Storage.loadSessionState(ss) || ss.last_record_epoch == 0) return;

    struct tm saved_tm{}, now_tm{};
    time_t ts = (time_t)ss.last_record_epoch, tn = (time_t)now_ep;
    localtime_r(&ts, &saved_tm);
    localtime_r(&tn, &now_tm);
    const bool same_day = (saved_tm.tm_mday == now_tm.tm_mday &&
                           saved_tm.tm_mon  == now_tm.tm_mon  &&
                           saved_tm.tm_year == now_tm.tm_year);
    _solar.active = same_day ? ss.solar_active : false;
    APP_LOGI("Solar", "Estado %s: %s",
             same_day ? "restaurado" : "reseteado (día anterior)",
             _solar.active ? "activo" : "inactivo");
}

// ── Alertas de potencia/batería/red ───────────────────────────────────────
void AlertEngine::evaluate(const EnergyData& e, uint32_t now_ep) {
    if (!Telegram.isConfigured()) return;
    const TelegramConfig cfg = Storage.loadTelegramConfig();   // desde la caché en RAM (DEYE-43)

    // Producción solar. Umbral de inicio: 50 W / parada: 20 W, debounce 3 lecturas (15 s)
    if (cfg.notify_solar) {
        switch (_solar.update(e.pv_power)) {
        case SolarMonitor::Event::START:
            Telegram.enqueueAlert(AlertType::SOLAR_START, (int32_t)e.pv_power);
            screen_banner_enqueue(AlertType::SOLAR_START, (int32_t)e.pv_power);
            Storage.saveSessionState({now_ep, true, true});
            break;
        case SolarMonitor::Event::STOP:
            Telegram.enqueueAlert(AlertType::SOLAR_STOP);
            screen_banner_enqueue(AlertType::SOLAR_STOP);
            Storage.saveSessionState({now_ep, true, false});
            break;
        default: break;
        }
    }

    // Batería. Dos niveles: aviso (batt_warn) y crítico (batt_threshold);
    // recuperación con histéresis +5 % sobre el umbral de aviso
    {
        const uint8_t soc = (uint8_t)e.batt_soc;
        switch (_batt.update(soc, cfg.batt_warn, cfg.batt_threshold)) {
        case BatteryMonitor::Event::CRITICAL:
            Telegram.enqueueAlert(AlertType::BATT_CRITICAL, (int32_t)soc);
            screen_banner_enqueue(AlertType::BATT_CRITICAL, (int32_t)soc);
            break;
        case BatteryMonitor::Event::WARN_LOW:
            Telegram.enqueueAlert(AlertType::BATT_LOW, (int32_t)soc);
            screen_banner_enqueue(AlertType::BATT_LOW, (int32_t)soc);
            break;
        case BatteryMonitor::Event::RECOVERED:
            Telegram.enqueueAlert(AlertType::BATT_RECOVERED, (int32_t)soc);
            screen_banner_enqueue(AlertType::BATT_RECOVERED, (int32_t)soc);
            break;
        default: break;
        }
    }

    // Red eléctrica. Corte: |grid_power| = 0 W durante 12 lecturas (60 s); solo se
    // evalúa tras haber visto red activa al menos una vez
    if (cfg.notify_grid) {
        switch (_grid.update(e.grid_power)) {
        case GridMonitor::Event::OUTAGE:
            Telegram.enqueueAlert(AlertType::GRID_OUTAGE, (int32_t)e.grid_power);
            screen_banner_enqueue(AlertType::GRID_OUTAGE);
            break;
        case GridMonitor::Event::RESTORED:
            Telegram.enqueueAlert(AlertType::GRID_RESTORED);
            screen_banner_enqueue(AlertType::GRID_RESTORED);
            break;
        default: break;
        }
    }
}
