#include "ppa_rotate.h"

#ifdef DEYE_PPA_ROTATE

#include <string.h>
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "safe_serial.h"

static const size_t PPA_LINE = 128;   // línea de caché L2 del P4 (= CONFIG_LV_DRAW_BUF_ALIGN)

static ppa_client_handle_t s_client = nullptr;
static uint16_t *s_dst = nullptr;
static uint32_t s_w = 0, s_h = 0;                       // entrada: s_w × s_h; salida: s_h × s_w
static size_t s_bytes = 0;
static ppa_srm_rotation_angle_t s_angle = PPA_SRM_ROTATION_ANGLE_270;
static uint32_t s_errors = 0;

// Una pasada del PPA (bloqueante): src (s_w×s_h) → s_dst (s_h×s_w), RGB565 sin conversión.
static esp_err_t srm_run(const uint16_t *src, ppa_srm_rotation_angle_t angle)
{
    ppa_srm_oper_config_t c = {};
    c.in.buffer = src;
    c.in.pic_w = s_w;       c.in.pic_h = s_h;
    c.in.block_w = s_w;     c.in.block_h = s_h;
    c.in.block_offset_x = 0; c.in.block_offset_y = 0;
    c.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    c.out.buffer = s_dst;
    c.out.buffer_size = (uint32_t)s_bytes;
    c.out.pic_w = s_h;      c.out.pic_h = s_w;
    c.out.block_offset_x = 0; c.out.block_offset_y = 0;
    c.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    c.rotation_angle = angle;
    c.scale_x = 1.0f;       c.scale_y = 1.0f;           // sin esto la escala queda a 0
    c.mirror_x = false;     c.mirror_y = false;
    c.rgb_swap = false;     c.byte_swap = false;
    c.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    c.mode = PPA_TRANS_MODE_BLOCKING;
    return ppa_do_scale_rotate_mirror(s_client, &c);
}

static inline uint16_t pattern(uint32_t x, uint32_t y)
{
    return (uint16_t)(x * 0x9E37u + y * 0x79B9u + ((x ^ y) * 3u) + 1u);
}

// Compara s_dst con lo que haría GFX rotation=1: fb[x * H + (H-1-y)] = pixel(x, y).
// Devuelve el número de píxeles distintos (0 = idéntico) y apunta el primero en *first.
static uint32_t count_mismatch(uint32_t *first_x, uint32_t *first_y, uint16_t *got)
{
    uint32_t bad = 0;
    for (uint32_t y = 0; y < s_h; y++) {
        for (uint32_t x = 0; x < s_w; x++) {
            uint16_t g = s_dst[x * s_h + (s_h - 1 - y)];
            if (g != pattern(x, y)) {
                if (!bad) { *first_x = x; *first_y = y; *got = g; }
                bad++;
            }
        }
    }
    return bad;
}

static void release()
{
    if (s_client) { ppa_unregister_client(s_client); s_client = nullptr; }
    if (s_dst)    { heap_caps_free(s_dst); s_dst = nullptr; }
}

bool ppa_rotate_init(uint16_t *scratch, uint32_t src_w, uint32_t src_h)
{
    s_w = src_w; s_h = src_h;
    s_bytes = (size_t)src_w * src_h * sizeof(uint16_t);

    if (!scratch || ((uintptr_t)scratch % PPA_LINE) != 0 || (s_bytes % PPA_LINE) != 0) {
        APP_LOGW("PPA", "rotación hardware desactivada: búfer de render sin alinear a %u B", (unsigned)PPA_LINE);
        return false;
    }

    s_dst = (uint16_t *)heap_caps_aligned_alloc(PPA_LINE, s_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_dst) {
        APP_LOGW("PPA", "rotación hardware desactivada: sin PSRAM para el búfer de %u KB", (unsigned)(s_bytes / 1024));
        return false;
    }

    ppa_client_config_t cc = {};
    cc.oper_type = PPA_OPERATION_SRM;
    cc.max_pending_trans_num = 1;
    cc.data_burst_length = PPA_DATA_BURST_LENGTH_128;
    esp_err_t e = ppa_register_client(&cc, &s_client);
    if (e != ESP_OK) {
        APP_LOGW("PPA", "rotación hardware desactivada: ppa_register_client falló (%d)", (int)e);
        s_client = nullptr;
        release();
        return false;
    }

    // Prueba de extremo a extremo con el tamaño real: se escribe un patrón en el búfer de LVGL
    // (aún sin usar), se gira con el PPA y se compara con el mapeo de GFX. Se prueban 270° y 90°
    // (el enum del PPA cuenta en sentido antihorario; GFX rotation=1 es horario = 270°) y se
    // queda con el que coincide exactamente. Así queda comprobado también el orden de bytes y la
    // sincronización de caché.
    for (uint32_t y = 0; y < s_h; y++)
        for (uint32_t x = 0; x < s_w; x++)
            scratch[y * s_w + x] = pattern(x, y);

    const ppa_srm_rotation_angle_t candidates[2] = { PPA_SRM_ROTATION_ANGLE_270, PPA_SRM_ROTATION_ANGLE_90 };
    for (int i = 0; i < 2; i++) {
        memset(s_dst, 0xA5, s_bytes);
        if (esp_cache_msync(scratch, s_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA) != ESP_OK ||
            esp_cache_msync(s_dst, s_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE | ESP_CACHE_MSYNC_FLAG_TYPE_DATA) != ESP_OK) {
            APP_LOGW("PPA", "rotación hardware desactivada: esp_cache_msync falló");
            release();
            return false;
        }

        int64_t t0 = esp_timer_get_time();
        e = srm_run(scratch, candidates[i]);
        int64_t dt = esp_timer_get_time() - t0;
        if (e != ESP_OK) {
            APP_LOGW("PPA", "rotación hardware desactivada: ppa_do_scale_rotate_mirror falló (%d)", (int)e);
            release();
            return false;
        }
        esp_cache_msync(s_dst, s_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

        uint32_t fx = 0, fy = 0; uint16_t got = 0;
        uint32_t bad = count_mismatch(&fx, &fy, &got);
        if (bad == 0) {
            s_angle = candidates[i];
            APP_LOGI("PPA", "rotación hardware OK: %ux%u -> %ux%u, ángulo %s, %u us por fotograma",
                     (unsigned)s_w, (unsigned)s_h, (unsigned)s_h, (unsigned)s_w,
                     i == 0 ? "270 (horario)" : "90", (unsigned)dt);
            return true;
        }
        APP_LOGW("PPA", "prueba de giro %s: %u px distintos (primero en %u,%u: 0x%04X, esperado 0x%04X)",
                 i == 0 ? "270" : "90", (unsigned)bad, (unsigned)fx, (unsigned)fy,
                 (unsigned)got, (unsigned)pattern(fx, fy));
    }

    APP_LOGW("PPA", "rotación hardware desactivada: ningún giro coincide con GFX; se usa la rotación por software");
    release();
    return false;
}

const uint16_t *ppa_rotate_frame(const uint16_t *src)
{
    if (!s_client || !s_dst) return nullptr;
    // El render de LVGL deja el fotograma en la caché: escribirlo a PSRAM antes de que el PPA lo lea.
    esp_err_t e = esp_cache_msync((void *)src, s_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
    if (e == ESP_OK) e = srm_run(src, s_angle);
    if (e != ESP_OK) {
        if ((s_errors++ & 0x3F) == 0)
            APP_LOGE("PPA", "fallo al girar el fotograma (%d), %u en total", (int)e, (unsigned)s_errors);
        return nullptr;
    }
    return s_dst;   // lo lee el DMA2D de esp_lcd_panel_draw_bitmap(): la CPU no lo toca
}

#endif  // DEYE_PPA_ROTATE
