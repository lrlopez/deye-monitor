#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <stdio.h>

// ── SafeSerial ────────────────────────────────────────────────────────────
// Envoltorio thread-safe sobre cualquier Stream serial (HardwareSerial, HWCDC…).
// Cada llamada a printf() o write() adquiere un mutex, garantizando que la
// salida de una sola llamada no se entrelaza con la de otras tareas.
//
// Uso: llamar a begin(hw, baud) una vez en setup() antes de usar DBGSERIAL.
class SafeSerial : public Print {
public:
    template<typename T>
    void begin(T& hw, unsigned long baud) {
        _hw = &hw;
        hw.begin(baud);
        if (!_mutex) _mutex = xSemaphoreCreateMutex();
    }

    size_t write(uint8_t c) override {
        if (!_hw) return 0;
        _lock(); size_t n = _hw->write(c); _unlock(); return n;
    }

    size_t write(const uint8_t* buf, size_t size) override {
        if (!_hw) return 0;
        _lock(); size_t n = _hw->write(buf, size); _unlock(); return n;
    }

    // Formatea en buffer local y escribe en una única adquisición del mutex
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (!_hw) return 0;
        char buf[256];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n > 0) {
            int wlen = (n < (int)sizeof(buf)) ? n : (int)sizeof(buf) - 1;
            _lock(); _hw->write((const uint8_t*)buf, (size_t)wlen); _unlock();
        }
        return n;
    }

    operator bool() const { return _hw != nullptr; }

private:
    Stream*           _hw   = nullptr;
    SemaphoreHandle_t _mutex = nullptr;
    void _lock()   { if (_mutex) xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)); }
    void _unlock() { if (_mutex) xSemaphoreGive(_mutex); }
};

extern SafeSerial DbgSerial;
