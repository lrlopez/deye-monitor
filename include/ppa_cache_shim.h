// Adaptador de banco de pruebas PPA: LVGL 9.5 (lv_draw_ppa_buf.c) llama a
// esp_cache_get_line_size_by_addr(), que no existe en el ESP-IDF 5.5 de nuestra plataforma.
// Se inyecta con -include en los entornos *_ppa; la definición está en src/ppa_cache_shim.cpp.
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t esp_cache_get_line_size_by_addr(const void *addr);
#ifdef __cplusplus
}
#endif
