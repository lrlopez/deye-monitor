#pragma once
#include <lvgl.h>
typedef void (*CalendarSelectCb)(uint32_t);
static inline void calendar_show(uint32_t, uint32_t, CalendarSelectCb) {}
