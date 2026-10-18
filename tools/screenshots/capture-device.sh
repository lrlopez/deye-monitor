#!/bin/sh
# Regenera las capturas de la pantalla táctil del manual (docs/manual/img/pantalla-*, config-*, asistente-*).
#
# No hace falta hardware: compila las pantallas reales del firmware (LVGL 9 + las fuentes del
# proyecto) con clang en el ordenador, las dibuja en un framebuffer con datos de ejemplo y guarda el
# resultado como PNG. Requiere clang, Python 3 con Pillow (pip install pillow) y las bibliotecas de
# PlatformIO descargadas (basta haber compilado una vez:  pio run -e sunton_4827s043).
#
# uso: tools/screenshots/capture-device.sh          # todas las capturas del manual
#      tools/screenshots/capture-device.sh clean    # además, recompila LVGL desde cero
set -e
HERE=$(cd "$(dirname "$0")" && pwd); P=$(cd "$HERE/../.." && pwd)
SIM=$HERE/sim; IMG=$P/docs/manual/img
export LVGL_DIR=${LVGL_DIR:-$P/.pio/libdeps/sunton_4827s043/lvgl}
[ -d "$LVGL_DIR/src" ] || { echo "No encuentro LVGL en $LVGL_DIR: ejecuta antes 'pio run -e sunton_4827s043'"; exit 1; }

[ "$1" = clean ] && rm -rf "$SIM/obj" "$SIM/out"
mkdir -p "$SIM/obj" "$SIM/out" "$IMG"

# 1. LVGL a objetos (solo la primera vez). Se excluyen controladores de plataformas, aceleradores
#    gráficos y bibliotecas opcionales que no hacen falta para dibujar en memoria.
if [ -z "$(ls "$SIM/obj"/*.o 2>/dev/null)" ]; then
  echo "Compilando LVGL (una sola vez)..."
  { find "$LVGL_DIR/src" -name '*.c' | grep -Ev '/(drivers|libs)/|/draw/(vg_lite|nxp|nema_gfx|renesas|eve|espressif|dma2d|opengles|sdl)/|/draw/convert/helium|/draw/sw/blend/(neon|helium|riscv_v)|/osal/(lv_cmsis|lv_freertos|lv_linux|lv_mqx|lv_pthread|lv_rtthread|lv_sdl2|lv_windows|lv_os\.c)|/others/(file_explorer|fragment|translation)|/draw/sw/lv_draw_sw_(helium|neon)'
    echo "$LVGL_DIR/src/libs/bin_decoder/lv_bin_decoder.c"   # decodificador de imágenes binarias (lo pide lv_init)
    ls "$P"/src/fonts/*.c; } \
    | xargs -P 8 -n 1 "$SIM/cc1.sh"      # (las fuentes Montserrat propias del proyecto van incluidas)
  [ -f "$SIM/obj/build_fail.txt" ] && { echo "Fallos al compilar LVGL:"; cat "$SIM/obj/build_fail.txt"; exit 1; }
fi

# 2. Escenas → PNG a 800x480 (Guition JC4880P443C). "escena nombre-de-fichero"
shot() { "$SIM/render.sh" p4s "$1" >/dev/null && cp "$SIM/out/p4s-$1.png" "$IMG/$2.png" && echo "  $2"; }
echo "Capturas de la pantalla:"
shot dashboard pantalla-dashboard;      shot flow_exp pantalla-flujo
shot dash_stale pantalla-datos-obsoletos; shot clock pantalla-reloj
shot stats pantalla-estadisticas;       shot chart pantalla-grafica
shot chart_week pantalla-grafica-semana; shot chart_hidden pantalla-grafica-leyenda
shot profile pantalla-perfil
shot config_menu config-menu;           shot config_inv_bad config-validacion
i=0; for n in wifi inversor grafica red telegram pantalla acceso-web datos; do shot cfgs$i config-$n; i=$((i+1)); done
for n in 0 1 2 3 4 5 7 9 10; do shot wiz$n asistente-$n; done
echo "Hecho: $IMG"
