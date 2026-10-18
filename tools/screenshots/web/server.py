"""Servidor de datos de ejemplo para capturar la web del firmware (misma API que web_server.cpp)."""
import json, math, os, time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT = os.path.dirname(os.path.abspath(__file__)) + "/site"
NOW = datetime.now()
FW = {"state": os.environ.get("FW_STATE", "idle"), "latest": "v3.7.0" if os.environ.get("FW_STATE") == "available" else ""}

def records():
    midnight = datetime(NOW.year, NOW.month, NOW.day).timestamp()
    out = []
    for slot in range(0, 21 * 12):   # el día de ejemplo llega hasta las 21:00, con independencia de la hora real
        h = slot * 5 / 60.0
        sun = max(0.0, math.sin((h - 7.5) * math.pi / 12.5)) if 7.5 <= h <= 20 else 0.0
        pv = int(4200 * sun * (0.92 + 0.08 * math.sin(slot * 0.9)))
        load = int(450 + 380 * (1 if 18 < h < 23 else 0.25) + 160 * math.sin(slot * 0.31) + 250 * max(0, math.sin(slot * 0.07)))
        batt = int(-min(1600, max(0, pv - load)) * 0.7) if pv > load else (int(min(1100, load * 0.7)) if h > 17.5 else 0)
        grid = load - pv - batt
        soc = int(35 if h < 8 else (35 + (h - 8) * 6.5 if h < 17 else 94 - (h - 17) * 14))
        out.append({"t": int(midnight + slot * 300), "pv": pv, "grid": grid, "batt": batt, "load": max(load, 0), "soc": soc})
    return out

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body, ct="application/json"):
        b = body if isinstance(body, bytes) else body.encode()
        self.send_response(code); self.send_header("Content-Type", ct); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        u = urlparse(self.path); q = parse_qs(u.query)
        if u.path in ("/", "/chart", "/update"):
            f = {"/": "index.html", "/chart": "chart.html", "/update": "update.html"}[u.path]
            self.send(200, open(ROOT + "/" + f, "rb").read(), "text/html; charset=utf-8")
        elif u.path.startswith("/assets/"):
            self.send(200, open(ROOT + u.path, "rb").read(), "application/javascript")
        elif u.path == "/api/data":
            self.send(200, json.dumps({"live": {"pv_w": 3180, "pv1_w": 1920, "pv2_w": 1260, "grid_w": -1100, "batt_w": -870,
                "batt_soc": 78, "load_w": 1210, "batt_eta_mode": 2, "batt_eta_min": 130, "batt_v": 52.4, "temp_c": 41.5},
                "daily": {"pv_kwh": 9.8, "export_kwh": 3.0, "import_kwh": 2.1, "load_kwh": 8.4, "batt_charge_kwh": 1.9,
                "batt_discharge_kwh": 2.2, "valid": True}}))
        elif u.path == "/api/status":
            self.send(200, json.dumps({"version": "3.7.0", "uptime_s": 273000, "heap_free_kb": 142, "heap_min_kb": 96,
                "psram_free_kb": 6240, "fs_total_kb": 8192, "fs_used_kb": 3921, "wifi_rssi": -57, "ip": "192.168.1.77"}))
        elif u.path == "/api/alerts":
            self.send(200, json.dumps({"alerts": [], "last_id": 0}))
        elif u.path == "/api/latest_date":
            self.send(200, json.dumps({"date": NOW.strftime("%Y%m%d")}))
        elif u.path == "/api/history":
            r = records()
            self.send(200, json.dumps({"date": NOW.strftime("%Y-%m-%d"), "granularity": "5min", "from_ts": 0, "new_records": len(r),
                "daily": {"pv": 9.8, "exp": 3.0, "imp": 2.1, "load": 8.4, "bchg": 1.9, "bdis": 2.2}, "records": r}))
        elif u.path == "/api/fw/status":
            self.send(200, json.dumps({"state": FW["state"], "env": "guition_jc4880p443c", "current": "3.6.0", "latest": FW["latest"],
                "notes_url": "", "asset": "deye_monitor_guition_jc4880p443c_v3.7.0.bin" if FW["latest"] else "",
                "size": 1869584 if FW["latest"] else 0, "done": 0, "has_digest": True, "error": ""}))
        else:
            self.send(404, "no")
    def do_POST(self):
        if self.path == "/api/fw/check":
            FW.update(state="available", latest="v3.7.0")
            self.do_GET()
        else:
            self.send(404, "no")

ThreadingHTTPServer(("127.0.0.1", 8766), H).serve_forever()
