#pragma once
#include <Arduino.h>
#include "storage.h"

class BacklightManager {
public:
    static BacklightManager& instance();

    // Llamar en setup() antes del primer lv_timer_handler()
    // pin: GPIO del backlight (TFT2_BL)
    void begin(uint8_t pin);

    // Actualizar configuración en caliente (sin reiniciar)
    void setConfig(const BacklightConfig& cfg);
    BacklightConfig getConfig() const { return _cfg; }

    // Llamar desde loop() cuando se detecta toque
    void onTouch();

    // Llamar desde loop() cada iteración
    void tick();

    // Forzar brillo inmediato (para preview en config)
    void previewNormal()  { _apply_immediate(_pct_to_duty(_cfg.normal_pct));  }
    void previewReduced() { _apply_immediate(_pct_to_duty(_cfg.reduced_pct)); }

private:
    BacklightManager() = default;

    uint8_t         _pin          = 0;
    BacklightConfig _cfg          = {};
    uint8_t         _current_duty = 0;
    uint32_t        _last_touch   = 0;
    uint32_t        _last_tick    = 0;

    static uint8_t  _pct_to_duty(uint8_t pct);
    bool            _in_night_schedule() const;
    bool            _inactivity_exceeded() const;
    void            _apply_smooth(uint8_t target_duty);
    void            _apply_immediate(uint8_t duty);
};

#define Backlight BacklightManager::instance()
