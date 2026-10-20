#pragma once

// Overlay a pantalla completa «Actualizando firmware». Llamar SOLO desde loop() (Core 1):
// LVGL no es thread-safe.
void ota_overlay_show();
// Retira el overlay si la actualización falla (no-op si no está visible)
void ota_overlay_hide();
