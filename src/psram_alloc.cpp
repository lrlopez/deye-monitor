#include "psram_alloc.h"

// LVGL 9 con LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM requiere
// que estas tres funciones estén definidas con enlace C.
// Todo el heap de LVGL va a PSRAM, liberando SRAM para TLS y draw buffers.

extern "C" {

    void lv_mem_init(void) {
    // PSRAM ya está inicializada por el SDK antes de que llegue aquí.
    // No necesitamos hacer nada; el heap de PSRAM está disponible
    // desde el arranque del ESP32-S3.
    DBGSERIAL.printf("[LVGL] lv_mem_init — PSRAM libre: %lu KB\n",
                  (unsigned long)ESP.getFreePsram() / 1024);
}

void lv_mem_deinit(void) {
    // Nada que liberar; el SDK gestiona la PSRAM.
}

void* lv_malloc_core(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void lv_free_core(void* ptr) {
    heap_caps_free(ptr);
}

void* lv_realloc_core(void* ptr, size_t new_size) {
    return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

} // extern "C"
