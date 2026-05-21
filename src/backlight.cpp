#include "backlight.h"
#include <Arduino.h>
#include <time.h>

BacklightManager& BacklightManager::instance() {
    static BacklightManager b;
    return b;
}

// ── PWM ───────────────────────────────────────────────────────────────────
static constexpr uint32_t PWM_FREQ = 10000;   // 10 kHz
static constexpr uint8_t  PWM_BITS = 8;       // 0–255
static constexpr uint8_t  BL_CHANNEL = 7;   // canales 0-5 suelen estar ocupados

uint8_t BacklightManager::_pct_to_duty(uint8_t pct) {
    if (pct == 0)   return 0;
    if (pct >= 100) return 255;
    return (uint16_t)pct * 255 / 100;
}

void BacklightManager::_apply_immediate(uint8_t duty) {
    _current_duty = duty;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(_pin, duty);
#else
    ledcWrite(BL_CHANNEL, duty);
#endif
}

// Transición suave: ±8 duty cada tick (~5 ms) → rango completo en ~160 ms
void BacklightManager::_apply_smooth(uint8_t target) {
    if (_current_duty == target) return;
    int delta = (int)target - (int)_current_duty;
    int step  = delta > 0 ? min(delta, 8) : max(delta, -8);
    _current_duty = (uint8_t)(_current_duty + step);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(_pin, _current_duty);
#else
    ledcWrite(BL_CHANNEL, _current_duty);
#endif
}

// ── Horario nocturno con wrap-around ──────────────────────────────────────
bool BacklightManager::_in_night_schedule() const {
    struct tm t; getLocalTime(&t, 0);
    int h = t.tm_hour;
    int s = (int)_cfg.night_start_h;
    int e = (int)_cfg.night_end_h;
    if (s == e) return false;          // rango vacío = desactivado
    if (s < e)  return h >= s && h < e;
    return h >= s || h < e;            // wrap-around: ej. 22h→06h
}

bool BacklightManager::_inactivity_exceeded() const {
    uint32_t timeout_ms = (uint32_t)_cfg.inactivity_div10 * 10 * 1000;
    return millis() - _last_touch >= timeout_ms;
}

// ── API pública ───────────────────────────────────────────────────────────
void BacklightManager::begin(uint8_t pin) {
    _pin = pin;
    _cfg = Storage.loadBacklightConfig();

    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    // arduino-esp32 3.x — API unificada
    ledcAttach(pin, PWM_FREQ, PWM_BITS);
    #else
        // arduino-esp32 2.x — API con canal explícito
        ledcSetup(BL_CHANNEL, PWM_FREQ, PWM_BITS);
        ledcAttachPin(pin, BL_CHANNEL);
    #endif

    uint8_t initial = _pct_to_duty(_cfg.normal_pct);
    _apply_immediate(initial);
    _last_touch = millis();

    DBGSERIAL.printf("[BL] Iniciado — normal:%d%% reducido:%d%% "
                  "inact:%ds noche:%02dh-%02dh\n",
                  _cfg.normal_pct, _cfg.reduced_pct,
                  _cfg.inactivity_div10 * 10,
                  _cfg.night_start_h, _cfg.night_end_h);
}

void BacklightManager::setConfig(const BacklightConfig& cfg) {
    _cfg = cfg;
    Storage.saveBacklightConfig(cfg);
    // Aplicar inmediatamente
    _last_touch = millis();
}

void BacklightManager::onTouch() {
    _last_touch = millis();
}

void BacklightManager::tick() {
    // Limitar a ~60 Hz para no saturar ledcWrite
    uint32_t now = millis();
    if (now - _last_tick < 16) return;
    _last_tick = now;

    bool dim = false;
    if (_cfg.night_enabled && _in_night_schedule())    dim = true;
    if (_cfg.inactivity_enabled)                       dim = _inactivity_exceeded();

    uint8_t target = _pct_to_duty(dim ? _cfg.reduced_pct : _cfg.normal_pct);
    _apply_smooth(target);
}
