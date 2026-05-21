#pragma once
#include <esp_heap_caps.h>
#include <Arduino.h>

// ── API interna de LVGL 9 (LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM) ──────
// LVGL llama directamente a estos tres símbolos.
// Deben estar en un .cpp para que el linker los encuentre.

// Declaraciones (la definición va en psram_alloc.cpp)
#ifdef __cplusplus
extern "C" {
#endif

void  lv_mem_init(void);
void  lv_mem_deinit(void);
void* lv_malloc_core(size_t size);
void  lv_free_core(void* ptr);
void* lv_realloc_core(void* ptr, size_t new_size);

#ifdef __cplusplus
}
#endif

// ── Helpers propios del proyecto (pueden llamarse desde C++) ──────────────
inline void* psram_malloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
inline void* psram_realloc(void* ptr, size_t size) {
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
inline void psram_free(void* ptr) {
    heap_caps_free(ptr);
}

inline void print_mem_stats(const char* label) {
    DBGSERIAL.printf("[MEM] %s — SRAM libre: %lu KB (bloque máx: %lu KB) | "
                  "PSRAM libre: %lu KB\n",
                  label,
                  (unsigned long)ESP.getFreeHeap()     / 1024,
                  (unsigned long)ESP.getMaxAllocHeap() / 1024,
                  (unsigned long)ESP.getFreePsram()    / 1024);
}