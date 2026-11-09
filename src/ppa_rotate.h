// Rotación de 90° del fotograma de LVGL por hardware (PPA del ESP32-P4), solo para la
// Guition JC4880P443C: panel nativo vertical 480×800 mostrado en horizontal (800×480 lógicos).
//
// Antes el flush de esa placa pasaba por gfx->draw16bitRGBBitmap() (rotación por software,
// píxel a píxel). Ahora LVGL renderiza en horizontal, el PPA gira el fotograma al orden
// nativo del panel en un segundo búfer y ese búfer se entrega a esp_lcd_panel_draw_bitmap()
// (DMA2D + intercambio en vsync), igual que en la JC1060P470.
//
// ppa_rotate_init() VERIFICA el giro al arrancar comparando, píxel a píxel, la salida del PPA
// con el mapeo que hacía GFX (rotation=1). Si algo falla (registro del cliente, memoria,
// sincronización de caché o resultado distinto) devuelve false y main.cpp sigue con el
// camino de GFX de siempre. La decisión se toma UNA vez al arrancar; no se alterna por fotograma
// (tras draw_bitmap el panel muestra otro framebuffer y mezclar ambas vías congelaría la imagen).
//
// -DDEYE_NO_PPA_ROTATE desactiva el módulo (queda el camino de GFX).
#pragma once

#if defined(BOARD_GUITION_JC4880P443C) && defined(LV_USE_PPA) && LV_USE_PPA && !defined(DEYE_NO_PPA_ROTATE)
#define DEYE_PPA_ROTATE 1

#include <stdint.h>

// `scratch` = búfer de render de LVGL (src_w×src_h RGB565, alineado a 128 B). Se usa como entrada
// de la prueba, así que debe llamarse ANTES del primer render. Devuelve true si el PPA queda listo.
bool ppa_rotate_init(uint16_t *scratch, uint32_t src_w, uint32_t src_h);

// Gira un fotograma completo (src_w×src_h) y devuelve el búfer con la imagen en orden nativo
// (src_h de ancho × src_w de alto), válido hasta la siguiente llamada. nullptr si el PPA falla
// en ese fotograma (el llamador simplemente no dibuja ese fotograma).
const uint16_t *ppa_rotate_frame(const uint16_t *src);

#endif
