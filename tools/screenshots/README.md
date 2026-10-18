# Capturas del manual

Las imágenes de `docs/manual/img/` se generan **sin hardware**, con datos de ejemplo:

- `capture-device.sh` — pantalla táctil. Compila las pantallas reales del firmware (LVGL 9, mismas
  fuentes y mismos textos) con clang en el ordenador, las dibuja en un framebuffer y guarda PNG de
  800 × 480. Las escenas (qué pantalla, con qué datos) están en `sim/scenes.inc`; `sim/stubs.cpp`
  sustituye lo que necesita hardware (almacenamiento, WiFi, caché del histórico).
- `capture-web.sh` — web integrada. `web/gen.py` extrae del propio `src/web_server.cpp` las páginas
  HTML reales; `web/server.py` las sirve con la misma API que el equipo y datos de ejemplo, y Chrome
  headless las fotografía.

```bash
pio run -e sunton_4827s043            # una vez, para descargar LVGL y las bibliotecas
pip install pillow
tools/screenshots/capture-device.sh   # pantalla táctil
tools/screenshots/capture-web.sh      # web
```

La primera ejecución de `capture-device.sh` compila LVGL (unos minutos); las siguientes son rápidas.
Usa `capture-device.sh clean` para recompilar desde cero.

## Añadir o cambiar una captura

1. Si es una pantalla del dispositivo: añade (o ajusta) una escena en `sim/scenes.inc` y, si el
   módulo nuevo necesita algo de hardware, un stub en `sim/stubs.cpp`; después añade una línea
   `shot <escena> <nombre-de-fichero>` en `capture-device.sh`.
2. Si es una página web: añade la ruta a `web/gen.py` / `web/server.py` y una línea `shot` en
   `capture-web.sh`.
3. Referencia la imagen desde el capítulo correspondiente (`![texto](img/nombre.png)`).

## Limitaciones

- La administración web (`/admin`) no tiene captura: su HTML se construye con código dinámico en el
  firmware y no se puede extraer como el de las demás páginas.
- Las capturas de pantalla son del **simulador**, no de un equipo real: representan fielmente la
  maquetación y los textos, pero la gráfica de 7 días usa datos sintéticos.
