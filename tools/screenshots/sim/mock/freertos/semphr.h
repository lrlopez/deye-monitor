#pragma once
typedef void* SemaphoreHandle_t;
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define LOGGER_PORT 8899
#define MODBUS_UNIT_ID 1
typedef void (*TaskFunction_t)(void*);
extern int g_scan_mode;
static inline int xTaskCreatePinnedToCore(TaskFunction_t f, const char*, int, void*, int, void*, int) { f(nullptr); return 1; }
static inline void vTaskDelete(void*) {}
#define pdTRUE 1
#define portMAX_DELAY 0xFFFFFFFF
static inline SemaphoreHandle_t xSemaphoreCreateMutex() { return (SemaphoreHandle_t)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) { return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
