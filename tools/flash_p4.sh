#!/bin/bash
# Graba por USB el firmware COMPLETO (cargador de arranque, tabla de particiones y aplicación) en una placa
# ESP32-P4. Hace falta en la primera instalación (los .bin de las Releases son solo la aplicación) y para las placas
# con chip ESP32-P4 rev v3.x (entorno guition_jc1060p470_r3): `pio run -t upload` falla con el esptool 5.2 que trae
# PlatformIO (el stub se cuelga con "Guru Meditation Error"; FAQ de Espressif:
# https://github.com/espressif/esp-faq/blob/master/docs/en/development-environment/firmware-update.rst).
# Hace falta esptool >= 5.3.dev3, en un entorno virtual:
#   python3 -m venv ~/venv-esptool && ~/venv-esptool/bin/pip install esptool==5.3.dev3
#
# Uso (compilar antes con:  pio run -e <entorno>):
#   ESPTOOL=~/venv-esptool/bin/esptool tools/flash_p4.sh [entorno] [puerto]
# El entorno por defecto es guition_jc1060p470_r3. Los desplazamientos son los de partitions.csv (otadata en 0xe000,
# app0 en 0x10000); basado en tools/flash.sh de esp32-chess. Sin probar con esta aplicación en una placa real.
set -e
cd "$(dirname "$0")/.."
ESPTOOL=${ESPTOOL:-esptool}
ENVN=${1:-guition_jc1060p470_r3}
PORT=${2:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}
B=.pio/build/$ENVN
BOOT_APP0=$(ls ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin)
[ -f "$B/firmware.bin" ] || { echo "No existe $B/firmware.bin: compila antes con  pio run -e $ENVN" >&2; exit 1; }
[ -n "$PORT" ] || { echo "No encuentro el puerto USB de la placa: indícalo como segundo argumento" >&2; exit 1; }
"$ESPTOOL" --chip esp32p4 --port "$PORT" --baud 921600 write-flash -z --flash-mode dio --flash-freq 80m --flash-size detect \
  0x2000 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$B/firmware.bin"
