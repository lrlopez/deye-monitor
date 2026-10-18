Import("env")
import os

# Retoques mínimos a «GFX Library for Arduino» para las ESP32-P4 (Guition):
#   - Arduino_ESP32DSIPanel.h:   getPanelHandle() para volcar con esp_lcd_panel_draw_bitmap
#   - Arduino_ESP32DSIPanel.cpp: num_fbs = 2 (doble framebuffer DMA2D, sin tearing)
#
# Se aplican como sustituciones de texto sobre la librería instalada (no se copian ficheros
# enteros), así se conserva el resto de cambios de cada versión de la librería. Si una
# sustitución no encaja (la librería cambió) la compilación se detiene, en lugar de seguir
# con un firmware sin el doble buffer.

PATCHES = {
    "Arduino_ESP32DSIPanel.h": [
        ("  uint16_t *getFrameBuffer();\n",
         "  uint16_t *getFrameBuffer();\n"
         "  esp_lcd_panel_handle_t getPanelHandle() const { return _panel_handle; }\n"),
    ],
    "Arduino_ESP32DSIPanel.cpp": [
        (".num_fbs = 1,", ".num_fbs = 2,"),
    ],
}

gfx_dir = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),
                       "GFX Library for Arduino", "src", "databus")

if not os.path.isdir(gfx_dir):
    print(f"[patch_gfx_p4] AVISO: {gfx_dir} no existe — ¿librería no instalada aún?")
else:
    for fname, subs in PATCHES.items():
        path = os.path.join(gfx_dir, fname)
        with open(path, encoding="utf-8") as f:
            text = f.read()
        changed = False
        for old, new in subs:
            if new in text:
                continue                      # ya aplicado
            if old not in text:
                raise SystemExit(f"[patch_gfx_p4] ERROR: no encuentro {old.strip()!r} en {fname}; "
                                 "la versión de GFX Library ha cambiado, revisar scripts/patch_gfx_p4.py")
            text = text.replace(old, new, 1)
            changed = True
        if changed:
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            print(f"[patch_gfx_p4] Aplicado: {fname}")
