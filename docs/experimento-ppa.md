# Experimento: aceleración PPA del ESP32-P4 (rama `exp/ppa-bench`)

Estado: **en pausa** hasta disponer de una Guition JC4880P443C. No está pensado para fusionarse tal cual.

## Pregunta original

¿Puede LVGL usar el PPA (Pixel-Processing Accelerator) del ESP32-P4 para acelerar el render y,
de paso, evitar la dependencia de GFX Library for Arduino?
Documentación de LVGL: https://lvgl.io/docs/open/integration/chip_vendors/espressif/hardware_accelerator_ppa

## Qué contiene la rama

- `platformio.ini`: dos entornos de banco de pruebas, ambos con `ci_skip = true` (CI los ignora):
  - `guition_jc1060p470_bench`: igual que producción + medición (`-DPPA_BENCH`).
  - `guition_jc1060p470_ppa`: lo anterior + unidad de dibujo PPA de LVGL
    (`-DLV_USE_PPA=1 -DLV_DRAW_BUF_ALIGN=128 -DCONFIG_LV_DRAW_BUF_ALIGN=128 -include ppa_cache_shim.h`).
- `include/lv_conf.h`: `LV_USE_PPA`, `LV_USE_PPA_IMG` y `LV_DRAW_BUF_ALIGN` pasan a poder fijarse con `-D`
  (valores por defecto idénticos a antes).
- `src/main.cpp`:
  - Con `PPA_BENCH`: mide render (`LV_EVENT_RENDER_START`→`RENDER_READY`) y flush por fotograma, imprime
    `[BENCH] ppa=N frames=… render avg/max … | flush avg/max …` cada 5 s y repinta la pantalla entera cada 100 ms
    para tener carga constante.
  - En las P4 el buffer de LVGL se reserva con `heap_caps_aligned_alloc(128, …)` (línea de caché L2 del P4);
    es inocuo sin PPA.
- `include/ppa_cache_shim.h` + `src/ppa_cache_shim.cpp`: definen `esp_cache_get_line_size_by_addr()`, que usa
  LVGL 9.5 (`lv_draw_ppa_buf.c`) pero no existe en el ESP-IDF 5.5 de nuestra plataforma. Solo se compila con `LV_USE_PPA`.

## Cómo reproducir

`pio` no está en el PATH: `~/.platformio/penv/bin/pio`.

```
pio run -e guition_jc1060p470_bench -t upload -t monitor    # referencia
pio run -e guition_jc1060p470_ppa   -t upload -t monitor    # con PPA
```

Dejar ~15 s en cada pantalla (dashboard, gráfica, perfil de energía, config) y comparar las líneas `[BENCH]`.
Ignorar los primeros 3–4 informes (arranque: WiFi/NTP/caché compiten por la CPU).

## Resultados (JC1060P470, 1024×600, modo FULL, régimen estable, 2026-11-08)

| | Render LVGL | Flush | Máx. render |
|---|---|---|---|
| `ppa=0` | ~52,0 ms | ~0,67 ms | ~55 ms |
| `ppa=1` | ~47,0 ms | ~0,67 ms | ~50 ms |

- El PPA ahorra ~5 ms (≈10 %), lejos del ~30 % que anuncia LVGL: la UI es sobre todo texto, arcos y donuts,
  que siguen dibujándose por software.
- El flush (DMA2D directo, rotation=0) cuesta ~0,7 ms: no es el cuello de botella; lo es el render por software.
- Pendiente de anotar: pantalla concreta medida (probablemente dashboard) y si la imagen con PPA salió sin artefactos.

## Hallazgos técnicos

- LVGL resuelve `^9.2` a **9.5.0**, que incluye `src/draw/espressif/ppa/`. La plataforma trae `libesp_driver_ppa.a`
  y `driver/ppa.h`.
- `lv_draw_ppa_private.h` exige `CONFIG_LV_DRAW_BUF_ALIGN == CONFIG_CACHE_L2_CACHE_LINE_SIZE` (128 en el P4); el
  `sdkconfig.h` precompilado de Arduino no define el primero → se pasa con `-D`.
- La unidad PPA es **experimental**, solo ayuda con buffer de pantalla completa (no en modo parcial) y la mezcla de
  imágenes (`LV_USE_PPA_IMG`) no aporta según la documentación; se deja a 0.
- La rotación por PPA que menciona la documentación **no es de LVGL**: está en el componente `esp_lvgl_port` de
  Espressif. Para usarla aquí habría que llamar a `ppa_do_scale_rotate_mirror()` (`driver/ppa.h`) desde el flush.

## Sobre quitar GFX Library

No se consigue del todo: la Sunton (RGB paralelo) la sigue necesitando. En las P4 habría que crear el panel con
`esp_lcd_new_panel_dpi` y reproducir a mano la secuencia de init del ST7701/JD9165 (hoy en la GFX vendorizada);
`esp_lcd_st7701` es del IDF Component Manager y no entra en Arduino/PlatformIO sin más. Los timings DSI verificados de la
JC4880P443C (`vsync_front_porch=166`, `lane_bit_rate=500`, RST=GPIO5) habría que revalidarlos en hardware.

## Qué hacer al retomarlo (con la JC4880P443C)

1. Medir el flush actual de la JC4880P443C (`gfx->draw16bitRGBBitmap()` con rotation=1, el camino lento de
   `my_disp_flush`). Crear entornos `_bench`/`_ppa` análogos para esa placa (copiar los de la 1060).
2. Si el flush es significativo, probar la rotación por PPA: renderizar LVGL en horizontal (800×480) y rotar con
   `ppa_do_scale_rotate_mirror()` (SRM, rotación 90°) al framebuffer nativo 480×800 en lugar de `draw16bitRGBBitmap`.
   Respetar alineación de 128 B y sincronizar caché (`esp_cache_msync`). Comprobar la imagen: stride nativo 480 px.
3. Valorar `LV_USE_PPA` solo si compensa (hoy ≈10 % en la 1060, experimental).
4. Ideas baratas por probar en cualquier P4, que pueden rendir más que el PPA (el render es lo que pesa):
   - `-O2`/`-O3` para LVGL en vez de `-Os`.
   - `LV_DRAW_SW_DRAW_UNIT_CNT=2` (requiere `LV_USE_OS`; cuidar el reparto de núcleos con las tareas de red).
   - Render parcial en vez de `FULL` (delicado con el doble framebuffer DSI sin tearing).
5. Cualquier cambio que se lleve a `main` debe compilar las tres placas y no alterar el comportamiento por defecto.
