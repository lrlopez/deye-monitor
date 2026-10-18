#!/bin/sh
# Regenera las capturas de la web del manual (docs/manual/img/web-*).
#
# Extrae del propio firmware (src/web_server.cpp) las páginas HTML reales, las sirve con un servidor
# de datos de ejemplo (web/server.py, la misma API que el equipo) y las fotografía con Chrome
# headless. Requiere Python 3 con Pillow y Google Chrome / Chromium.
#
# uso: tools/screenshots/capture-web.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd); P=$(cd "$HERE/../.." && pwd); IMG=$P/docs/manual/img
WEB=$HERE/web; PORT=8766
CHROME=${CHROME:-$(for c in "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
  "$(command -v google-chrome)" "$(command -v chromium)" "$(command -v chromium-browser)"; do [ -x "$c" ] && echo "$c" && break; done)}
[ -n "$CHROME" ] || { echo "No encuentro Chrome/Chromium (variable CHROME)"; exit 1; }
mkdir -p "$IMG"

python3 -I "$WEB/gen.py" >/dev/null
trap 'kill $PID 2>/dev/null' EXIT

shot() {  # ruta fichero ancho alto
  "$CHROME" --headless=new --disable-gpu --hide-scrollbars --window-size=$3,$4 --virtual-time-budget=20000 \
    --screenshot="$IMG/$2.png" "http://127.0.0.1:$PORT$1" >/dev/null 2>&1 && echo "  $2"
}
crop() {  # fichero izquierda arriba derecha abajo
  python3 -I -c "
from PIL import Image
p='$IMG/$1.png'; im=Image.open(p); im.crop(($2,$3,$4,$5)).save(p)"
}

echo "Capturas de la web:"
python3 -I "$WEB/server.py" & PID=$!; sleep 1
shot / web-dashboard 1100 660
shot /chart web-grafica 1100 820
shot /update web-actualizacion 800 1000;  crop web-actualizacion 0 110 800 920
kill $PID; wait $PID 2>/dev/null || true
FW_STATE=available python3 -I "$WEB/server.py" & PID=$!; sleep 1
shot /update web-actualizacion-disponible 800 1000; crop web-actualizacion-disponible 0 110 800 960
echo "Hecho: $IMG"
