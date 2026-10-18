"""Extrae del firmware las páginas web (literales raw del C++) y genera HTML estático para capturas."""
import re, os
HERE = os.path.dirname(os.path.abspath(__file__))
P = os.path.normpath(HERE + "/../../../src")
OUT = HERE + "/site"
os.makedirs(OUT + "/assets", exist_ok=True)
src = open(P + "/web_server.cpp", encoding="utf-8").read()

def func_body(name):
    m = re.search(r"^static void %s\(\) \{" % name, src, re.M)
    end = re.search(r"^static (void|bool|String|uint32_t) ", src[m.end():], re.M)   # el CSS/JS de los literales tiene "}" en columna 0
    return src[m.start(): m.end() + end.start()]

RAW = re.compile(r'R"=EOF=\((.*?)\)=EOF="', re.S)

def raws(body):
    return [m.group(1) for m in RAW.finditer(body)]

# ── Dashboard ─────────────────────────────────────────────────────────────
b = func_body("handle_root")
parts = raws(b)
# bloques: 0 head, 1 topbar, 2 tarjetas(inicio batería...), 3 cierre svg batería, 4 resto, 5 footer, 6 js
SEG_LEN = 245.05 / 24
GAP = 326.73 - SEG_LEN
grad = ""
for i in range(24):
    t = i / 23.0
    if t <= 0.5:
        u = t * 2
        r = int(231 + (245 - 231) * u + 0.5); g = int(76 + (197 - 76) * u + 0.5); bl = int(60 - (60 - 24) * u + 0.5)
    else:
        u = (t - 0.5) * 2
        r = int(245 - (245 - 46) * u + 0.5); g = int(197 + (204 - 197) * u + 0.5); bl = int(24 + (113 - 24) * u + 0.5)
    grad += ('    <circle cx="60" cy="60" r="52" fill="none" stroke="#%02x%02x%02x" stroke-width="10" '
             'stroke-dasharray="%.2f %.2f" stroke-linecap="butt" transform="rotate(135 60 60)" '
             'stroke-dashoffset="%.2f"/>\n' % (r, g, bl, SEG_LEN, GAP, -i * SEG_LEN))
assert len(parts) == 6, len(parts)
const = "\n<script>const IM=6000,GM=6000,C=326.73,ARC=245.05,HALF=122.52,CO2F=0.150;\n"
html = parts[0] + parts[1] + parts[2] + grad + parts[3] + parts[4] + const + parts[5]
open(OUT + "/index.html", "w", encoding="utf-8").write(html)

# ── Gráfica ───────────────────────────────────────────────────────────────
b = func_body("handle_chart")
parts = raws(b)
open(OUT + "/chart.html", "w", encoding="utf-8").write("".join(parts))

# ── Actualización de firmware ─────────────────────────────────────────────
b = func_body("handle_update_get")
m = re.search(r'OTA_PAGE_HTML\[\] PROGMEM = R"\((.*?)\)";', b, re.S)
open(OUT + "/update.html", "w", encoding="utf-8").write(m.group(1))

# ── Chart.js ──────────────────────────────────────────────────────────────
h = open(P + "/web_assets_chartjs.h", encoding="utf-8").read()
for name, delim in (("chart.js", "CHARTJS444"), ("chart-annotation.js", "CHARTANNO301")):
    mm = re.search(r'R"%s\((.*?)\)%s"' % (delim, delim), h, re.S)
    open(OUT + "/assets/" + name, "w", encoding="utf-8").write(mm.group(1))
print("ok", os.listdir(OUT))
