#include "web_server.h"
#include <WebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <time.h>

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

<footer><a href="/update">&#8593; Actualizar firmware</a></footer>

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

    const battCol = l.batt_w >= 0 ? '#2ecc71' : '#e74c3c';
    const battTxt = l.batt_w > 0  ? 'Descargando'
                  : l.batt_w < 0  ? 'Cargando' : 'En reposo';
    document.getElementById('batt-soc').textContent = l.batt_soc + '%';
    const bar = document.getElementById('batt-bar');
    bar.style.width      = l.batt_soc + '%';
    bar.style.background = battCol;
    document.getElementById('batt-sub').textContent =
      fmtW(l.batt_w) + ' – ' + battTxt;

    document.getElementById('load-val').textContent = l.load_w + ' W';

    // ── Daily donuts ───────────────────────────────────────────────────────
    if (day) {
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

      document.getElementById('dc-total').textContent =
        day.load_kwh.toFixed(1);
      document.getElementById('dp-total').textContent =
        day.pv_kwh.toFixed(1);

      document.getElementById('dl-con-pv').textContent  = fmt(pvDirect * 1000);
      document.getElementById('dl-con-dis').textContent =
        fmt(day.batt_discharge_kwh * 1000);
      document.getElementById('dl-con-imp').textContent =
        fmt(day.import_kwh * 1000);
      document.getElementById('dl-pro-load').textContent = fmt(pvToLoad * 1000);
      document.getElementById('dl-pro-chg').textContent  =
        fmt(day.batt_charge_kwh * 1000);
      document.getElementById('dl-pro-exp').textContent  =
        fmt(day.export_kwh * 1000);
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
    DailyStats d{};
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (s_energy) e = *s_energy;
        if (s_daily)  d = *s_daily;
        xSemaphoreGive(s_mutex);
    }

    char json[512];
    snprintf(json, sizeof(json),
        R"({"live":{"pv_w":%d,"pv1_w":%d,"pv2_w":%d,)"
        R"("grid_w":%d,"batt_w":%d,"batt_soc":%d,"load_w":%d},)"
        R"("daily":{"pv_kwh":%.2f,"export_kwh":%.2f,"import_kwh":%.2f,)"
        R"("load_kwh":%.2f,"batt_charge_kwh":%.2f,"batt_discharge_kwh":%.2f}})",
        (int)e.pv_power, (int)e.pv1_power, (int)e.pv2_power,
        (int)e.grid_power, (int)e.batt_power, (int)e.batt_soc, (int)e.load_power,
        d.pv_kwh, d.export_kwh, d.import_kwh,
        d.load_kwh, d.batt_charge_kwh, d.batt_discharge_kwh);

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
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
    server.onNotFound([]() {
        server.send(404, "text/plain", "Not found");
    });
    server.begin();
    Serial0.println("[Web] Servidor en http://" + WiFi.localIP().toString());

    xTaskCreatePinnedToCore(webserver_task, "webserver",
                             8192, nullptr, 1, nullptr, 0);
}