#pragma once
#include "Arduino.h"
enum class AlertType : uint8_t { TEST = 0 };
struct TelegramMock { void setCredentials(const char*, const char*) {} bool enqueueAlert(AlertType, int32_t = 0) { return true; } };
extern TelegramMock g_tg;
#define Telegram g_tg
