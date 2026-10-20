#!/bin/sh
# uso: render.sh <placa> <escena> [extra_cpp...]   (placa: sunton | p4s | p4v)
# Compila las pantallas reales del firmware contra LVGL y las renderiza en un framebuffer → out/<placa>-<escena>.png
S=$(cd "$(dirname "$0")" && pwd); P=$(cd "$S/../../.." && pwd)
BOARD=$1; SCENE=$2; shift 2
case $BOARD in
 sunton) D="-DSCREEN_WIDTH=480 -DSCREEN_HEIGHT=270 -DFONT_SMALL_SIZE=12 -DFONT_SMALL=lv_font_montserrat_12 -DFONT_NORMAL_SIZE=14 -DFONT_NORMAL=lv_font_montserrat_14 -DFONT_LARGE_SIZE=28 -DFONT_LARGE=lv_font_montserrat_28 -DFONT_MEDIUM_SIZE=22 -DFONT_MEDIUM=lv_font_montserrat_22";;
 p4v)    D="-DBOARD_GUITION_JC1060P470 -DSCREEN_WIDTH=1024 -DSCREEN_HEIGHT=600 -DFONT_SMALL_SIZE=28 -DFONT_SMALL=lv_font_montserrat_28 -DFONT_NORMAL_SIZE=32 -DFONT_NORMAL=lv_font_montserrat_32 -DFONT_LARGE_SIZE=48 -DFONT_LARGE=lv_font_montserrat_48 -DFONT_MEDIUM_SIZE=42 -DFONT_MEDIUM=lv_font_montserrat_42";;
 p4s)    D="-DBOARD_GUITION_JC4880P443C -DSCREEN_WIDTH=800 -DSCREEN_HEIGHT=480 -DFONT_SMALL_SIZE=22 -DFONT_SMALL=lv_font_montserrat_22 -DFONT_NORMAL_SIZE=28 -DFONT_NORMAL=lv_font_montserrat_28 -DFONT_LARGE_SIZE=42 -DFONT_LARGE=lv_font_montserrat_42 -DFONT_MEDIUM_SIZE=32 -DFONT_MEDIUM=lv_font_montserrat_32";;
 *) echo "placa desconocida: $BOARD"; exit 1;;
esac
LVGL_DIR=${LVGL_DIR:-$P/.pio/libdeps/sunton_4827s043/lvgl}
cd $S; ./prep.sh; mkdir -p out
SRCS="harness.cpp stubs.cpp $P/src/dashboard.cpp $P/src/pagination_dots.cpp $P/src/home_button.cpp $P/src/ui_toast.cpp $P/src/clock_mode.cpp $P/src/ota_overlay.cpp $S/profile_copy.cpp $P/src/setup_wizard.cpp $P/src/logger_scan_ui.cpp $S/stats_copy.cpp $S/chart_copy.cpp $S/config_copy.cpp $*"
rm -f out/preview_$BOARD   # si la compilación falla no debe ejecutarse un binario antiguo
clang++ -std=gnu++17 -O1 -w -DLV_CONF_INCLUDE_SIMPLE -DDBGSERIAL=DbgSerial $D -Imock -I$P/src -I$LVGL_DIR $SRCS obj/*.o -o out/preview_$BOARD 2>&1 | head -30
[ -x out/preview_$BOARD ] || { echo "Error: no se pudo compilar el simulador"; exit 1; }
./out/preview_$BOARD $SCENE out/$BOARD-$SCENE.ppm && python3 -I -c "
from PIL import Image; im=Image.open('out/$BOARD-$SCENE.ppm'); im.save('out/$BOARD-$SCENE.png'); print('ok', im.size)"
