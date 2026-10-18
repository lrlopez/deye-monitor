#pragma once
#include "storage.h"
struct BacklightMock { void setConfig(const BacklightConfig&) {} };
extern BacklightMock g_bl;
#define Backlight g_bl
