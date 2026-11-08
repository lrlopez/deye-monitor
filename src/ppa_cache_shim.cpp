// Ver include/ppa_cache_shim.h. Solo se compila con LV_USE_PPA (entorno *_ppa).
#include "sdkconfig.h"

#if defined(LV_USE_PPA) && LV_USE_PPA
#include "ppa_cache_shim.h"
#include "esp_memory_utils.h"

// Equivalente a la función de IDF 6.x para el P4: la PSRAM pasa por la caché L2 (línea de
// 128 B); el resto (L2MEM interna) no tiene caché de datos y devuelve 0 = "no cacheada".
extern "C" size_t esp_cache_get_line_size_by_addr(const void *addr)
{
    return esp_ptr_external_ram(addr) ? (size_t)CONFIG_CACHE_L2_CACHE_LINE_SIZE : 0;
}
#endif
