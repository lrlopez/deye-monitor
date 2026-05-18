#include <WebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <time.h>
#include "web_server.h"
#include "data_store.h"
#include "psram_cache.h"

static WebServer          server(80);
static SemaphoreHandle_t  s_mutex   = nullptr;
static const EnergyData*  s_energy  = nullptr;
static const DailyStats*  s_daily   = nullptr;

void webserver_set_data(SemaphoreHandle_t mutex,
                        const EnergyData* energy,
                        const DailyStats* daily) {
    s_mutex  = mutex;
    s_energy = energy;
    s_daily  = daily;
}

// ═════════════════════════════════════════════════════════════════════════
// Ruta GET /   →  Dashboard HTML con auto-refresco
// ═════════════════════════════════════════════════════════════════════════
static void handle_root() {
    String html;
    html.reserve(5120);
    html += R"(<!DOCTYPE html><html lang="es"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Deye Monitor</title>
<style>
  :root{--bg:#0d1117;--card:#161b22;--accent:#4a9eff;--ok:#2ecc71;
        --warn:#f5c518;--err:#e74c3c;--muted:#6e7681;--white:#eaeaea}
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:var(--white);
       font-family:system-ui,sans-serif;padding:16px}
  h1{font-size:1.1rem;color:var(--muted);margin-bottom:16px;text-align:center}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}
  .card{background:var(--card);border:1px solid #21262d;
        border-radius:10px;padding:14px;transition:border-color .3s}
  .card h2{font-size:.7rem;text-transform:uppercase;letter-spacing:.08em;
            color:var(--muted);margin-bottom:8px}
  .val{font-size:1.6rem;font-weight:700;line-height:1.1;
       transition:color .4s}
  .sub{font-size:.75rem;color:var(--muted);margin-top:4px}
  .solar{border-color:#f5c518}
  .grid-card{border-color:#4a9eff}
  .batt{border-color:#2ecc71}
  .load{border-color:#bb6bd9}
  .bar-wrap{background:#21262d;border-radius:6px;height:10px;margin:8px 0}
  .bar{height:10px;border-radius:6px;transition:width .6s ease,background .4s}
  hr{border:none;border-top:1px solid #21262d;margin:20px 0}
  .daily-grid{display:grid;
              grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}
  .daily-card{background:var(--card);border:1px solid #21262d;
              border-radius:8px;padding:10px;text-align:center;
              transition:opacity .4s}
  .daily-card h3{font-size:.65rem;color:var(--muted);
                 margin-bottom:4px;text-transform:uppercase}
  .daily-card .kwh{font-size:1.2rem;font-weight:700;transition:color .4s}
  /* Donut SVG */
  .donut-wrap{display:flex;flex-direction:column;align-items:center;margin:16px 0}
  .donut-row{display:flex;gap:32px;justify-content:center;flex-wrap:wrap}
  .donut-box{display:flex;flex-direction:column;align-items:center;gap:6px}
  .donut-box h3{font-size:.7rem;text-transform:uppercase;
                letter-spacing:.08em;color:var(--muted)}
  .leg{display:grid;grid-template-columns:1fr 1fr;
       gap:2px 12px;margin-top:6px;font-size:.72rem}
  .leg-item{display:flex;align-items:center;gap:5px}
  .leg-dot{width:8px;height:8px;border-radius:2px;flex-shrink:0}
  /* Status bar */
  #status-bar{position:fixed;bottom:0;left:0;right:0;
              background:#161b22;border-top:1px solid #21262d;
              padding:4px 12px;font-size:.7rem;color:var(--muted);
              display:flex;justify-content:space-between}
  #status-dot{width:8px;height:8px;border-radius:50%;
              background:var(--muted);display:inline-block;
              margin-right:5px;transition:background .3s}
  footer{text-align:center;color:var(--muted);
         font-size:.7rem;margin:20px 0 28px}
  a{color:var(--accent);text-decoration:none}
</style></head><body>
<h1>&#9889; Deye Monitor – )";
    html += WiFi.localIP().toString();
    html += R"(</h1>

<!-- Live cards -->
<div class="grid">
  <div class="card solar">
    <h2>&#9728; Solar</h2>
    <div class="val" id="pv-val" style="color:#f5c518">-- W</div>
    <div class="sub" id="pv-sub">PV1: -- W &nbsp; PV2: -- W</div>
  </div>
  <div class="card grid-card">
    <h2>&#8644; Red</h2>
    <div class="val" id="grid-val">-- W</div>
    <div class="sub" id="grid-sub">--</div>
  </div>
  <div class="card batt">
    <h2>&#128267; Bateria</h2>
    <div class="val" id="batt-soc">--%</div>
    <div class="bar-wrap">
      <div class="bar" id="batt-bar" style="width:0%"></div>
    </div>
    <div class="sub" id="batt-sub">-- W</div>
  </div>
  <div class="card load">
    <h2>&#127968; Carga</h2>
    <div class="val" id="load-val" style="color:#bb6bd9">-- W</div>
    <div class="sub">Consumo actual</div>
  </div>
</div>

<hr>

<!-- Daily donuts SVG -->
<div class="donut-wrap">
  <h1 style="margin-bottom:12px">Hoy</h1>
  <div class="donut-row">
    <div class="donut-box">
      <h3>Consumo</h3>
      <svg id="donut-con" width="120" height="120" viewBox="0 0 120 120">
        <circle cx="60" cy="60" r="46" fill="none" stroke="#21262d" stroke-width="18"/>
        <circle id="dc-pv"  cx="60" cy="60" r="46" fill="none" stroke="#2ecc71"
                stroke-width="18" stroke-dasharray="0 289" stroke-linecap="butt"
                transform="rotate(-90 60 60) " style="transition:stroke-dasharray .6s"/>
        <circle id="dc-dis" cx="60" cy="60" r="46" fill="none" stroke="#4a9eff"
                stroke-width="18" stroke-dasharray="0 289" stroke-linecap="butt"
                transform="rotate(-90 60 60) " style="transition:stroke-dasharray .6s"/>
        <circle id="dc-imp" cx="60" cy="60" r="46" fill="none" stroke="#bb6bd9"
                stroke-width="18" stroke-dasharray="0 289" stroke-linecap="butt"
                transform="rotate(-90 60 60) " style="transition:stroke-dasharray .6s"/>
        <text x="60" y="56" text-anchor="middle" fill="#eaeaea"
              font-size="13" font-weight="700" font-family="system-ui">
          <tspan id="dc-total">--</tspan>
        </text>
        <text x="60" y="71" text-anchor="middle" fill="#6e7681"
              font-size="10" font-family="system-ui">kWh</text>
      </svg>
      <div class="leg">
        <div class="leg-item"><div class="leg-dot" style="background:#2ecc71"></div>
          <span style="color:#6e7681">Solar</span></div>
        <div class="leg-item" style="justify-content:flex-end">
          <span id="dl-con-pv" style="color:#2ecc71">--</span></div>
        <div class="leg-item"><div class="leg-dot" style="background:#4a9eff"></div>
          <span style="color:#6e7681">Descarga</span></div>
        <div class="leg-item" style="justify-content:flex-end">
          <span id="dl-con-dis" style="color:#4a9eff">--</span></div>
        <div class="leg-item"><div class="leg-dot" style="background:#bb6bd9"></div>
          <span style="color:#6e7681">Import.</span></div>
        <div class="leg-item" style="justify-content:flex-end">
          <span id="dl-con-imp" style="color:#bb6bd9">--</span></div>
      </div>
    </div>
    <div class="donut-box">
      <h3>Produccion</h3>
      <svg id="donut-pro" width="120" height="120" viewBox="0 0 120 120">
        <circle cx="60" cy="60" r="46" fill="none" stroke="#21262d" stroke-width="18"/>
        <circle id="dp-load" cx="60" cy="60" r="46" fill="none" stroke="#f5c518"
                stroke-width="18" stroke-dasharray="0 289" stroke-linecap="butt"
                transform="rotate(-90 60 60) " style="transition:stroke-dasharray .6s"/>
        <circle id="dp-chg"  cx="60" cy="60" r="46" fill="none" stroke="#4a9eff"
                stroke-width="18" stroke-dasharray="0 289" stroke-linecap="butt"
                transform="rotate(-90 60 60) " style="transition:stroke-dasharray .6s"/>
        <circle id="dp-exp"  cx="60" cy="60" r="46" fill="none" stroke="#e88080"
                stroke-width="18" stroke-dasharray="0 289" stroke-linecap="butt"
                transform="rotate(-90 60 60) " style="transition:stroke-dasharray .6s"/>
        <text x="60" y="56" text-anchor="middle" fill="#eaeaea"
              font-size="13" font-weight="700" font-family="system-ui">
          <tspan id="dp-total">--</tspan>
        </text>
        <text x="60" y="71" text-anchor="middle" fill="#6e7681"
              font-size="10" font-family="system-ui">kWh</text>
      </svg>
      <div class="leg">
        <div class="leg-item"><div class="leg-dot" style="background:#f5c518"></div>
          <span style="color:#6e7681">Autocon.</span></div>
        <div class="leg-item" style="justify-content:flex-end">
          <span id="dl-pro-load" style="color:#f5c518">--</span></div>
        <div class="leg-item"><div class="leg-dot" style="background:#4a9eff"></div>
          <span style="color:#6e7681">Carga bat.</span></div>
        <div class="leg-item" style="justify-content:flex-end">
          <span id="dl-pro-chg" style="color:#4a9eff">--</span></div>
        <div class="leg-item"><div class="leg-dot" style="background:#e88080"></div>
          <span style="color:#6e7681">Export.</span></div>
        <div class="leg-item" style="justify-content:flex-end">
          <span id="dl-pro-exp" style="color:#e88080">--</span></div>
      </div>
    </div>
  </div>
</div>

<footer>
  <a href="/chart">&#128200; Gr&aacute;fica diaria</a>
  &nbsp;|&nbsp;
  <a href="/update">&#8593; Actualizar firmware</a>
</footer>

<!-- Status bar fija -->
<div id="status-bar">
  <span><span id="status-dot"></span><span id="status-txt">Conectando...</span></span>
  <span id="status-time">--:--:--</span>
</div>

<script>
// ── Circumferencia del donut (r=46): 2π×46 ≈ 289.03
const CIRC = 2 * Math.PI * 46;

// Calcula stroke-dasharray y stroke-dashoffset para un segmento
// offset = suma de fracciones anteriores × CIRC
function segDA(frac) {
  const len = frac * CIRC;
  return `${len.toFixed(2)} ${(CIRC - len).toFixed(2)}`;
}

function setDonut(ids, vals, total) {
  let offset = 0;
  ids.forEach((id, i) => {
    const el = document.getElementById(id);
    const frac = total > 0 ? Math.max(0, vals[i]) / total : 0;
    el.style.strokeDasharray = segDA(frac);
    // Rotar cada segmento para que empiece donde termina el anterior
    const deg = -90 + offset * 360;
    el.setAttribute('transform', `rotate(${deg.toFixed(2)} 60 60)`);
    offset += frac;
  });
}

function fmt(w)  { return (w / 1000).toFixed(2) + ' kWh'; }
function fmtW(w) { return (w >= 0 ? '+' : '') + w + ' W'; }

async function refresh() {
  try {
    const res = await fetch('/api/data');
    if (!res.ok) throw new Error(res.status);
    const d = await res.json();
    const l = d.live;
    const day = d.daily;

    // ── Live ──────────────────────────────────────────────────────────────
    document.getElementById('pv-val').textContent  = l.pv_w + ' W';
    document.getElementById('pv-sub').innerHTML    =
      'PV1: ' + l.pv1_w + ' W &nbsp; PV2: ' + l.pv2_w + ' W';

    const gridCol = l.grid_w > 0 ? '#4a9eff' : '#2ecc71';
    const gridTxt = l.grid_w > 0 ? 'Importando de la red'
                  : l.grid_w < 0 ? 'Exportando a la red' : 'Sin intercambio';
    const gv = document.getElementById('grid-val');
    gv.textContent = fmtW(l.grid_w);
    gv.style.color = gridCol;
    document.getElementById('grid-sub').textContent = gridTxt;

    const battCol = l.batt_w > 0 ? '#e74c3c' : '#2ecc71';
    const battTxt = l.batt_w < 0  ? 'Cargando'
                  : l.batt_w > 0  ? 'Descargando' : 'En reposo';
    document.getElementById('batt-soc').textContent = l.batt_soc + '%';
    const bar = document.getElementById('batt-bar');
    bar.style.width      = l.batt_soc + '%';
    bar.style.background = battCol;
    document.getElementById('batt-sub').textContent =
      fmtW(l.batt_w) + ' – ' + battTxt;

    document.getElementById('load-val').textContent = l.load_w + ' W';

    // ── Daily donuts ───────────────────────────────────────────────────────
    if (day && day.valid) {
      const pvDirect = Math.max(0,
        day.load_kwh - day.batt_discharge_kwh - day.import_kwh);
      const pvToLoad = Math.max(0,
        day.pv_kwh - day.export_kwh - day.batt_charge_kwh);

      const conVals = [pvDirect * 1000,
                      day.batt_discharge_kwh * 1000,
                      day.import_kwh * 1000];
      const proVals = [pvToLoad * 1000,
                      day.batt_charge_kwh * 1000,
                      day.export_kwh * 1000];

      setDonut(['dc-pv','dc-dis','dc-imp'], conVals, day.load_kwh * 1000);
      setDonut(['dp-load','dp-chg','dp-exp'], proVals, day.pv_kwh * 1000);

      document.getElementById('dc-total').textContent = day.load_kwh.toFixed(1);
      document.getElementById('dp-total').textContent = day.pv_kwh.toFixed(1);

      document.getElementById('dl-con-pv').textContent  = fmt(pvDirect * 1000);
      document.getElementById('dl-con-dis').textContent = fmt(day.batt_discharge_kwh * 1000);
      document.getElementById('dl-con-imp').textContent = fmt(day.import_kwh * 1000);
      document.getElementById('dl-pro-load').textContent = fmt(pvToLoad * 1000);
      document.getElementById('dl-pro-chg').textContent  = fmt(day.batt_charge_kwh * 1000);
      document.getElementById('dl-pro-exp').textContent  = fmt(day.export_kwh * 1000);
    } else if (day && !day.valid) {
      // Datos aún no disponibles — mostrar estado de espera
      document.getElementById('dc-total').textContent = '...';
      document.getElementById('dp-total').textContent = '...';
    }

    // ── Status bar ────────────────────────────────────────────────────────
    document.getElementById('status-dot').style.background = '#2ecc71';
    document.getElementById('status-txt').textContent =
      'Actualizado ' + new Date().toLocaleTimeString('es-ES');

  } catch (err) {
    document.getElementById('status-dot').style.background = '#e74c3c';
    document.getElementById('status-txt').textContent = 'Error: ' + err.message;
  }
}

// Reloj en la status bar (independiente del refresco de datos)
function clock() {
  document.getElementById('status-time').textContent =
    new Date().toLocaleTimeString('es-ES');
}

refresh();                          // carga inicial inmediata
setInterval(refresh, 5000);         // refresco de datos cada 5 s
setInterval(clock,   1000);         // reloj cada 1 s
</script>
</body></html>)";

    server.send(200, "text/html; charset=utf-8", html);
}

// ═════════════════════════════════════════════════════════════════════════
// Ruta GET /update  →  Formulario de subida OTA
// ═════════════════════════════════════════════════════════════════════════
static void handle_update_get() {
    String html = R"(<!DOCTYPE html><html lang="es"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OTA Update</title>
<style>
  body{background:#0d1117;color:#eaeaea;font-family:system-ui,sans-serif;
       display:flex;flex-direction:column;align-items:center;
       justify-content:center;min-height:100vh;gap:16px}
  h1{color:#4a9eff;font-size:1.2rem}
  form{background:#161b22;border:1px solid #21262d;border-radius:10px;
       padding:28px;display:flex;flex-direction:column;gap:14px;min-width:300px}
  input[type=file]{color:#eaeaea}
  button{background:#1f6feb;color:#fff;border:none;border-radius:6px;
         padding:10px;font-size:1rem;cursor:pointer}
  button:hover{background:#388bfd}
  #progress{width:100%;background:#21262d;border-radius:6px;height:12px;display:none}
  #bar{height:12px;border-radius:6px;background:#2ecc71;width:0%;transition:width .3s}
  #msg{color:#6e7681;font-size:.85rem;text-align:center}
  a{color:#4a9eff;font-size:.8rem}
</style></head><body>
<h1>&#8593; Actualización de Firmware OTA</h1>
<form id="frm" enctype="multipart/form-data">
  <label>Selecciona el fichero <code>.bin</code></label>
  <input type="file" id="fw" accept=".bin" required>
  <div id="progress"><div id="bar"></div></div>
  <div id="msg">Elige un fichero y pulsa Actualizar</div>
  <button type="button" onclick="upload();">&#8593; Actualizar</button>
</form>
<a href="/">&#8592; Volver al dashboard</a>
<script>
function upload(){
  const f=document.getElementById('fw').files[0];
  if(!f){alert('Elige un fichero .bin');return;}
  const fd=new FormData();fd.append('firmware',f);
  const xhr=new XMLHttpRequest();
  xhr.open('POST','/update');
  xhr.upload.onprogress=e=>{
    if(e.lengthComputable){
      const p=Math.round(e.loaded/e.total*100);
      document.getElementById('progress').style.display='block';
      document.getElementById('bar').style.width=p+'%';
      document.getElementById('msg').textContent='Subiendo… '+p+'%';
    }
  };
  xhr.onload=()=>{
    if(xhr.status===200){
      document.getElementById('msg').textContent=
        'OK – el dispositivo se reiniciará ahora.';
      document.getElementById('bar').style.background='#2ecc71';
    } else {
      document.getElementById('msg').textContent='Error: '+xhr.responseText;
      document.getElementById('bar').style.background='#e74c3c';
    }
  };
  xhr.onerror=()=>{
    document.getElementById('msg').textContent='Error de red.';
  };
  xhr.send(fd);
}
</script></body></html>)";

    server.send(200, "text/html; charset=utf-8", html);
}

static void handle_chart() {
    // Página servida en chunks para no agotar el heap del ESP32
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.sendHeader("Content-Type", "text/html; charset=utf-8");
    server.send(200);

    // ── HEAD + estilos ────────────────────────────────────────────────────
    server.sendContent(R"=EOF=(<!DOCTYPE html><html lang="es"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Deye – Gráfica diaria</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-annotation@3.0.1/dist/chartjs-plugin-annotation.min.js"></script>
<style>
:root{
  --bg:#0d1117;--card:#161b22;--muted:#6e7681;--white:#eaeaea;
  --pv:#f5c518;--grid:#4a9eff;--batt:#2ecc71;--load:#bb6bd9;
  --soc:#1a56db;--exp:#e88080;--ok:#2ecc71;--err:#e74c3c;
  --border:#21262d;--accent:#4a9eff
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--white);font-family:system-ui,sans-serif;
     min-height:100vh;display:flex;flex-direction:column}
header{background:var(--card);border-bottom:1px solid var(--border);
       padding:10px 16px;display:flex;align-items:center;gap:12px;flex-wrap:wrap}
header h1{font-size:1rem;color:var(--white);flex:1}
.nav-bar{display:flex;align-items:center;gap:8px}
.nav-btn{background:var(--border);border:none;color:var(--accent);
         padding:6px 14px;border-radius:6px;cursor:pointer;font-size:.9rem;
         transition:background .2s}
.nav-btn:hover{background:#2d333b}
.nav-btn:disabled{opacity:.4;cursor:default}
#date-lbl{font-size:.95rem;color:var(--white);min-width:140px;text-align:center}
.today-badge{background:var(--accent);color:#fff;border-radius:4px;
             font-size:.7rem;padding:2px 6px;margin-left:4px}
main{flex:1;padding:12px 16px;display:flex;flex-direction:column;gap:12px}
.chart-card{background:var(--card);border:1px solid var(--border);
            border-radius:10px;padding:12px}
.chart-card h2{font-size:.75rem;text-transform:uppercase;letter-spacing:.06em;
               color:var(--muted);margin-bottom:10px}
.chart-wrap{position:relative;height:260px}
.donut-row{display:flex;gap:12px;justify-content:center;flex-wrap:wrap}
.donut-box{display:flex;flex-direction:column;align-items:center;gap:6px;
           background:var(--card);border:1px solid var(--border);
           border-radius:10px;padding:12px;flex:1;min-width:180px;max-width:260px}
.donut-box h3{font-size:.7rem;text-transform:uppercase;letter-spacing:.06em;
              color:var(--muted)}
.leg{display:grid;grid-template-columns:1fr 1fr;gap:2px 10px;
     margin-top:4px;font-size:.7rem;width:100%}
.leg-item{display:flex;align-items:center;gap:4px}
.leg-dot{width:8px;height:8px;border-radius:2px;flex-shrink:0}
footer{text-align:center;color:var(--muted);font-size:.7rem;
       padding:10px;border-top:1px solid var(--border)}
footer a{color:var(--accent);text-decoration:none}
#status{position:fixed;bottom:28px;right:12px;
        background:var(--card);border:1px solid var(--border);
        border-radius:6px;padding:4px 10px;font-size:.7rem;color:var(--muted)}
#status-dot{display:inline-block;width:7px;height:7px;border-radius:50%;
            background:var(--muted);margin-right:5px;transition:background .3s}
</style></head><body>)=EOF=");

    // ── HEADER ────────────────────────────────────────────────────────────
    server.sendContent(R"=EOF=(
<header>
  <h1>&#128200; Gr&aacute;fica diaria</h1>
  <div class="nav-bar">
    <button class="nav-btn" id="btn-prev" onclick="changeDay(-1)">&#8592;</button>
    <span id="date-lbl">--</span>
    <button class="nav-btn" id="btn-next" onclick="changeDay(1)" disabled>&#8594;</button>
  </div>
  <a href="/" class="nav-btn">&#8592; Dashboard</a>
</header>
<main>

<!-- Gráfica de potencias -->
<div class="chart-card">
  <h2>&#9889; Potencias (W)</h2>
  <div class="chart-wrap"><canvas id="chart-pwr"></canvas></div>
</div>

<!-- Gráfica de SOC -->
<div class="chart-card">
  <h2>&#128267; Estado batería (%)</h2>
  <div class="chart-wrap" style="height:120px"><canvas id="chart-soc"></canvas></div>
</div>

<!-- Donuts -->
<div class="donut-row">
  <div class="donut-box">
    <h3>Consumo</h3>
    <svg width="110" height="110" viewBox="0 0 110 110">
      <circle cx="55" cy="55" r="42" fill="none" stroke="#21262d" stroke-width="16"/>
      <circle id="dc-pv"  cx="55" cy="55" r="42" fill="none" stroke="#2ecc71"
              stroke-width="16" stroke-dasharray="0 264" transform="rotate(-90 55 55)"
              style="transition:stroke-dasharray .6s"/>
      <circle id="dc-dis" cx="55" cy="55" r="42" fill="none" stroke="#4a9eff"
              stroke-width="16" stroke-dasharray="0 264" transform="rotate(-90 55 55)"
              style="transition:stroke-dasharray .6s"/>
      <circle id="dc-imp" cx="55" cy="55" r="42" fill="none" stroke="#bb6bd9"
              stroke-width="16" stroke-dasharray="0 264" transform="rotate(-90 55 55)"
              style="transition:stroke-dasharray .6s"/>
      <text x="55" y="51" text-anchor="middle" fill="#eaeaea"
            font-size="12" font-weight="700" font-family="system-ui">
        <tspan id="dc-total">--</tspan></text>
      <text x="55" y="64" text-anchor="middle" fill="#6e7681"
            font-size="9" font-family="system-ui">kWh</text>
    </svg>
    <div class="leg">
      <div class="leg-item"><div class="leg-dot" style="background:#2ecc71"></div>
        <span style="color:#6e7681">Solar dir.</span></div>
      <div class="leg-item" style="justify-content:flex-end">
        <span id="dl-pv" style="color:#2ecc71">--</span></div>
      <div class="leg-item"><div class="leg-dot" style="background:#4a9eff"></div>
        <span style="color:#6e7681">Descarga</span></div>
      <div class="leg-item" style="justify-content:flex-end">
        <span id="dl-dis" style="color:#4a9eff">--</span></div>
      <div class="leg-item"><div class="leg-dot" style="background:#bb6bd9"></div>
        <span style="color:#6e7681">Import.</span></div>
      <div class="leg-item" style="justify-content:flex-end">
        <span id="dl-imp" style="color:#bb6bd9">--</span></div>
    </div>
  </div>
  <div class="donut-box">
    <h3>Producci&oacute;n</h3>
    <svg width="110" height="110" viewBox="0 0 110 110">
      <circle cx="55" cy="55" r="42" fill="none" stroke="#21262d" stroke-width="16"/>
      <circle id="dp-lod" cx="55" cy="55" r="42" fill="none" stroke="#f5c518"
              stroke-width="16" stroke-dasharray="0 264" transform="rotate(-90 55 55)"
              style="transition:stroke-dasharray .6s"/>
      <circle id="dp-chg" cx="55" cy="55" r="42" fill="none" stroke="#1a56db"
              stroke-width="16" stroke-dasharray="0 264" transform="rotate(-90 55 55)"
              style="transition:stroke-dasharray .6s"/>
      <circle id="dp-exp" cx="55" cy="55" r="42" fill="none" stroke="#e88080"
              stroke-width="16" stroke-dasharray="0 264" transform="rotate(-90 55 55)"
              style="transition:stroke-dasharray .6s"/>
      <text x="55" y="51" text-anchor="middle" fill="#eaeaea"
            font-size="12" font-weight="700" font-family="system-ui">
        <tspan id="dp-total">--</tspan></text>
      <text x="55" y="64" text-anchor="middle" fill="#6e7681"
            font-size="9" font-family="system-ui">kWh</text>
    </svg>
    <div class="leg">
      <div class="leg-item"><div class="leg-dot" style="background:#f5c518"></div>
        <span style="color:#6e7681">Autocon.</span></div>
      <div class="leg-item" style="justify-content:flex-end">
        <span id="dl-lod" style="color:#f5c518">--</span></div>
      <div class="leg-item"><div class="leg-dot" style="background:#1a56db"></div>
        <span style="color:#6e7681">Carga bat.</span></div>
      <div class="leg-item" style="justify-content:flex-end">
        <span id="dl-chg" style="color:#1a56db">--</span></div>
      <div class="leg-item"><div class="leg-dot" style="background:#e88080"></div>
        <span style="color:#6e7681">Export.</span></div>
      <div class="leg-item" style="justify-content:flex-end">
        <span id="dl-exp" style="color:#e88080">--</span></div>
    </div>
  </div>
</div>
</main>

<footer>
  <a href="/">&#8592; Dashboard</a> &nbsp;|&nbsp;
  <a href="/update">&#8593; Firmware</a>
</footer>
<div id="status"><span id="status-dot"></span><span id="status-txt">Cargando...</span></div>)=EOF=");

    // ── SCRIPT ────────────────────────────────────────────────────────────
    server.sendContent(R"=EOF=(<script>
// ── Constantes ────────────────────────────────────────────────────────────
const CIRC     = 2 * Math.PI * 42;   // r=42
const REFRESH  = 30000;              // ms entre refrescos cuando es hoy
const COLORS   = {
  pv:'#f5c518', grid:'#4a9eff', batt:'#2ecc71', load:'#bb6bd9', soc:'#1a56db'
};

// ── Estado ────────────────────────────────────────────────────────────────
let currentDate = todayStr();   // YYYYMMDD
let lastTs      = 0;            // último timestamp recibido (para incrementales)
let refreshTimer = null;
let midnightTimer = null;

// ── Helpers de fecha ──────────────────────────────────────────────────────
function todayStr() {
  const d = new Date();
  return d.getFullYear().toString()
    + String(d.getMonth()+1).padStart(2,'0')
    + String(d.getDate()).padStart(2,'0');
}

function addDays(dateStr, n) {
  const d = new Date(
    parseInt(dateStr.slice(0,4)),
    parseInt(dateStr.slice(4,6))-1,
    parseInt(dateStr.slice(6,8))
  );
  d.setDate(d.getDate() + n);
  return d.getFullYear().toString()
    + String(d.getMonth()+1).padStart(2,'0')
    + String(d.getDate()).padStart(2,'0');
}

function isToday(dateStr) { return dateStr === todayStr(); }

function fmtDate(dateStr) {
  const MESES = ['Enero','Febrero','Marzo','Abril','Mayo','Junio',
                 'Julio','Agosto','Sept.','Octubre','Nov.','Dic.'];
  const d = new Date(
    parseInt(dateStr.slice(0,4)),
    parseInt(dateStr.slice(4,6))-1,
    parseInt(dateStr.slice(6,8))
  );
  const dias = ['Dom','Lun','Mar','Mié','Jue','Vie','Sáb'];
  return `${dias[d.getDay()]} ${d.getDate()} ${MESES[d.getMonth()]}`;
}

function tsToTime(ts) {
  const d = new Date(ts * 1000);
  return String(d.getHours()).padStart(2,'0') + ':' +
         String(d.getMinutes()).padStart(2,'0');
}

// ── Donut SVG ─────────────────────────────────────────────────────────────
function setDonut(ids, vals, total) {
  let offset = 0;
  ids.forEach((id, i) => {
    const el = document.getElementById(id);
    const frac = total > 0 ? Math.max(0, vals[i]) / total : 0;
    const len  = frac * CIRC;
    el.style.strokeDasharray = `${len.toFixed(2)} ${(CIRC-len).toFixed(2)}`;
    el.setAttribute('transform', `rotate(${(-90 + offset*360).toFixed(1)} 55 55)`);
    offset += frac;
  });
}

function updateDonuts(daily) {
  const pvDirect  = Math.max(0, daily.load - daily.bdis - daily.imp);
  const pvToLoad  = Math.max(0, daily.pv   - daily.exp  - daily.bchg);
  const conVals   = [pvDirect,  daily.bdis, daily.imp];
  const proVals   = [pvToLoad,  daily.bchg, daily.exp];

  setDonut(['dc-pv','dc-dis','dc-imp'], conVals, daily.load);
  setDonut(['dp-lod','dp-chg','dp-exp'], proVals, daily.pv);

  document.getElementById('dc-total').textContent = daily.load.toFixed(1);
  document.getElementById('dp-total').textContent = daily.pv.toFixed(1);

  const fmt = v => v.toFixed(2) + ' kWh';
  document.getElementById('dl-pv').textContent  = fmt(pvDirect);
  document.getElementById('dl-dis').textContent = fmt(daily.bdis);
  document.getElementById('dl-imp').textContent = fmt(daily.imp);
  document.getElementById('dl-lod').textContent = fmt(pvToLoad);
  document.getElementById('dl-chg').textContent = fmt(daily.bchg);
  document.getElementById('dl-exp').textContent = fmt(daily.exp);
}

// ── Charts.js ─────────────────────────────────────────────────────────────
const commonOpts = {
  responsive: true, maintainAspectRatio: false, animation: false,
  interaction: { mode:'index', intersect:false },
  plugins: { legend:{ labels:{ color:'#6e7681', boxWidth:12, font:{size:11} } },
             tooltip:{ backgroundColor:'#1c2128', titleColor:'#eaeaea',
                       bodyColor:'#eaeaea', borderColor:'#30363d', borderWidth:1 } },
  scales: {
    x: { ticks:{ color:'#6e7681', maxRotation:0, font:{size:10},
                 maxTicksLimit:13 },
         grid:{ color:'#21262d' } }
  }
};

// Gráfica de potencias
const pwrChart = new Chart(document.getElementById('chart-pwr'), {
  type: 'line',
  data: {
    labels: [],
    datasets: [
      { label:'PV',    data:[], borderColor:COLORS.pv,
        backgroundColor:COLORS.pv+'22', fill:true,
        borderWidth:1.5, pointRadius:0, tension:0.3 },
      { label:'Red',   data:[], borderColor:COLORS.grid,
        backgroundColor:COLORS.grid+'22', fill:true,
        borderWidth:1.5, pointRadius:0, tension:0.3 },
      { label:'Bat',   data:[], borderColor:COLORS.batt,
        backgroundColor:COLORS.batt+'22', fill:true,
        borderWidth:1.5, pointRadius:0, tension:0.3 },
      { label:'Carga', data:[], borderColor:COLORS.load,
        backgroundColor:COLORS.load+'22', fill:true,
        borderWidth:1.5, pointRadius:0, tension:0.3 },
    ]
  },
  options: {
    ...commonOpts,
    scales: {
      ...commonOpts.scales,
      y: { ticks:{ color:'#6e7681', font:{size:10},
                   callback: v => v >= 1000 ? (v/1000).toFixed(1)+'k' : v },
           grid:{ color:'#21262d' },
           // Línea de cero destacada
           afterDataLimits(scale) {
             const max = Math.max(scale.max, 100);
             const min = Math.min(scale.min, -100);
             scale.max = max; scale.min = min;
           }
      }
    },
    plugins: {
      ...commonOpts.plugins,
      tooltip: {
        ...commonOpts.plugins.tooltip
      },
      annotation: {
        annotations: {
          zeroLine: {
            type:'line', yMin:0, yMax:0,
            borderColor:'#4a9eff44', borderWidth:1, borderDash:[4,4]
          }
        }
      }
    }
  }
});

// Gráfica de SOC
const socChart = new Chart(document.getElementById('chart-soc'), {
  type: 'line',
  data: {
    labels: [],
    datasets: [{
      label:'SOC %', data:[], borderColor:COLORS.soc,
      backgroundColor:COLORS.soc+'33', fill:true,
      borderWidth:2, pointRadius:0, tension:0.3
    }]
  },
  options: {
    ...commonOpts,
    scales: {
      ...commonOpts.scales,
      y: { min:0, max:100,
           ticks:{ color:'#6e7681', font:{size:10},
                   callback: v => v + '%' },
           grid:{ color:'#21262d' } }
    }
  }
});

// ── Carga de datos ────────────────────────────────────────────────────────
function appendData(records) {
  if (!records || records.length === 0) return;

  records.forEach(r => {
    const label = tsToTime(r.t);
    pwrChart.data.labels.push(label);
    pwrChart.data.datasets[0].data.push(r.pv);
    pwrChart.data.datasets[1].data.push(r.grid);
    // batería: positivo = descargando, negativo = cargando (RAW del inversor)
    pwrChart.data.datasets[2].data.push(r.batt);
    pwrChart.data.datasets[3].data.push(r.load);

    socChart.data.labels.push(label);
    socChart.data.datasets[0].data.push(r.soc);

    lastTs = r.t;
  });

  pwrChart.update('none');   // sin animación para incrementales
  socChart.update('none');
}

async function loadDay(dateStr, incremental = false) {
  try {
    let url = `/api/history?date=${dateStr}&granularity=5min`;
    if (incremental && lastTs > 0) url += `&from_ts=${lastTs}`;

    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    if (!incremental) {
      // Carga completa: limpiar y rellenar desde cero
      pwrChart.data.labels   = [];
      socChart.data.labels   = [];
      pwrChart.data.datasets.forEach(ds => ds.data = []);
      socChart.data.datasets[0].data = [];
      lastTs = 0;
    }

    appendData(data.records);

    // Actualizar donuts siempre (también en incrementales)
    if (data.daily) {
      updateDonuts({
        pv:   data.daily.pv,
        exp:  data.daily.exp,
        imp:  data.daily.imp,
        load: data.daily.load,
        bchg: data.daily.bchg,
        bdis: data.daily.bdis,
      });
    }

    // Status
    const isInc = incremental && data.new_records > 0;
    document.getElementById('status-dot').style.background = '#2ecc71';
    document.getElementById('status-txt').textContent =
      isInc
        ? `+${data.new_records} nuevos · ${new Date().toLocaleTimeString('es-ES')}`
        : `Actualizado ${new Date().toLocaleTimeString('es-ES')}`;

  } catch(err) {
    document.getElementById('status-dot').style.background = '#e74c3c';
    document.getElementById('status-txt').textContent = 'Error: ' + err.message;
  }
}

// ── Navegación ────────────────────────────────────────────────────────────
function updateNav() {
  const today  = todayStr();
  const isHoy  = isToday(currentDate);
  const prev   = addDays(currentDate, -1);
  // Limitar a 90 días atrás (capacidad del histórico)
  const minDate = addDays(today, -90);

  document.getElementById('date-lbl').innerHTML =
    fmtDate(currentDate) + (isHoy ? '<span class="today-badge">hoy</span>' : '');
  document.getElementById('btn-prev').disabled = currentDate <= minDate;
  document.getElementById('btn-next').disabled = isHoy;
}

function changeDay(delta) {
  clearInterval(refreshTimer);
  clearTimeout(midnightTimer);

  currentDate = addDays(currentDate, delta);
  lastTs = 0;
  updateNav();
  loadDay(currentDate, false).then(() => {
    if (isToday(currentDate)) scheduleRefresh();
  });
}

// ── Refresco incremental (solo cuando es hoy) ─────────────────────────────
function scheduleRefresh() {
  refreshTimer = setInterval(async () => {
    // Detectar cambio de día (00:00)
    const today = todayStr();
    if (currentDate !== today) {
      // Era ayer y ahora es hoy → cambiar automáticamente
      clearInterval(refreshTimer);
      currentDate = today;
      lastTs = 0;
      updateNav();
      await loadDay(currentDate, false);
      scheduleRefresh();
      return;
    }
    // Actualización incremental normal
    await loadDay(currentDate, true);
  }, REFRESH);

  // Timer adicional exacto a medianoche para no depender del intervalo
  scheduleMidnight();
}

function scheduleMidnight() {
  const now  = new Date();
  const mdn  = new Date(now);
  mdn.setHours(24, 0, 5, 0);   // 00:00:05 del día siguiente
  const msUntil = mdn - now;
  midnightTimer = setTimeout(() => {
    // Mostrar el nuevo día automáticamente
    const newDay = todayStr();
    if (currentDate !== newDay) {
      clearInterval(refreshTimer);
      currentDate = newDay;
      lastTs = 0;
      updateNav();
      loadDay(currentDate, false).then(() => scheduleRefresh());
    }
  }, msUntil);
}

// ── Inicio ────────────────────────────────────────────────────────────────
updateNav();
fetch('/api/latest_date').then(r => r.json()).then(d => {
  // Si el último día con datos 5-min no es hoy, navegar a él directamente
  if (d.date && d.date !== currentDate) {
    currentDate = d.date;
    updateNav();
  }
  loadDay(currentDate, false).then(() => {
    if (isToday(currentDate)) scheduleRefresh();
  });
}).catch(() => {
  loadDay(currentDate, false).then(() => {
    if (isToday(currentDate)) scheduleRefresh();
  });
});
</script></body></html>)=EOF=");
}

// ═════════════════════════════════════════════════════════════════════════
// Ruta POST /update  →  Recepción del .bin y flasheo
// ═════════════════════════════════════════════════════════════════════════
static void handle_update_post() {
    // La respuesta se envía desde el handler de upload (abajo)
    // Aquí solo reiniciamos si todo fue bien
    if (Update.hasError()) return;
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
}

static void handle_upload() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        Serial0.printf("[OTA] Inicio: %s (%u bytes)\n",
                      upload.filename.c_str(), upload.totalSize);
        // UPDATE_SIZE_UNKNOWN → Update calcula el espacio él solo
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Serial0.print("[OTA] Error al iniciar: ");
            Update.printError(Serial);
        }

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Serial0.print("[OTA] Error escribiendo: ");
            Update.printError(Serial);
        }

    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial0.printf("[OTA] Completado: %u bytes. Reiniciando.\n",
                          upload.totalSize);
        } else {
            Serial0.print("[OTA] Error al finalizar: ");
            Update.printError(Serial);
            // Informar al cliente del error
            server.send(500, "text/plain",
                        String("Error OTA: ") + Update.errorString());
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Ruta GET /api/data  →  JSON para integraciones externas
// ═════════════════════════════════════════════════════════════════════════
static void handle_api_data() {
    EnergyData e{};
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (s_energy) e = *s_energy;
        xSemaphoreGive(s_mutex);
    }

    // ── Daily: leer desde cache PSRAM (fuente primaria) ───────────────────
    DailyStats d{};
    time_t now; time(&now);
    struct tm tm_now; localtime_r(&now, &tm_now);
    tm_now.tm_hour = 0; tm_now.tm_min = 0;
    tm_now.tm_sec  = 0; tm_now.tm_isdst = -1;
    uint32_t today_ep = (uint32_t)mktime(&tm_now);

    DailyRecord dr{};
    if (Cache.getDaily(today_ep, dr)) {
        d = daily_record_to_stats(dr);
    } else if (s_daily && s_mutex &&
               xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Fallback al puntero compartido si la cache aún no tiene datos
        d = *s_daily;
        xSemaphoreGive(s_mutex);
    }

    char json[512];
    snprintf(json, sizeof(json),
        R"({"live":{"pv_w":%d,"pv1_w":%d,"pv2_w":%d,)"
        R"("grid_w":%d,"batt_w":%d,"batt_soc":%d,"load_w":%d},)"
        R"("daily":{"pv_kwh":%.2f,"export_kwh":%.2f,"import_kwh":%.2f,)"
        R"("load_kwh":%.2f,"batt_charge_kwh":%.2f,"batt_discharge_kwh":%.2f,)"
        R"("valid":%s}})",
        (int)e.pv_power, (int)e.pv1_power, (int)e.pv2_power,
        (int)e.grid_power, (int)e.batt_power, (int)e.batt_soc, (int)e.load_power,
        d.pv_kwh, d.export_kwh, d.import_kwh,
        d.load_kwh, d.batt_charge_kwh, d.batt_discharge_kwh,
        d.valid ? "true" : "false");

    Serial0.printf("[API] daily valid=%d pv=%.2f load=%.2f cache_day=%lu\n",
          d.valid, d.pv_kwh, d.load_kwh,
          (unsigned long)today_ep);

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

// ── Helper: epoch desde string YYYYMMDD ───────────────────────────────────
static uint32_t parse_date(const String& s) {
    if (s.length() != 8) return 0;
    struct tm tm{};
    tm.tm_year = s.substring(0,4).toInt() - 1900;
    tm.tm_mon  = s.substring(4,6).toInt() - 1;
    tm.tm_mday = s.substring(6,8).toInt();
    tm.tm_isdst = -1;
    return (uint32_t)mktime(&tm);
}

static String epoch_to_date(uint32_t ep) {
    time_t t = (time_t)ep;
    struct tm tm; localtime_r(&t, &tm);
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday);
    return String(buf);
}

// ── GET /api/status ───────────────────────────────────────────────────────
static void handle_status() {
    Record5Min first{}, last{};
    bool has_first = false, has_last = false;
    has_last = Store.getLastRecord(last);

    char json[512];
    snprintf(json, sizeof(json),
        "{\"raw\":{\"count\":%lu,\"capacity\":%lu},"
        "\"hourly\":{\"count\":%lu,\"capacity\":17520},"
        "\"daily\":{\"count\":%lu,\"capacity\":730},"
        "\"psram_free_kb\":%lu,"
        "\"wifi_rssi\":%d,\"ip\":\"%s\"}",
        (unsigned long)Store.getRawCount(),
        (unsigned long)201600,
        (unsigned long)Store.getHourlyCount(),
        (unsigned long)Store.getDailyCount(),
        (unsigned long)ESP.getFreePsram() / 1024,
        (int)WiFi.RSSI(),
        WiFi.localIP().toString().c_str());

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

// ── GET /api/latest_date ──────────────────────────────────────────────────
static void handle_latest_date() {
    uint32_t ep = Store.getLastRawDayEpoch();
    char json[48];
    if (ep == 0) {
        snprintf(json, sizeof(json), "{\"date\":null}");
    } else {
        time_t t = (time_t)ep;
        struct tm tm; localtime_r(&t, &tm);
        snprintf(json, sizeof(json), "{\"date\":\"%04d%02d%02d\"}",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

// ── GET /api/history ──────────────────────────────────────────────────────
static void handle_history() {
    String date_s  = server.arg("date");
    String from_s  = server.arg("from");
    String to_s    = server.arg("to");
    String gran    = server.arg("granularity");
    if (gran == "") gran = "hourly";

    // ── Rango de fechas (para granularity=daily) ──────────────────────────
    if (gran == "daily" && from_s.length() == 8 && to_s.length() == 8) {
        uint32_t from_ep = parse_date(from_s);
        uint32_t to_ep   = parse_date(to_s);
        if (!from_ep || !to_ep || to_ep < from_ep) {
            server.send(400, "application/json", "{\"error\":\"bad range\"}");
            return;
        }

        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.sendHeader("Content-Type", "application/json");
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200);
        server.sendContent("{\"from\":\"" + from_s + "\",\"to\":\"" + to_s +
                            "\",\"granularity\":\"daily\",\"records\":[");

        bool first = true;
        for (uint32_t d = from_ep; d <= to_ep; d += 86400) {
            DailyRecord dr{};
            if (!Cache.getDaily(d, dr)) continue;
            DailyStats ds = daily_record_to_stats(dr);

            char buf[256];
            snprintf(buf, sizeof(buf),
                "%s{\"date\":\"%s\","
                "\"pv\":%.2f,\"export\":%.2f,\"import\":%.2f,"
                "\"load\":%.2f,\"bchg\":%.2f,\"bdis\":%.2f,"
                "\"soc_start\":%d,\"soc_end\":%d,\"complete\":%s}",
                first ? "" : ",",
                epoch_to_date(d).c_str(),
                ds.pv_kwh, ds.export_kwh, ds.import_kwh,
                ds.load_kwh, ds.batt_charge_kwh, ds.batt_discharge_kwh,
                dr.soc_start, dr.soc_end,
                (dr.flags & 0x02) ? "true" : "false");   // bit1 = día completo
            server.sendContent(buf);
            first = false;
        }
        server.sendContent("]}");
        return;
    }

    // ── Día concreto ──────────────────────────────────────────────────────
    uint32_t dep = 0;
    if (date_s.length() == 8) {
        dep = parse_date(date_s);
    } else {
        // Por defecto: hoy
        time_t now; time(&now);
        struct tm tm; localtime_r(&now, &tm);
        tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_isdst = -1;
        dep = (uint32_t)mktime(&tm);
        date_s = epoch_to_date(dep);
        date_s.replace("-", "");
    }
    if (!dep) { server.send(400, "application/json", "{\"error\":\"bad date\"}"); return; }

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.sendHeader("Content-Type", "application/json");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200);

    if (gran == "5min") {
        // from_ts: solo devolver registros con timestamp > from_ts
        // Si no se especifica, devolver todos
        uint32_t from_ts = 0;
        if (server.hasArg("from_ts"))
            from_ts = (uint32_t)server.arg("from_ts").toInt();

        uint32_t  count_out = 0;
        const Record5Min* recs = Cache.getRawDay(dep, count_out);

        // Filtrar por from_ts y contar cuántos enviaremos
        uint32_t first_idx = 0;
        if (from_ts > 0 && recs) {
            while (first_idx < count_out && recs[first_idx].timestamp <= from_ts)
                first_idx++;
        }
        uint32_t send_count = count_out - first_idx;

        // Último registro del día para los totales diarios
        Record5Min last_rec{};
        DailyRecord dr{};
        DailyStats daily{};
        if (Cache.getDaily(dep, dr)) daily = daily_record_to_stats(dr);
        
        char hdr[256];
        snprintf(hdr, sizeof(hdr),
            "{\"date\":\"%s\",\"granularity\":\"5min\","
            "\"from_ts\":%lu,\"new_records\":%lu,"
            "\"daily\":{\"pv\":%.2f,\"exp\":%.2f,\"imp\":%.2f,"
            "\"load\":%.2f,\"bchg\":%.2f,\"bdis\":%.2f},"
            "\"records\":[",
            epoch_to_date(dep).c_str(),
            (unsigned long)from_ts,
            (unsigned long)send_count,
            daily.pv_kwh, daily.export_kwh, daily.import_kwh,
            daily.load_kwh, daily.batt_charge_kwh, daily.batt_discharge_kwh);
        server.sendContent(hdr);

        Cache.lock();
        for (uint32_t i = first_idx; i < count_out; i++) {
            const Record5Min& r = recs[i];
            char buf[128];
            snprintf(buf, sizeof(buf),
                "%s{\"t\":%lu,\"pv\":%d,\"grid\":%d,"
                "\"batt\":%d,\"load\":%d,\"soc\":%d}",
                i > first_idx ? "," : "",
                (unsigned long)r.timestamp,
                r.pv_w, r.grid_w, r.batt_w, r.load_w, r.soc);
            server.sendContent(buf);
        }
        Cache.unlock();
        server.sendContent("]}");
    } else {
        // ── hourly (default) ──────────────────────────────────────────────
        const HourlyRecord* hr = Cache.getHourly(dep);

        // Último registro del día para totales diarios
        DailyRecord dr{};
        DailyStats  daily{};
        if (Cache.getDaily(dep, dr)) daily = daily_record_to_stats(dr);

        char hdr[256];
        snprintf(hdr, sizeof(hdr),
            "{\"date\":\"%s\",\"granularity\":\"hourly\","
            "\"daily\":{\"pv\":%.2f,\"exp\":%.2f,\"imp\":%.2f,"
            "\"load\":%.2f,\"bchg\":%.2f,\"bdis\":%.2f},"
            "\"records\":[",
            epoch_to_date(dep).c_str(),
            daily.pv_kwh, daily.export_kwh, daily.import_kwh,
            daily.load_kwh, daily.batt_charge_kwh, daily.batt_discharge_kwh);
        server.sendContent(hdr);

        bool first = true;
        if (hr) {
            for (int h = 0; h < 24; h++) {
                if (!(hr[h].flags & 0x01) || hr[h].sample_count == 0) continue;
                char buf[160];
                snprintf(buf, sizeof(buf),
                    "%s{\"h\":%d,\"pv_w\":%d,\"grid_w\":%d,"
                    "\"batt_w\":%d,\"load_w\":%d,\"soc\":%d,\"n\":%d}",
                    first ? "" : ",",
                    h,
                    hr[h].avg_pv_w, hr[h].avg_grid_w,
                    hr[h].avg_batt_w, hr[h].avg_load_w,
                    hr[h].soc_end, hr[h].sample_count);
                server.sendContent(buf);
                first = false;
            }
        }
        server.sendContent("]}");
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Tarea FreeRTOS
// ═════════════════════════════════════════════════════════════════════════
void webserver_task(void* /*pv*/) {
    for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(2));   // cede CPU; 2 ms de latencia máxima
    }
}

void webserver_begin() {
    server.on("/",         HTTP_GET,  handle_root);
    server.on("/update",   HTTP_GET,  handle_update_get);
    server.on("/api/data", HTTP_GET,  handle_api_data);
    server.on("/update",   HTTP_POST, handle_update_post, handle_upload);
    server.on("/api/history",     HTTP_GET, handle_history);
    server.on("/api/latest_date", HTTP_GET, handle_latest_date);
    server.on("/api/status",      HTTP_GET, handle_status);
    server.on("/chart", HTTP_GET, handle_chart);
    server.onNotFound([]() {
        server.send(404, "text/plain", "Not found");
    });
    server.begin();
    Serial0.println("[Web] Servidor en http://" + WiFi.localIP().toString());

    xTaskCreatePinnedToCore(webserver_task, "webserver",
                             8192, nullptr, 1, nullptr, 0);
}