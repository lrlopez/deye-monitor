#!/bin/sh
# Compila un fichero .c de LVGL a obj/ (lo usa capture-device.sh). Variable de entorno: LVGL_DIR.
S=$(cd "$(dirname "$0")" && pwd)
o=$S/obj/$(echo "$1" | shasum | cut -c1-12).o
clang -O1 -w -c -DLV_CONF_INCLUDE_SIMPLE -I"$S/mock" -I"$LVGL_DIR" "$1" -o "$o" 2>>"$S/obj/build_lv.log" || echo "FAIL $1" >> "$S/obj/build_fail.txt"
