#include <WebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <time.h>
#include <ArduinoJson.h>
#include "web_server.h"
#include "config.h"
#include "storage.h"
#include "data_store.h"
#include "psram_cache.h"
#include "web_assets_chartjs.h"
#include "web_alerts.h"
#include "fw_update.h"
#include "clock_mode.h"

extern uint32_t      g_uptime_start;   // main.cpp: millis() en el arranque
extern volatile bool g_ota_cancel;   // main.cpp: retira el overlay OTA si la actualización falla

static WebServer          server(80);
static SemaphoreHandle_t  s_mutex   = nullptr;
static const EnergyData*  s_energy  = nullptr;
static const DailyStats*  s_daily   = nullptr;
static bool               s_ota_authed = false;

// ─────────────────────────────────────────────────────────────────────────────
// Rate-limit de autenticación — contador global (dispositivo de un solo
// admin en LAN, no hace falta trackear por IP) con backoff exponencial:
// a partir del 3er fallo consecutivo, cada fallo adicional dobla la espera
// hasta un máximo de 5 minutos. Se resetea con un login correcto. No
// persiste entre reinicios — un reinicio ya requiere acceso físico u OTA.
// ─────────────────────────────────────────────────────────────────────────────
static uint8_t  s_auth_fail_count    = 0;
static uint32_t s_auth_lockout_until = 0;

static uint32_t auth_lockout_duration_ms(uint8_t fails) {
    if (fails < 3) return 0;
    uint32_t secs = 1UL << min((int)(fails - 3), 8);   // 1,2,4,...,256 s
    if (secs > 300) secs = 300;                        // tope 5 min
    return secs * 1000UL;
}

static bool auth_is_locked_out() {
    return s_auth_lockout_until != 0 && millis() < s_auth_lockout_until;
}

static void auth_record_failure() {
    if (s_auth_fail_count < 255) s_auth_fail_count++;
    uint32_t dur = auth_lockout_duration_ms(s_auth_fail_count);
    if (dur > 0) s_auth_lockout_until = millis() + dur;
}

static void auth_record_success() {
    s_auth_fail_count    = 0;
    s_auth_lockout_until = 0;
}

static bool auth_send_lockout_response() {
    uint32_t remaining_s = (s_auth_lockout_until - millis() + 999) / 1000;
    char buf[16]; snprintf(buf, sizeof(buf), "%lu", (unsigned long)remaining_s);
    server.sendHeader("Retry-After", buf);
    server.send(429, "text/plain",
        "Demasiados intentos fallidos. Espera " + String(buf) + " s e inténtalo de nuevo.");
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Auth helper – devuelve true si el acceso está permitido.
// Exige HTTP Basic Auth con usuario "admin". Si no hay contraseña en NVS el
// acceso se DENIEGA (403) hasta que se defina desde la pantalla táctil.
// ─────────────────────────────────────────────────────────────────────────────
static bool admin_password_missing() {
    return Storage.loadAdminPassword().isEmpty();
}

// DEYE-20: sin contraseña de administración las rutas sensibles (config, OTA,
// importar/borrar datos, reinicio) quedan bloqueadas en vez de abiertas.
// La contraseña solo puede crearse desde la pantalla táctil (Config → Acceso web).
static void send_password_required_response() {
    server.send(403, "text/plain; charset=utf-8",
        "Acceso bloqueado: no hay contraseña de administración.\n"
        "Defínela desde la pantalla táctil (Config > Acceso web) y recarga.");
}

// Respuesta de denegación coherente: 403 informativo si no hay contraseña
// (un prompt de Basic Auth no serviría de nada), 401 en caso contrario.
static void send_auth_denied_response() {
    if (admin_password_missing()) {
        send_password_required_response();
        return;
    }
    server.requestAuthentication(BASIC_AUTH, "Deye Admin",
        "Acceso restringido. Configure la contraseña desde la pantalla táctil.");
}

static bool check_auth() {
    String pw = Storage.loadAdminPassword();
    if (pw.isEmpty()) {
        send_password_required_response();
        return false;
    }
    if (auth_is_locked_out()) return auth_send_lockout_response();
    if (server.authenticate("admin", pw.c_str())) {
        auth_record_success();
        return true;
    }
    auth_record_failure();
    server.requestAuthentication(BASIC_AUTH, "Deye Admin",
        "Acceso restringido. Configure la contraseña desde la pantalla táctil.");
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mitigación CSRF — HTTP Basic Auth no protege por sí solo: el navegador
// reenvía las credenciales cacheadas también en peticiones disparadas desde
// OTRA web (p.ej. un <form> oculto apuntando a /update). Si el navegador
// indica Origin o Referer, deben apuntar a este mismo dispositivo. Las
// peticiones sin ninguno de los dos (curl, scripts) se permiten — no pueden
// llevar credenciales cacheadas de un navegador ajenas a quien las envía.
// ─────────────────────────────────────────────────────────────────────────────
static bool same_origin_request() {
    String host = server.hostHeader();
    if (host.isEmpty()) return true;   // sin Host no podemos comparar

    String origin = server.header("Origin");
    if (origin.length() > 0) return origin.indexOf(host) >= 0;

    String referer = server.header("Referer");
    if (referer.length() > 0) return referer.indexOf(host) >= 0;

    return true;
}

static bool check_same_origin() {
    if (same_origin_request()) return true;
    DBGSERIAL.println("[Web] Petición rechazada: Origin/Referer no coincide (posible CSRF)");
    server.send(403, "text/plain", "Forbidden: cross-origin request");
    return false;
}

void webserver_set_data(SemaphoreHandle_t mutex,
                        const EnergyData* energy,
                        const DailyStats* daily) {
    s_mutex  = mutex;
    s_energy = energy;
    s_daily  = daily;
}

// ═════════════════════════════════════════════════════════════════════════
// Ruta GET /   →  Dashboard con 4 indicadores de arco SVG (estilo pantalla)
// ═════════════════════════════════════════════════════════════════════════
static void handle_root() {
    AppConfig cfg{}; Storage.loadConfig(cfg);

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.sendHeader("Content-Type", "text/html; charset=utf-8");
    server.send(200);

    // ── HEAD + CSS ────────────────────────────────────────────────────────
    server.sendContent(R"=EOF=(<!DOCTYPE html><html lang="es"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Deye Monitor</title>
<style>
:root{--bg:#0d1117;--card:#161b22;--bd:#21262d;--accent:#4a9eff;
      --ok:#2ecc71;--warn:#f5c518;--err:#e74c3c;--muted:#6e7681;--wh:#eaeaea}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--wh);font-family:system-ui,sans-serif;
     padding-bottom:8px}
/* Barra superior */
#topbar{background:var(--card);border-bottom:1px solid var(--bd);
        padding:5px 12px;display:flex;align-items:center;
        justify-content:space-between;font-size:.78rem;gap:8px}
#tb-clock{font-variant-numeric:tabular-nums}
#tb-ac{font-size:.95rem;font-weight:700}
.dot{display:inline-block;width:7px;height:7px;border-radius:50%;
     background:var(--muted);margin-right:4px;vertical-align:middle;
     transition:background .3s}
/* Tarjetas 2×2 — layout vertical */
.cards{display:grid;grid-template-columns:1fr 1fr;gap:8px;padding:8px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:10px;
      display:flex;flex-direction:column;align-items:center;
      padding:8px 8px 10px;gap:4px;min-width:0;overflow:hidden}
.ct{font-size:.6rem;text-transform:uppercase;letter-spacing:.07em;
    color:var(--muted);align-self:flex-start}
.aw{width:calc(50vw - 28px);height:calc(50vw - 28px);max-width:150px;max-height:150px}
.aw svg{display:block;width:100%;height:100%}
.csub{font-size:.65rem;color:var(--muted);text-align:center;
      white-space:nowrap;overflow:hidden;text-overflow:ellipsis;width:100%}
@media(min-width:700px){
  .cards{grid-template-columns:repeat(4,1fr)}
  .aw{width:calc(25vw - 28px);height:calc(25vw - 28px);max-width:220px;max-height:220px}
}
/* Sección diaria */
hr{border:none;border-top:1px solid var(--bd);margin:0 8px}
.dw{padding:10px 8px 4px}
.dw h2{font-size:.7rem;text-transform:uppercase;letter-spacing:.07em;
        color:var(--muted);text-align:center;margin-bottom:8px}
.kpis{display:flex;gap:20px;justify-content:center;margin-bottom:10px}
.kpi{text-align:center}
.kpi-v{font-size:1.2rem;font-weight:700}
.kpi-l{font-size:.58rem;text-transform:uppercase;letter-spacing:.06em;color:var(--muted)}
.donut-row{display:flex;gap:10px;justify-content:center;flex-wrap:wrap}
.donut-box{display:flex;flex-direction:column;align-items:center;gap:4px;
           flex:1;min-width:150px;max-width:220px}
.donut-box h3{font-size:.68rem;text-transform:uppercase;
              letter-spacing:.06em;color:var(--muted)}
.leg{display:grid;grid-template-columns:1fr 1fr;gap:2px 8px;
     margin-top:4px;font-size:.67rem;width:100%}
.li{display:flex;align-items:center;gap:4px}
.ld{width:7px;height:7px;border-radius:2px;flex-shrink:0}
footer{text-align:center;color:var(--muted);font-size:.7rem;
       padding:10px 8px;border-top:1px solid var(--bd);margin-top:6px}
footer a{color:var(--accent);text-decoration:none}
</style>
<link rel="manifest" href="/manifest.json">
<meta name="theme-color" content="#0d1117">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<link rel="apple-touch-icon" href="/icon.svg">
</head><body>)=EOF=");

    // ── BARRA SUPERIOR ────────────────────────────────────────────────────
    server.sendContent(R"=EOF=(
<div id="topbar">
  <span id="tb-clock">--:--:--</span>
  <span>&#9889;&thinsp;<span id="tb-ac" style="color:var(--muted)">--%</span>
    <span style="font-size:.62rem;color:var(--muted)">autocon.</span></span>
  <a href="#" id="tb-notif" title="Notificaciones del navegador" style="display:none;
     color:var(--muted);text-decoration:none;font-size:.85rem">&#128276;</a>
  <span><span class="dot" id="tb-dot"></span><span id="tb-txt" style="color:var(--muted)">--</span></span>
</div>)=EOF=");

    // ── TARJETAS 2×2 ─────────────────────────────────────────────────────
    // Geometría: r=52, cx=cy=60, viewBox 120×120
    //   C    = 2π×52 = 326.73
    //   ARC  = 0.75×C = 245.05  (270°)
    //   GAP  = 81.68
    //   THIRD = ARC/3 = 81.68   (franjas batería)
    //   transform="rotate(135 60 60)" → arranca a las 7 en punto
    server.sendContent(R"=EOF=(
<div class="cards">

<!-- ☀ Solar -->
<div class="card">
  <div class="ct">&#9728; Solar</div>
  <div class="aw"><svg viewBox="0 0 120 120">
    <circle cx="60" cy="60" r="52" fill="none" stroke="#21262d" stroke-width="10"
      stroke-dasharray="245.05 81.68" stroke-linecap="butt" transform="rotate(135 60 60)"/>
    <circle id="a-pv" cx="60" cy="60" r="52" fill="none" stroke="#f5c518" stroke-width="10"
      stroke-dasharray="0 326.73" stroke-linecap="butt" transform="rotate(135 60 60)"
      style="transition:stroke-dasharray .5s"/>
    <text x="60" y="55" text-anchor="middle" fill="#eaeaea" font-size="20" font-weight="700"
      font-family="system-ui" id="a-pv-t">--</text>
    <text x="60" y="72" text-anchor="middle" fill="#eaeaea" font-size="12"
      font-family="system-ui">W</text>
  </svg></div>
  <div class="csub" id="pv-sub">PV1: -- &nbsp; PV2: --</div>
</div>

<!-- ⇄ Red (bipolar: azul=importar / verde=exportar) -->
<div class="card">
  <div class="ct">&#8644; Red</div>
  <div class="aw"><svg viewBox="0 0 120 120">
    <circle cx="60" cy="60" r="52" fill="none" stroke="#21262d" stroke-width="10"
      stroke-dasharray="245.05 81.68" stroke-linecap="butt" transform="rotate(135 60 60)"/>
    <circle id="a-gimp" cx="60" cy="60" r="52" fill="none" stroke="#4a9eff" stroke-width="10"
      stroke-dasharray="0 326.73" stroke-linecap="butt" transform="rotate(135 60 60)"
      style="transition:stroke-dasharray .5s,stroke-dashoffset .5s"/>
    <circle id="a-gexp" cx="60" cy="60" r="52" fill="none" stroke="#2ecc71" stroke-width="10"
      stroke-dasharray="0 326.73" stroke-linecap="butt" transform="rotate(135 60 60)"
      style="transition:stroke-dasharray .5s,stroke-dashoffset .5s"/>
    <text x="60" y="55" text-anchor="middle" fill="#eaeaea" font-size="20" font-weight="700"
      font-family="system-ui" id="a-gv-t">--</text>
    <text x="60" y="72" text-anchor="middle" fill="#eaeaea" font-size="12"
      font-family="system-ui" id="a-gu-t">W</text>
  </svg></div>
  <div class="csub" id="grid-sub">--</div>
</div>

<!-- 🔋 Batería (degradado multi-segmento rojo→amarillo→verde + máscara gris) -->
<div class="card">
  <div class="ct">&#128267; Bater&iacute;a</div>
  <div class="aw"><svg viewBox="0 0 120 120">
)=EOF=");

    // ── Degradado batería: 24 segmentos rojo→amarillo→verde ──────────────────
    // Un linearGradient SVG no funciona en arcos de 270° (no monótono en 2D).
    // Se generan N segmentos de arco, cada uno con su color interpolado.
    {
        static const int   N       = 24;
        static const float SEG_LEN = 245.05f / N;   // ~10.21 px por segmento
        static const float GAP     = 326.73f - SEG_LEN;
        char sb[220];
        for (int i = 0; i < N; i++) {
            float t = (float)i / (N - 1);           // 0.0 → 1.0 a lo largo del arco
            uint8_t r, g, b;
            if (t <= 0.5f) {
                float u = t * 2.0f;                 // rojo → amarillo
                r = (uint8_t)(231 + (245 - 231) * u + 0.5f);
                g = (uint8_t)( 76 + (197 -  76) * u + 0.5f);
                b = (uint8_t)( 60 - ( 60 -  24) * u + 0.5f);
            } else {
                float u = (t - 0.5f) * 2.0f;       // amarillo → verde
                r = (uint8_t)(245 - (245 -  46) * u + 0.5f);
                g = (uint8_t)(197 + (204 - 197) * u + 0.5f);
                b = (uint8_t)( 24 + (113 -  24) * u + 0.5f);
            }
            snprintf(sb, sizeof(sb),
                "    <circle cx=\"60\" cy=\"60\" r=\"52\" fill=\"none\" stroke=\"#%02x%02x%02x\""
                " stroke-width=\"10\" stroke-dasharray=\"%.2f %.2f\""
                " stroke-linecap=\"butt\" transform=\"rotate(135 60 60)\""
                " stroke-dashoffset=\"%.2f\"/>\n",
                r, g, b, SEG_LEN, GAP, -i * SEG_LEN);
            server.sendContent(sb);
        }
    }

    server.sendContent(R"=EOF=(    <circle id="a-bm" cx="60" cy="60" r="52" fill="none" stroke="#21262d" stroke-width="11"
      stroke-dasharray="245.05 81.68" stroke-linecap="butt" transform="rotate(135 60 60)"/>
    <text x="60" y="60" text-anchor="middle" dominant-baseline="middle" fill="#eaeaea"
      font-size="24" font-weight="700" font-family="system-ui" id="a-soc-t">--%</text>
  </svg></div>
  <div class="csub" id="batt-sub">--</div>
</div>

<!-- 🏠 Carga -->
<div class="card">
  <div class="ct">&#127968; Carga</div>
  <div class="aw"><svg viewBox="0 0 120 120">
    <circle cx="60" cy="60" r="52" fill="none" stroke="#21262d" stroke-width="10"
      stroke-dasharray="245.05 81.68" stroke-linecap="butt" transform="rotate(135 60 60)"/>
    <circle id="a-ld" cx="60" cy="60" r="52" fill="none" stroke="#bb6bd9" stroke-width="10"
      stroke-dasharray="0 326.73" stroke-linecap="butt" transform="rotate(135 60 60)"
      style="transition:stroke-dasharray .5s"/>
    <text x="60" y="55" text-anchor="middle" fill="#eaeaea" font-size="20" font-weight="700"
      font-family="system-ui" id="a-ld-t">--</text>
    <text x="60" y="72" text-anchor="middle" fill="#eaeaea" font-size="12"
      font-family="system-ui">W</text>
  </svg></div>
  <div class="csub">Consumo actual</div>
</div>

</div>)=EOF=");

    // ── SECCIÓN DIARIA (donuts) ───────────────────────────────────────────
    server.sendContent(R"=EOF=(
<hr>
<div class="dw">
  <h2>Hoy</h2>
  <div class="kpis">
    <div class="kpi">
      <div class="kpi-v" id="kpi-autosuf" style="color:#2ecc71">--%</div>
      <div class="kpi-l">Autosuficiencia</div>
    </div>
    <div class="kpi">
      <div class="kpi-v" id="kpi-autocon" style="color:#f5c518">--%</div>
      <div class="kpi-l">Autoconsumo</div>
    </div>
  </div>
  <div class="donut-row">
    <div class="donut-box">
      <h3>Consumo</h3>
      <svg width="110" height="110" viewBox="0 0 110 110">
        <circle cx="55" cy="55" r="42" fill="none" stroke="#21262d" stroke-width="16"/>
        <circle id="dc-pv"  cx="55" cy="55" r="42" fill="none" stroke="#2ecc71"
          stroke-width="16" stroke-dasharray="0 264" stroke-linecap="butt"
          transform="rotate(-90 55 55)" style="transition:stroke-dasharray .6s"/>
        <circle id="dc-dis" cx="55" cy="55" r="42" fill="none" stroke="#4a9eff"
          stroke-width="16" stroke-dasharray="0 264" stroke-linecap="butt"
          transform="rotate(-90 55 55)" style="transition:stroke-dasharray .6s"/>
        <circle id="dc-imp" cx="55" cy="55" r="42" fill="none" stroke="#bb6bd9"
          stroke-width="16" stroke-dasharray="0 264" stroke-linecap="butt"
          transform="rotate(-90 55 55)" style="transition:stroke-dasharray .6s"/>
        <text x="55" y="51" text-anchor="middle" fill="#eaeaea" font-size="12"
          font-weight="700" font-family="system-ui"><tspan id="dc-total">--</tspan></text>
        <text x="55" y="64" text-anchor="middle" fill="#6e7681"
          font-size="9" font-family="system-ui">kWh</text>
      </svg>
      <div class="leg">
        <div class="li"><div class="ld" style="background:#2ecc71"></div>
          <span style="color:#6e7681">Solar dir.</span></div>
        <div class="li" style="justify-content:flex-end">
          <span id="dl-con-pv" style="color:#2ecc71">--</span></div>
        <div class="li"><div class="ld" style="background:#4a9eff"></div>
          <span style="color:#6e7681">Descarga</span></div>
        <div class="li" style="justify-content:flex-end">
          <span id="dl-con-dis" style="color:#4a9eff">--</span></div>
        <div class="li"><div class="ld" style="background:#bb6bd9"></div>
          <span style="color:#6e7681">Import.</span></div>
        <div class="li" style="justify-content:flex-end">
          <span id="dl-con-imp" style="color:#bb6bd9">--</span></div>
      </div>
    </div>
    <div class="donut-box">
      <h3>Producci&oacute;n</h3>
      <svg width="110" height="110" viewBox="0 0 110 110">
        <circle cx="55" cy="55" r="42" fill="none" stroke="#21262d" stroke-width="16"/>
        <circle id="dp-lod" cx="55" cy="55" r="42" fill="none" stroke="#f5c518"
          stroke-width="16" stroke-dasharray="0 264" stroke-linecap="butt"
          transform="rotate(-90 55 55)" style="transition:stroke-dasharray .6s"/>
        <circle id="dp-chg" cx="55" cy="55" r="42" fill="none" stroke="#4a9eff"
          stroke-width="16" stroke-dasharray="0 264" stroke-linecap="butt"
          transform="rotate(-90 55 55)" style="transition:stroke-dasharray .6s"/>
        <circle id="dp-exp" cx="55" cy="55" r="42" fill="none" stroke="#e88080"
          stroke-width="16" stroke-dasharray="0 264" stroke-linecap="butt"
          transform="rotate(-90 55 55)" style="transition:stroke-dasharray .6s"/>
        <text x="55" y="51" text-anchor="middle" fill="#eaeaea" font-size="12"
          font-weight="700" font-family="system-ui"><tspan id="dp-total">--</tspan></text>
        <text x="55" y="64" text-anchor="middle" fill="#6e7681"
          font-size="9" font-family="system-ui">kWh</text>
      </svg>
      <div class="leg">
        <div class="li"><div class="ld" style="background:#f5c518"></div>
          <span style="color:#6e7681">Autocon.</span></div>
        <div class="li" style="justify-content:flex-end">
          <span id="dl-pro-lod" style="color:#f5c518">--</span></div>
        <div class="li"><div class="ld" style="background:#4a9eff"></div>
          <span style="color:#6e7681">Carga bat.</span></div>
        <div class="li" style="justify-content:flex-end">
          <span id="dl-pro-chg" style="color:#4a9eff">--</span></div>
        <div class="li"><div class="ld" style="background:#e88080"></div>
          <span style="color:#6e7681">Export.</span></div>
        <div class="li" style="justify-content:flex-end">
          <span id="dl-pro-exp" style="color:#e88080">--</span></div>
      </div>
    </div>
  </div>
</div>

<footer>
  <a href="/chart">&#128200; Gr&aacute;fica</a> &nbsp;|&nbsp;
  <a href="/admin">&#9881; Admin</a> &nbsp;|&nbsp;
  <a href="/update">&#8593; Firmware</a> &nbsp;|&nbsp;
  <a href="#" id="csv-toggle">&#128229; CSV</a> &nbsp;|&nbsp;
  <span id="app-ver" style="color:var(--muted)">v--</span>
  <div id="csv-panel" style="display:none;justify-content:center;align-items:center;
       gap:8px;flex-wrap:wrap;padding-top:10px">
    <input type="date" id="csv-from" style="background:var(--card);color:var(--wh);
           border:1px solid var(--bd);border-radius:6px;padding:4px 6px;font-size:.75rem">
    <span style="color:var(--muted)">&#8594;</span>
    <input type="date" id="csv-to" style="background:var(--card);color:var(--wh);
           border:1px solid var(--bd);border-radius:6px;padding:4px 6px;font-size:.75rem">
    <a href="#" id="csv-download" style="color:var(--accent);text-decoration:none;
       font-size:.75rem">&#11015; Descargar</a>
  </div>
</footer>)=EOF=");

    // ── JS: constantes desde AppConfig + lógica ───────────────────────────
    {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "\n<script>const IM=%u,GM=%u,C=326.73,ARC=245.05,HALF=122.52;\n",
                 (unsigned)cfg.inv_max_w, (unsigned)cfg.grid_max_w);
        server.sendContent(buf);
    }
    server.sendContent(R"=EOF=(
const CD=2*Math.PI*42;

function ip(id){return document.getElementById(id);}

function setArc(id,f){
  const l=Math.max(0,Math.min(ARC,f*ARC));
  ip(id).setAttribute('stroke-dasharray',l.toFixed(2)+' '+(C-l).toFixed(2));
}

function setBatt(soc){
  const filled=(soc/100)*ARC;
  const rest=ARC-filled;
  const bm=ip('a-bm');
  bm.setAttribute('stroke-dasharray',rest.toFixed(2)+' '+(C-rest).toFixed(2));
  bm.setAttribute('stroke-dashoffset',(-filled).toFixed(2));
}

function setGrid(gw){
  const imp=ip('a-gimp'), exp=ip('a-gexp');
  const zero='0 '+C.toFixed(2);
  if(gw>0){
    const l=Math.min(HALF,(gw/GM)*HALF);
    imp.setAttribute('stroke-dasharray',l.toFixed(2)+' '+(C-l).toFixed(2));
    imp.setAttribute('stroke-dashoffset',(-HALF).toFixed(2));
    exp.setAttribute('stroke-dasharray',zero);
  } else if(gw<0){
    const l=Math.min(HALF,(-gw/GM)*HALF);
    exp.setAttribute('stroke-dasharray',l.toFixed(2)+' '+(C-l).toFixed(2));
    exp.setAttribute('stroke-dashoffset',(-(HALF-l)).toFixed(2));
    imp.setAttribute('stroke-dasharray',zero);
  } else {
    imp.setAttribute('stroke-dasharray',zero);
    exp.setAttribute('stroke-dasharray',zero);
  }
}

function setDonut(ids,vals,total){
  let off=0;
  ids.forEach((id,i)=>{
    const el=ip(id);
    const frac=total>0?Math.max(0,vals[i])/total:0;
    const len=frac*CD;
    el.style.strokeDasharray=len.toFixed(2)+' '+(CD-len).toFixed(2);
    el.setAttribute('transform','rotate('+((-90+off*360).toFixed(1))+' 55 55)');
    off+=frac;
  });
}

function fmt(v){return v.toFixed(2)+' kWh';}

async function refresh(){
  try{
    const res=await fetch('/api/data');
    if(!res.ok) throw new Error(res.status);
    const d=await res.json();
    const l=d.live, day=d.daily;

    // Solar
    ip('a-pv-t').textContent=l.pv_w;
    ip('pv-sub').innerHTML='PV1: '+l.pv1_w+' W &nbsp; PV2: '+l.pv2_w+' W';
    setArc('a-pv',l.pv_w/IM);

    // Red
    const gw=l.grid_w;
    const gcol=gw>0?'#4a9eff':gw<0?'#2ecc71':'#6e7681';
    const gtxt=gw>0?'Importando':gw<0?'Exportando':'En reposo';
    const gvt=ip('a-gv-t'), gut=ip('a-gu-t');
    gvt.textContent=(gw>0?'+':'')+gw;
    ip('grid-sub').textContent=gtxt;
    setGrid(gw);

    // Batería
    const soc=l.batt_soc, bw=l.batt_w;
    ip('a-soc-t').textContent=soc+'%';
    ip('batt-sub').textContent=Math.abs(bw)+' W \xb7 '+(bw<0?'Cargando':bw>0?'Descargando':'En reposo');
    setBatt(soc);

    // Carga
    ip('a-ld-t').textContent=l.load_w;
    setArc('a-ld',l.load_w/IM);

    // ── Autoconsumo instantáneo (barra superior) ───────────────────────────
    const pv=l.pv_w;
    if(pv>10){
      const ex=gw<0?-gw:0;
      const ac=Math.min(100,Math.round((pv-ex)/pv*100));
      const ae=ip('tb-ac');
      ae.textContent=ac+'%';
      ae.style.color=ac>=80?'#2ecc71':ac>=50?'#f5c518':'#e74c3c';
    } else {
      ip('tb-ac').textContent='--%';
      ip('tb-ac').style.color='var(--muted)';
    }

    // ── Donuts diarios ────────────────────────────────────────────────────
    if(day&&day.valid){
      const pvDir=Math.max(0,day.load_kwh-day.batt_discharge_kwh-day.import_kwh);
      const pvLd=Math.max(0,day.pv_kwh-day.export_kwh-day.batt_charge_kwh);
      setDonut(['dc-pv','dc-dis','dc-imp'],
               [pvDir,day.batt_discharge_kwh,day.import_kwh],day.load_kwh);
      setDonut(['dp-lod','dp-chg','dp-exp'],
               [pvLd,day.batt_charge_kwh,day.export_kwh],day.pv_kwh);
      ip('dc-total').textContent=day.load_kwh.toFixed(1);
      ip('dp-total').textContent=day.pv_kwh.toFixed(1);
      ip('dl-con-pv').textContent=fmt(pvDir);
      ip('dl-con-dis').textContent=fmt(day.batt_discharge_kwh);
      ip('dl-con-imp').textContent=fmt(day.import_kwh);
      ip('dl-pro-lod').textContent=fmt(pvLd);
      ip('dl-pro-chg').textContent=fmt(day.batt_charge_kwh);
      ip('dl-pro-exp').textContent=fmt(day.export_kwh);
      const asuf=day.load_kwh>0.01?
        Math.max(0,Math.min(100,(day.load_kwh-day.import_kwh)/day.load_kwh*100)):0;
      const acon=day.pv_kwh>0.01?
        Math.max(0,Math.min(100,(day.pv_kwh-day.export_kwh)/day.pv_kwh*100)):0;
      ip('kpi-autosuf').textContent=asuf.toFixed(0)+'%';
      ip('kpi-autocon').textContent=acon.toFixed(0)+'%';
    }

    ip('tb-dot').style.background='#2ecc71';
    ip('tb-txt').textContent='Actualizado '+new Date().toLocaleTimeString('es-ES');

  }catch(err){
    ip('tb-dot').style.background='#e74c3c';
    ip('tb-txt').textContent='Error';
  }
}

function clock(){
  ip('tb-clock').textContent=new Date().toLocaleTimeString('es-ES');
}

refresh();
fetch('/api/status').then(r=>r.json()).then(s=>{
  if(s.version) ip('app-ver').textContent='v'+s.version;
}).catch(()=>{});
setInterval(refresh,5000);
setInterval(clock,1000);
(function(){
  function fmt(d){return d.getFullYear().toString()+'-'+
    String(d.getMonth()+1).padStart(2,'0')+'-'+
    String(d.getDate()).padStart(2,'0');}
  function fmtCompact(s){return s.replace(/-/g,'');}

  var to=new Date(),from=new Date(to);
  from.setDate(from.getDate()-29);
  var elFrom=ip('csv-from'), elTo=ip('csv-to');
  elFrom.value=fmt(from);
  elTo.value=fmt(to);
  elFrom.max=elTo.max=fmt(to);

  function updateHref(){
    ip('csv-download').href='/api/export?from='+fmtCompact(elFrom.value)
                            +'&to='+fmtCompact(elTo.value);
  }
  updateHref();
  elFrom.addEventListener('change',updateHref);
  elTo.addEventListener('change',updateHref);

  ip('csv-toggle').addEventListener('click',function(e){
    e.preventDefault();
    var p=ip('csv-panel');
    p.style.display=(p.style.display==='none')?'flex':'none';
  });
})();

// ── Notificaciones del navegador mientras el dashboard está abierto ───────
// No es Web Push real (no hace falta service worker ni claves VAPID): solo
// funciona con esta pestaña abierta (puede estar en segundo plano), pero
// cubre el caso de uso principal sin criptografía nueva en el firmware.
(function(){
  var btn = ip('tb-notif');
  if (!('Notification' in window)) return;   // navegador sin soporte
  btn.style.display = 'inline';

  function updateIcon(){
    var p = Notification.permission;
    btn.style.color = p==='granted' ? '#2ecc71' : p==='denied' ? '#e74c3c' : 'var(--muted)';
  }
  updateIcon();

  btn.addEventListener('click', function(e){
    e.preventDefault();
    if (Notification.permission === 'default') {
      Notification.requestPermission().then(updateIcon);
    } else if (Notification.permission === 'denied') {
      alert('Las notificaciones están bloqueadas para este sitio — actívalas desde los ajustes del navegador.');
    } else {
      new Notification('Deye Monitor', {body:'Notificaciones activas', icon:'/icon.svg'});
    }
  });

  var lastAlertId = null;   // null = aún no se ha hecho el primer sondeo
  function pollAlerts(){
    var url = '/api/alerts' + (lastAlertId!==null ? ('?since_id='+lastAlertId) : '');
    fetch(url).then(function(r){return r.json();}).then(function(d){
      if (lastAlertId === null) { lastAlertId = d.last_id; return; } // no avisar del histórico al abrir
      (d.alerts||[]).forEach(function(a){
        lastAlertId = a.id;
        if (Notification.permission === 'granted')
          new Notification('Deye Monitor', {body:a.text, icon:'/icon.svg', tag:'deye-'+a.id});
      });
    }).catch(function(){});
  }
  pollAlerts();
  setInterval(pollAlerts, 15000);
})();
</script></body></html>)=EOF=");
}

// ═════════════════════════════════════════════════════════════════════════
// Ruta GET /update  →  Formulario de subida OTA
// ═════════════════════════════════════════════════════════════════════════
static void handle_update_get() {
    s_ota_authed = false;   // invalidar cualquier sesión OTA anterior
    if (!check_auth()) return;
    // DEYE-40: literal en flash enviado con send_P (antes se copiaba a un String)
    static const char OTA_PAGE_HTML[] PROGMEM = R"(<!DOCTYPE html><html lang="es"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Deye – Firmware</title>
<meta name="theme-color" content="#0d1117">
<link rel="icon" href="/icon.svg" type="image/svg+xml">
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
  .card{background:#161b22;border:1px solid #21262d;border-radius:10px;
        padding:22px 28px;display:flex;flex-direction:column;gap:10px;
        width:min(440px,calc(100vw - 32px));box-sizing:border-box}
  form{width:min(440px,calc(100vw - 32px));box-sizing:border-box}
  .card h2{margin:0;font-size:1rem;color:#eaeaea}
  .row{display:flex;justify-content:space-between;gap:16px;font-size:.9rem;color:#6e7681}
  .row b{color:#eaeaea;font-weight:600}
  #gh-msg{color:#c9d1d9;font-size:.9rem;line-height:1.35}
  #gh-prog{width:100%;background:#21262d;border-radius:6px;height:12px;display:none}
  #gh-bar{height:12px;border-radius:6px;background:#2ecc71;width:0%;transition:width .3s}
  .btns{display:flex;gap:10px;flex-wrap:wrap}
  button:disabled{opacity:.5;cursor:default}
  #gh-install{background:#238636}
  #gh-install:hover{background:#2ea043}
  .sep{color:#6e7681;font-size:.8rem}
</style></head><body>
<h1>&#8593; Actualización de Firmware</h1>
<div class="card" id="gh">
  <h2>&#8681; Actualizar desde GitHub</h2>
  <div class="row"><span>Versión instalada</span><b id="gh-cur">&ndash;</b></div>
  <div class="row"><span>Placa</span><b id="gh-env">&ndash;</b></div>
  <div class="row" id="gh-lat-row" style="display:none"><span>Última versión</span><b id="gh-lat">&ndash;</b></div>
  <div id="gh-msg">Pulsa «Buscar» para consultar la última release publicada.</div>
  <div id="gh-prog"><div id="gh-bar"></div></div>
  <div class="btns">
    <button type="button" id="gh-check" onclick="ghCheck();">Buscar actualización</button>
    <button type="button" id="gh-install" style="display:none" onclick="ghInstall();">Instalar</button>
  </div>
  <a id="gh-notes" href="#" target="_blank" rel="noopener" style="display:none">Ver notas de la versión en GitHub</a>
</div>
<div class="card" id="sys">
  <h2>&#9881; Sistema</h2>
  <div class="row"><span>Tiempo en marcha</span><b id="sy-up">&ndash;</b></div>
  <div class="row"><span>Memoria libre (m&iacute;n.)</span><b id="sy-heap">&ndash;</b></div>
  <div class="row"><span>PSRAM libre</span><b id="sy-psram">&ndash;</b></div>
  <div class="row"><span>Almacenamiento usado</span><b id="sy-fs">&ndash;</b></div>
  <div class="row"><span>WiFi</span><b id="sy-wifi">&ndash;</b></div>
</div>
<div class="sep">&mdash; o sube el fichero a mano &mdash;</div>
<form id="frm" enctype="multipart/form-data">
  <label>Selecciona el fichero <code>.bin</code></label>
  <input type="file" id="fw" accept=".bin" required>
  <div id="progress"><div id="bar"></div></div>
  <div id="msg">Elige un fichero y pulsa Actualizar</div>
  <button type="button" onclick="upload();">&#8593; Actualizar</button>
</form>
<a href="/">&#8592; Volver al dashboard</a>
<script>
function sysFmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
  return (d?d+' d ':'')+h+' h '+m+' min';}
function sysLoad(){
  fetch('/api/status').then(r=>r.json()).then(j=>{
    const $=id=>document.getElementById(id);
    $('sy-up').textContent=sysFmtUp(j.uptime_s);
    $('sy-heap').textContent=j.heap_free_kb+' KB ('+j.heap_min_kb+' KB)';
    $('sy-psram').textContent=Math.round(j.psram_free_kb/1024*10)/10+' MB';
    $('sy-fs').textContent=Math.round(j.fs_used_kb/1024*10)/10+' / '+Math.round(j.fs_total_kb/1024*10)/10+' MB';
    $('sy-wifi').textContent=j.wifi_rssi+' dBm · '+j.ip;
  }).catch(()=>{});
}
sysLoad();setInterval(sysLoad,10000);
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

// ── Actualización desde GitHub ──────────────────────────────────────────
const $=id=>document.getElementById(id);
let ghLast=null, ghArmed=false, ghTimer=null, ghBusy=false;
async function ghFetch(path,method){
  const r=await fetch(path,{method:method||'GET',cache:'no-store'});
  if(!r.ok) throw new Error((await r.text())||('HTTP '+r.status));
  return r.json();
}
function ghRender(s){
  ghLast=s;
  $('gh-cur').textContent=s.current;
  $('gh-env').textContent=s.env||'desconocida';
  $('gh-lat-row').style.display=s.latest?'flex':'none';
  $('gh-lat').textContent=s.latest||'';
  const n=$('gh-notes'); n.style.display=s.notes_url?'inline':'none'; if(s.notes_url) n.href=s.notes_url;
  const msg=$('gh-msg'), chk=$('gh-check'), ins=$('gh-install'), pr=$('gh-prog');
  pr.style.display='none'; ins.style.display='none'; chk.disabled=false;
  switch(s.state){
  case 'checking':
    msg.textContent='Consultando GitHub…'; chk.disabled=true; break;
  case 'uptodate':
    msg.textContent='Ya tienes la última versión ('+s.current+').'; break;
  case 'available':
    msg.textContent='Hay una versión nueva: '+s.latest+' ('+Math.round(s.size/1024)+' KB'+
      (s.has_digest?', verificada con SHA-256':'')+').';
    ins.style.display='inline-block';
    ins.textContent=ghArmed?'Confirmar: instalar '+s.latest+' y reiniciar':'Instalar '+s.latest;
    break;
  case 'downloading':
    msg.textContent='Descargando e instalando… no apagues el dispositivo.';
    chk.disabled=true; pr.style.display='block';
    $('gh-bar').style.width=(s.size?Math.round(s.done/s.size*100):0)+'%'; break;
  case 'installed':
    msg.textContent='Instalado. El dispositivo se está reiniciando…';
    chk.disabled=true; pr.style.display='block'; $('gh-bar').style.width='100%';
    ghWaitReboot(); return;
  case 'error':
    msg.textContent='Error: '+s.error; break;
  default:
    msg.textContent='Pulsa «Buscar» para consultar la última release publicada.';
  }
}
async function ghPoll(){
  clearTimeout(ghTimer);
  try{
    const s=await ghFetch('/api/fw/status'); ghRender(s);
    if(s.state==='checking'||s.state==='downloading') ghTimer=setTimeout(ghPoll,s.state==='downloading'?700:1000);
  }catch(e){
    // Durante la descarga un fallo puntual de red no debe cortar el seguimiento
    if(ghBusy){ ghTimer=setTimeout(ghPoll,1500); }
    else $('gh-msg').textContent='Sin respuesta del dispositivo ('+e.message+')';
  }
}
async function ghCheck(){
  ghArmed=false;
  try{ ghRender(await ghFetch('/api/fw/check','POST')); ghTimer=setTimeout(ghPoll,800); }
  catch(e){ $('gh-msg').textContent='Error: '+e.message; }
}
async function ghInstall(){
  // Doble confirmación: el primer clic arma el botón, el segundo instala
  if(!ghArmed){
    ghArmed=true; ghRender(ghLast);
    setTimeout(()=>{ if(ghArmed){ ghArmed=false; if(ghLast) ghRender(ghLast); } },8000);
    return;
  }
  ghArmed=false; ghBusy=true;
  try{ ghRender(await ghFetch('/api/fw/install','POST')); ghTimer=setTimeout(ghPoll,700); }
  catch(e){ ghBusy=false; $('gh-msg').textContent='Error: '+e.message; }
}
function ghWaitReboot(){
  // Esperar a que el dispositivo vuelva a responder y recargar con la versión nueva
  ghBusy=false;
  const t0=Date.now();
  const probe=()=>fetch('/api/status',{cache:'no-store'}).then(r=>{
    if(r.ok) location.reload(); else throw 0;
  }).catch(()=>{ if(Date.now()-t0<120000) setTimeout(probe,2000); });
  setTimeout(probe,6000);
}
ghPoll();
</script></body></html>)";

    server.send_P(200, "text/html; charset=utf-8", OTA_PAGE_HTML);
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
<script src="/assets/chart.js"></script>
<script src="/assets/chart-annotation.js"></script>
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
.no-data-overlay{
  display:none;position:absolute;inset:0;
  align-items:center;justify-content:center;flex-direction:column;gap:8px;
  background:rgba(13,17,23,.65);border-radius:6px;pointer-events:none}
.no-data-overlay.visible{display:flex}
.no-data-icon{font-size:2.2rem;opacity:.35}
.no-data-txt{color:var(--muted);font-size:.85rem}
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
</style>
<link rel="manifest" href="/manifest.json">
<meta name="theme-color" content="#0d1117">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<link rel="apple-touch-icon" href="/icon.svg">
</head><body>)=EOF=");

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
  <div class="chart-wrap">
    <canvas id="chart-pwr"></canvas>
    <div id="no-data-pwr" class="no-data-overlay">
      <span class="no-data-icon">&#128202;</span>
      <span class="no-data-txt">Sin datos para este d&iacute;a</span>
    </div>
  </div>
</div>

<!-- Gráfica de SOC -->
<div class="chart-card">
  <h2>&#128267; Estado bater&iacute;a (%)</h2>
  <div class="chart-wrap" style="height:120px">
    <canvas id="chart-soc"></canvas>
    <div id="no-data-soc" class="no-data-overlay"></div>
  </div>
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
  <a href="/admin">&#9881; Admin</a> &nbsp;|&nbsp;
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

// ── Sin datos ─────────────────────────────────────────────────────────────
function showNoData(visible) {
  document.getElementById('no-data-pwr').classList.toggle('visible', visible);
  document.getElementById('no-data-soc').classList.toggle('visible', visible);
}

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
      // Renderizar vacío ahora; appendData lo rellena si hay datos
      pwrChart.update('none');
      socChart.update('none');
    }

    const hasRecords = data.records && data.records.length > 0;
    appendData(data.records);

    // Mostrar overlay "sin datos" solo en cargas completas
    if (!incremental) showNoData(!hasRecords);

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
// ═════════════════════════════════════════════════════════════════════════
// Actualización desde GitHub: /api/fw/status (GET), /api/fw/check y
// /api/fw/install (POST). Las tres exigen contraseña de administración; las
// POST, además, que la petición venga de esta misma página (anti-CSRF). La
// consulta y la descarga corren en tareas propias (fw_update.cpp): estos
// handlers solo las lanzan y devuelven el estado.
// ═════════════════════════════════════════════════════════════════════════
static void send_fw_status() {
    FwStatus st;
    fw_update_status(st);
    JsonDocument d;
    d["state"]      = fw_state_name(st.state);
    d["env"]        = st.env;
    d["current"]    = st.current;
    d["latest"]     = st.latest;
    d["notes_url"]  = st.notes_url;
    d["asset"]      = st.asset;
    d["size"]       = st.size;
    d["done"]       = st.done;
    d["has_digest"] = st.has_digest;
    d["error"]      = st.error;
    String out;
    serializeJson(d, out);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
}

static void handle_fw_status() {
    if (!check_auth()) return;
    send_fw_status();
}

static void handle_fw_check() {
    if (!check_auth()) return;
    if (!check_same_origin()) return;
    if (!fw_update_check_async()) {
        server.send(409, "text/plain", "Ya hay una operación de actualización en curso");
        return;
    }
    send_fw_status();
}

static void handle_fw_install() {
    if (!check_auth()) return;
    if (!check_same_origin()) return;
    if (!fw_update_install_async()) {
        server.send(409, "text/plain",
                    "No hay una versión disponible para instalar o hay otra operación en curso");
        return;
    }
    send_fw_status();
}

static void handle_update_post() {
    if (!s_ota_authed) {
        send_auth_denied_response();
        return;
    }
    if (Update.hasError()) return;
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", "OK");
    Store.flush();
    delay(500);
    ESP.restart();
}

static void handle_upload() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        // Verificar autenticación y origen antes de iniciar la actualización
        if (!same_origin_request()) {
            DBGSERIAL.println("[OTA] Rechazado: Origin/Referer no coincide (posible CSRF)");
            s_ota_authed = false;
            return;
        }
        String pw = Storage.loadAdminPassword();
        if (pw.isEmpty()) {
            DBGSERIAL.println("[OTA] Rechazado: sin contraseña de administración");
            s_ota_authed = false;
        } else if (auth_is_locked_out()) {
            DBGSERIAL.println("[OTA] Rechazado: demasiados intentos fallidos");
            s_ota_authed = false;
            auth_send_lockout_response();
            return;
        } else if (server.authenticate("admin", pw.c_str())) {
            auth_record_success();
            s_ota_authed = true;
        } else {
            auth_record_failure();
            s_ota_authed = false;
        }
        if (!s_ota_authed) {
            DBGSERIAL.println("[OTA] Rechazado: acceso denegado");
            return;
        }
        DBGSERIAL.printf("[OTA] Inicio: %s (%u bytes)\n",
                      upload.filename.c_str(), upload.totalSize);
        // Pedir overlay OTA a Core 1 y esperar confirmación antes de tocar flash
        extern volatile bool g_ota_request;
        extern volatile bool g_ota_active;
        g_ota_request = true;
        uint32_t t0 = millis();
        while (!g_ota_active && (millis() - t0 < 1500)) delay(10);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            DBGSERIAL.print("[OTA] Error al iniciar: ");
            Update.printError(DBGSERIAL);
            g_ota_cancel = true;    // descongelar la pantalla (antes quedaba congelada hasta reiniciar)
        }

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!s_ota_authed) return;
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            DBGSERIAL.print("[OTA] Error escribiendo: ");
            Update.printError(DBGSERIAL);
        }

    } else if (upload.status == UPLOAD_FILE_END) {
        if (!s_ota_authed) return;
        if (Update.end(true)) {
            DBGSERIAL.printf("[OTA] Completado: %u bytes. Reiniciando.\n",
                          upload.totalSize);
        } else {
            DBGSERIAL.print("[OTA] Error al finalizar: ");
            Update.printError(DBGSERIAL);
            g_ota_cancel = true;    // descongelar la pantalla
            server.send(500, "text/plain",
                        String("Error OTA: ") + Update.errorString());
        }
    }
}

// ── HTML escape: sustituye &, <, >, ", ' para evitar XSS en atributos ────
static String html_escape(const char* s) {
    String out;
    while (*s) {
        switch (*s) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += *s;       break;
        }
        ++s;
    }
    return out;
}

static String sanitize_mdns(const String& s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') out += c;
    }
    while (out.length() && out[0] == '-') out.remove(0, 1);
    while (out.length() && out[out.length()-1] == '-') out.remove(out.length()-1);
    return out;
}

static bool is_valid_ipv4(const String& s) {
    int octets = 0;
    int val = 0;
    int digits = 0;
    for (size_t i = 0; i <= s.length(); i++) {
        char c = (i < s.length()) ? s[i] : '.';
        if (c == '.') {
            if (digits == 0 || val > 255) return false;
            octets++;
            val = 0;
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            digits++;
            if (digits > 3) return false;
        } else {
            return false;
        }
    }
    return octets == 4;
}

// ═════════════════════════════════════════════════════════════════════════
// Ruta GET /admin  →  Panel de administración (protegido por contraseña)
// ═════════════════════════════════════════════════════════════════════════
static void handle_admin_get() {
    if (!check_auth()) return;

    AppConfig       cfg   = {}; Storage.loadConfig(cfg);
    ChartConfig     ccfg  = Storage.loadChartConfig();
    TelegramConfig  tgcfg = Storage.loadTelegramConfig();
    BacklightConfig blcfg = Storage.loadBacklightConfig();
    bool saved = server.hasArg("saved");

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.sendHeader("Content-Type", "text/html; charset=utf-8");
    server.send(200);

    // ── HEAD + CSS + JS ───────────────────────────────────────────────────
    server.sendContent(R"=EOF=(<!DOCTYPE html><html lang="es"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Admin – Deye Monitor</title>
<style>
:root{--bg:#0d1117;--card:#161b22;--accent:#4a9eff;--ok:#2ecc71;
      --warn:#f5c518;--muted:#6e7681;--white:#eaeaea;--border:#21262d}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--white);font-family:system-ui,sans-serif;
     padding:16px;max-width:700px;margin:0 auto 60px}
h1{font-size:1.05rem;color:var(--accent);margin-bottom:14px;text-align:center}
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;
      padding:14px;margin-bottom:10px}
.card h2{font-size:.72rem;text-transform:uppercase;letter-spacing:.08em;
         color:var(--accent);margin-bottom:12px}
.row{display:flex;align-items:center;gap:8px;margin-bottom:9px}
.lbl{min-width:140px;font-size:.8rem;color:var(--muted);flex-shrink:0}
input[type=text],input[type=number]{background:#21262d;border:1px solid var(--border);
                 border-radius:6px;color:var(--white);padding:6px 10px;font-size:.85rem;flex:1}
input[type=range]{flex:1;accent-color:var(--accent)}
output{min-width:52px;font-size:.8rem;color:var(--white);font-family:monospace}
.cb-row{display:flex;align-items:center;gap:8px;margin-bottom:8px}
.cb-row label{font-size:.85rem;color:var(--white)}
input[type=checkbox]{accent-color:var(--accent);width:15px;height:15px}
.info{font-size:.8rem;color:var(--muted)}
.banner{padding:9px 13px;border-radius:8px;margin-bottom:12px;font-size:.85rem}
.ok-b{background:#1a3a2a;border:1px solid var(--ok);color:var(--ok)}
.actions{display:flex;gap:10px;margin-top:14px}
.btn{border:none;border-radius:6px;padding:9px 18px;font-size:.9rem;
     cursor:pointer;flex:1;font-weight:600}
.btn-save{background:#1f6feb;color:#fff}.btn-save:hover{background:#388bfd}
.btn-rst{background:#2d1f0e;color:#f5c518;border:1px solid #5a3e10}
.btn-danger{background:#2d0e0e;color:#e74c3c;border:1px solid #5a1010}
a.back{color:var(--accent);font-size:.8rem;display:block;text-align:center;
       margin-top:14px;text-decoration:none}
</style>
<script>
function fmtPct(v){return(+v)+'%';}
function fmtKW(v){return(+v)+' kW';}
function fmtS10(v){return(+v*10)+'s';}
function fmtH(v){v=+v;return(v<10?'0':'')+v+'h';}
</script>
</head><body>)=EOF=");

    // ── Helpers locales ──────────────────────────────────────────────────
    // Envía un campo de rango: etiqueta, nombre, min, max, valor actual, fn formato, unidad
    auto send_range = [](const char* lbl, const char* name,
                         int mn, int mx, int val, const char* fn,
                         int disp, const char* unit) {
        server.sendContent("<div class='row'><span class='lbl'>");
        server.sendContent(lbl);
        server.sendContent("</span><input type='range' name='");
        server.sendContent(name);
        char b[40]; snprintf(b, sizeof(b), "' min='%d' max='%d' value='%d'", mn, mx, val);
        server.sendContent(b);
        server.sendContent(" oninput='this.nextElementSibling.textContent=");
        server.sendContent(fn);
        server.sendContent("(this.value)'><output>");
        char v[12]; snprintf(v, sizeof(v), "%d%s", disp, unit);
        server.sendContent(v);
        server.sendContent("</output></div>");
    };
    // Envía un checkbox
    auto send_cb = [](const char* name, const char* id,
                      bool chk, const char* lbl) {
        server.sendContent("<div class='cb-row'><input type='checkbox' name='");
        server.sendContent(name);
        server.sendContent("' id='");
        server.sendContent(id);
        server.sendContent("'");
        if (chk) server.sendContent(" checked");
        server.sendContent("><label for='");
        server.sendContent(id);
        server.sendContent("'>");
        server.sendContent(lbl);
        server.sendContent("</label></div>");
    };

    // ── Título + banner ───────────────────────────────────────────────────
    server.sendContent("<h1>&#9881; Administraci&oacute;n &ndash; Deye Monitor</h1>");
    if (saved)
        server.sendContent("<div class='banner ok-b'>&#10003; Configuraci&oacute;n guardada.</div>");

    server.sendContent("<form method='post' action='/admin'>");

    // ── Red ───────────────────────────────────────────────────────────────
    {
        String _s = "<div class='card'><h2>&#128267; Red</h2>"
                    "<p class='info'>WiFi: <strong style='color:var(--white)'>";
        _s += html_escape(cfg.wifi_ssid);
        _s += "</strong> &mdash; Configurable desde la pantalla t&aacute;ctil.</p>"
              "<div class='row'><span class='lbl'>Nombre mDNS</span>"
              "<input type='text' name='mdns_host' value='";
        _s += html_escape(cfg.mdns_hostname);
        _s += "' maxlength='31' placeholder='inversor'></div>"
              "<p class='info'>Accede al dispositivo en la red como <strong style='color:var(--white)'>";
        _s += html_escape(cfg.mdns_hostname);
        _s += "</strong>.local &mdash; Requiere reinicio para aplicar.</p></div>";
        server.sendContent(_s);
    }

    // ── Inversor ──────────────────────────────────────────────────────────
    {
        String _s = "<div class='card'><h2>&#128268; Inversor / Datalogger</h2>"
                    "<div class='row'><span class='lbl'>IP Logger</span>"
                    "<input type='text' name='logger_ip' value='";
        _s += html_escape(cfg.logger_ip);
        _s += "' maxlength='23'></div>"
              "<div class='row'><span class='lbl'>N&ordm; Serie (decimal)</span>"
              "<input type='text' name='logger_serial' value='";
        char _b[12]; snprintf(_b, sizeof(_b), "%lu", (unsigned long)cfg.logger_serial);
        _s += _b;
        _s += "' maxlength='15'></div>";
        char _n[480];
        snprintf(_n, sizeof(_n),
            "<div class='row'><span class='lbl'>Inv. m&aacute;x. W</span>"
            "<input type='number' name='inv_max_w' min='1' max='65535' value='%u'></div>"
            "<div class='row'><span class='lbl'>Red m&aacute;x. W</span>"
            "<input type='number' name='grid_max_w' min='1' max='65535' value='%u'></div>"
            "<div class='row'><span class='lbl'>Cap. bat. Wh</span>"
            "<input type='number' name='bat_cap_w' min='1' max='65535' value='%u'></div>"
            "</div>",
            (unsigned)cfg.inv_max_w, (unsigned)cfg.grid_max_w, (unsigned)cfg.bat_cap_w);
        _s += _n;
        server.sendContent(_s);
    }

    // ── Gráfica ───────────────────────────────────────────────────────────
    server.sendContent("<div class='card'><h2>&#128200; Gr&aacute;fica</h2>");
    send_cb("ch_auto", "cha", ccfg.autoscale, "Autoescalado");
    send_range("M&aacute;x kW", "ch_kw", 1, 20, ccfg.max_kw, "fmtKW",
               ccfg.max_kw, " kW");
    server.sendContent("</div>");

    // ── Telegram ──────────────────────────────────────────────────────────
    {
        String _s = "<div class='card'><h2>&#128276; Telegram</h2>"
                    "<div class='row'><span class='lbl'>Bot Token</span>"
                    "<input type='text' name='tg_token' value='";
        _s += html_escape(tgcfg.token);
        _s += "' maxlength='63'></div>"
              "<div class='row'><span class='lbl'>Chat ID</span>"
              "<input type='text' name='tg_chatid' value='";
        _s += html_escape(tgcfg.chat_id);
        _s += "' maxlength='31'></div>";
        server.sendContent(_s);
    }
    send_range("Aviso bater&iacute;a", "tg_bwarn", 5, 95,
               tgcfg.batt_warn, "fmtPct",
               tgcfg.batt_warn, "%");
    send_range("Alerta cr&iacute;tica", "tg_batt", 5, 50,
               tgcfg.batt_threshold, "fmtPct",
               tgcfg.batt_threshold, "%");
    send_cb("tg_solar", "tgs", tgcfg.notify_solar,
            "Notificar inicio/fin solar");
    send_cb("tg_grid",  "tgg", tgcfg.notify_grid,
            "Notificar corte/restauraci&oacute;n red");
    send_cb("tg_log",   "tgl", tgcfg.notify_logger,
            "Notificar fallo logger");
    server.sendContent("</div>");

    // ── Pantalla ──────────────────────────────────────────────────────────
    server.sendContent("<div class='card'><h2>&#128261; Pantalla</h2>");
    send_range("Brillo operaci\xC3\xB3n", "bl_op",   10, 100,
               blcfg.op_pct,      "fmtPct", blcfg.op_pct,      "%");
    send_range("Brillo diurno",   "bl_norm",   0, 100,
               blcfg.normal_pct,  "fmtPct", blcfg.normal_pct,  "%");
    send_range("Brillo reducido", "bl_red",    0, 100,
               blcfg.reduced_pct, "fmtPct", blcfg.reduced_pct, "%");
    send_cb("bl_inact", "bli", blcfg.inactivity_enabled,
            "Reducir brillo con inactividad");
    send_range("Tiempo espera", "bl_isecs", 1, 18,
               blcfg.inactivity_div10, "fmtS10",
               blcfg.inactivity_div10 * 10, "s");
    send_cb("bl_night", "bln", blcfg.night_enabled,
            "Horario nocturno");
    // Inicio y fin de noche usan fmtH (padding con cero)
    server.sendContent("<div class='row'><span class='lbl'>Inicio noche</span>"
        "<input type='range' name='bl_nstart' min='0' max='23' value='");
    { char b[4]; snprintf(b, sizeof(b), "%d", (int)blcfg.night_start_h);
      server.sendContent(b); }
    server.sendContent("' oninput='this.nextElementSibling.textContent=fmtH(this.value)'>"
        "<output>");
    { char b[6]; snprintf(b, sizeof(b), "%02dh", (int)blcfg.night_start_h);
      server.sendContent(b); }
    server.sendContent("</output></div>"
        "<div class='row'><span class='lbl'>Fin noche</span>"
        "<input type='range' name='bl_nend' min='0' max='23' value='");
    { char b[4]; snprintf(b, sizeof(b), "%d", (int)blcfg.night_end_h);
      server.sendContent(b); }
    server.sendContent("' oninput='this.nextElementSibling.textContent=fmtH(this.value)'>"
        "<output>");
    { char b[6]; snprintf(b, sizeof(b), "%02dh", (int)blcfg.night_end_h);
      server.sendContent(b); }
    server.sendContent("</output></div>");
    // DEYE-54: reloj grande cuando la pantalla está en reposo
    send_cb("clock_mode", "clk", clock_mode_enabled(), "Reloj en reposo");
    server.sendContent("</div>");

    // ── Botón guardar ─────────────────────────────────────────────────────
    server.sendContent("<div class='actions'>"
        "<button type='submit' class='btn btn-save'>&#128190; Guardar cambios</button>"
        "</div></form>");

    // ── Copia de seguridad de la configuración ──────────────────────────────
    server.sendContent(
        "<div class='card'><h2>&#128190; Copia de seguridad</h2>"
        "<p class='info'>Incluye WiFi, inversor, Telegram y pantalla — "
        "&uacute;til antes de una actualizaci&oacute;n o al sustituir el "
        "dispositivo. El fichero contiene credenciales en claro: gu&aacute;rdalo "
        "en un sitio seguro. La contrase&ntilde;a de este panel no se incluye "
        "(solo se cambia desde la pantalla t&aacute;ctil).</p>"
        "<div class='actions'>"
        "<a class='btn btn-save' style='text-decoration:none;text-align:center' "
        "href='/api/config/export'>&#11015; Descargar copia</a>"
        "</div>"
        "<div class='row' style='margin-top:10px'>"
        "<input type='file' id='cfg_file' accept='application/json' style='flex:1'>"
        "</div>"
        "<div class='actions'>"
        "<button type='button' class='btn btn-rst' onclick='importConfig()'>"
        "&#11014; Restaurar copia</button>"
        "</div>"
        "<p class='info' id='cfg_import_msg'></p></div>"
        "<script>"
        "function importConfig(){"
        "var f=document.getElementById('cfg_file').files[0];"
        "if(!f){alert('Elige primero un fichero .json');return;}"
        "if(!confirm('Esto sobrescribe la configuración actual (excepto la "
        "contraseña de este panel). ¿Continuar?'))return;"
        "var r=new FileReader();"
        "r.onload=function(){"
        "fetch('/api/config/import',{method:'POST',body:r.result})"
        ".then(function(res){"
        "var m=document.getElementById('cfg_import_msg');"
        "if(res.ok){m.textContent='Restaurada. Recarga la página para ver los "
        "valores nuevos.';m.style.color='var(--ok)';}"
        "else{res.text().then(function(t){m.textContent='Error: '+t;"
        "m.style.color='#e74c3c';});}"
        "});"
        "};"
        "r.readAsText(f);"
        "}"
        "</script>");

    // ── Copia de seguridad de datos históricos ──────────────────────────────
    server.sendContent(
        "<div class='card'><h2>&#128194; Copia de seguridad de datos hist&oacute;ricos</h2>"
        "<p class='info'>Descarga TODO el historial registrado en flash (medidas "
        "de 5 min, horarias y diarias) para restaurarlo en otro dispositivo con "
        "el mismo firmware. Solo compatible entre dispositivos con la misma "
        "versi&oacute;n de formato de almacenamiento.</p>"
        "<div class='actions'>"
        "<a class='btn btn-save' style='text-decoration:none;text-align:center' "
        "href='/api/data/export'>&#11015; Descargar datos</a>"
        "</div>"
        "<div class='row' style='margin-top:10px'>"
        "<input type='file' id='data_file' accept='.bin,application/octet-stream' "
        "style='flex:1'>"
        "</div>"
        "<div id='data_progress' style='display:none;width:100%;background:#21262d;"
        "border-radius:6px;height:10px;margin-top:8px'>"
        "<div id='data_bar' style='height:10px;border-radius:6px;"
        "background:#1f6feb;width:0%'></div></div>"
        "<div class='actions'>"
        "<button type='button' class='btn btn-rst' onclick='importData()'>"
        "&#11014; Restaurar datos</button>"
        "</div>"
        "<p class='info' id='data_import_msg'></p></div>"
        "<script>"
        "function importData(){"
        "var f=document.getElementById('data_file').files[0];"
        "if(!f){alert('Elige primero un fichero .bin');return;}"
        "if(!confirm('Esto SUSTITUYE TODO el historial actual por el del "
        "fichero, y el dispositivo se reiniciará para aplicarlo. Esta acción "
        "no se puede deshacer. ¿Continuar?'))return;"
        "var m=document.getElementById('data_import_msg');"
        "var prog=document.getElementById('data_progress'),"
        "bar=document.getElementById('data_bar');"
        "prog.style.display='block';"
        "var fd=new FormData();fd.append('data',f);"
        "var xhr=new XMLHttpRequest();"
        "xhr.open('POST','/api/data/import');"
        "xhr.upload.onprogress=function(e){"
        "if(e.lengthComputable)bar.style.width=Math.round(e.loaded/e.total*100)+'%';"
        "};"
        "xhr.onload=function(){"
        "if(xhr.status===200){m.textContent='Restaurando y reiniciando el "
        "dispositivo…';m.style.color='var(--ok)';}"
        "else{m.textContent='Error: '+xhr.responseText;m.style.color='#e74c3c';}"
        "};"
        "xhr.onerror=function(){m.textContent='Error de red.';"
        "m.style.color='#e74c3c';};"
        "xhr.send(fd);"
        "}"
        "</script>");

    // ── Zona de peligro: borrar todo el historial ───────────────────────────
    // Permite dejar sitio para importar una copia completa en un dispositivo
    // que ya tiene datos propios (evita el problema de espacio duplicado que
    // tendría fusionar dos historiales de 730 días en la misma flash).
    server.sendContent(
        "<div class='card'><h2>&#9888; Borrar historial</h2>"
        "<p class='info'>Elimina TODAS las medidas de 5 min, horarias y diarias "
        "guardadas en este dispositivo. &Uacute;til para dejar sitio antes de "
        "importar una copia completa de otro dispositivo. La configuraci&oacute;n "
        "(WiFi, inversor, Telegram...) no se ve afectada. Esta acci&oacute;n no "
        "se puede deshacer.</p>"
        "<form method='post' action='/api/data/wipe' "
        // DEYE-58: acción irreversible → confirmación en DOS pasos
        "onsubmit=\"return confirm('Esto BORRA TODO el historial registrado en "
        "este dispositivo de forma permanente. ¿Continuar?') &amp;&amp; "
        "confirm('CONFIRMACIÓN FINAL: el historial no se podrá recuperar. "
        "¿Borrarlo definitivamente?');\">"
        "<div class='actions'>"
        "<button type='submit' class='btn btn-danger'>&#128465; Borrar todo el "
        "historial</button>"
        "</div></form></div>");

    // ── Reiniciar (formulario independiente, con confirmación) ─────────────
    server.sendContent("<form method='post' action='/api/restart' "
        "onsubmit=\"return confirm('¿Reiniciar el dispositivo ahora?');\">"
        "<div class='actions'>"
        "<button type='submit' class='btn btn-rst'>&#9851; Reiniciar dispositivo</button>"
        "</div></form>");

    server.sendContent("<a class='back' href='/'>&#8592; Volver al dashboard</a>"
        "</body></html>");
}

// ═════════════════════════════════════════════════════════════════════════
// Ruta POST /admin  →  Guarda la configuración recibida del formulario
// ═════════════════════════════════════════════════════════════════════════
static void handle_admin_post() {
    if (!check_auth()) return;
    if (!check_same_origin()) return;

    // ── Red ───────────────────────────────────────────────────────────────
    AppConfig cfg{}; Storage.loadConfig(cfg);   // preservar WiFi y mDNS
    String mdns = sanitize_mdns(server.arg("mdns_host"));
    if (mdns.length() > 0 && mdns.length() < sizeof(cfg.mdns_hostname))
        mdns.toCharArray(cfg.mdns_hostname, sizeof(cfg.mdns_hostname));

    // ── Inversor ──────────────────────────────────────────────────────────
    String lip = server.arg("logger_ip");
    if (lip.length() > 0 && lip.length() < sizeof(cfg.logger_ip) && is_valid_ipv4(lip))
        lip.toCharArray(cfg.logger_ip, sizeof(cfg.logger_ip));
    String lser = server.arg("logger_serial");
    if (!lser.isEmpty())
        cfg.logger_serial = (uint32_t)strtoul(lser.c_str(), nullptr, 10);
    { int v = server.arg("inv_max_w").toInt();  if (v >= 1 && v <= 65535) cfg.inv_max_w  = (uint16_t)v; }
    { int v = server.arg("grid_max_w").toInt(); if (v >= 1 && v <= 65535) cfg.grid_max_w = (uint16_t)v; }
    { int v = server.arg("bat_cap_w").toInt();  if (v >= 1 && v <= 65535) cfg.bat_cap_w  = (uint16_t)v; }

    // ── Gráfica ───────────────────────────────────────────────────────────
    ChartConfig ccfg{};
    ccfg.autoscale = server.hasArg("ch_auto");
    int ch_kw = server.arg("ch_kw").toInt();
    ccfg.max_kw = (uint8_t)(ch_kw < 1 ? 1 : ch_kw > 20 ? 20 : ch_kw);

    // ── Telegram ──────────────────────────────────────────────────────────
    TelegramConfig tgcfg{};
    server.arg("tg_token") .toCharArray(tgcfg.token,   sizeof(tgcfg.token)   - 1);
    server.arg("tg_chatid").toCharArray(tgcfg.chat_id, sizeof(tgcfg.chat_id) - 1);
    int tw = server.arg("tg_bwarn").toInt();
    tgcfg.batt_warn      = (uint8_t)(tw < 5 ? 5 : tw > 95 ? 95 : tw);
    int tb = server.arg("tg_batt").toInt();
    tgcfg.batt_threshold = (uint8_t)(tb < 5 ? 5 : tb > 50 ? 50 : tb);
    tgcfg.notify_solar   = server.hasArg("tg_solar");
    tgcfg.notify_grid    = server.hasArg("tg_grid");
    tgcfg.notify_logger  = server.hasArg("tg_log");

    // ── Pantalla ──────────────────────────────────────────────────────────
    auto clamp = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
    BacklightConfig blcfg{};
    blcfg.op_pct             = (uint8_t)clamp(server.arg("bl_op").toInt(),     10, 100);
    blcfg.normal_pct         = (uint8_t)clamp(server.arg("bl_norm").toInt(),    0, 100);
    blcfg.reduced_pct        = (uint8_t)clamp(server.arg("bl_red").toInt(),     0, 100);
    blcfg.inactivity_enabled = server.hasArg("bl_inact");
    blcfg.inactivity_div10   = (uint8_t)clamp(server.arg("bl_isecs").toInt(),   1,  18);
    blcfg.night_enabled      = server.hasArg("bl_night");
    blcfg.night_start_h      = (uint8_t)clamp(server.arg("bl_nstart").toInt(),  0,  23);
    blcfg.night_end_h        = (uint8_t)clamp(server.arg("bl_nend").toInt(),    0,  23);

    // ── Persistir ─────────────────────────────────────────────────────────
    Storage.saveConfig(cfg);
    Storage.saveChartConfig(ccfg);
    Storage.saveTelegramConfig(tgcfg);
    Storage.saveBacklightConfig(blcfg);
    clock_mode_set_enabled(server.hasArg("clock_mode"));

    server.sendHeader("Location", "/admin?saved=1");
    server.send(303, "text/plain", "Guardado");
}

// ═════════════════════════════════════════════════════════════════════════
// GET /api/config/export → descarga toda la configuración en un JSON
// POST /api/config/import → restaura la configuración desde ese JSON
//
// La contraseña de administración (web_pass) se excluye deliberadamente:
// solo puede establecerse desde la pantalla táctil (ver check_auth arriba),
// y permitir restaurarla desde la web rompería esa garantía.
// ═════════════════════════════════════════════════════════════════════════
static void handle_config_export() {
    if (!check_auth()) return;

    AppConfig       cfg   = {}; Storage.loadConfig(cfg);
    ChartConfig     ccfg  = Storage.loadChartConfig();
    TelegramConfig  tgcfg = Storage.loadTelegramConfig();
    BacklightConfig blcfg = Storage.loadBacklightConfig();

    JsonDocument doc;
    doc["config_version"]      = 1;
    doc["wifi_ssid"]           = cfg.wifi_ssid;
    doc["wifi_pass"]           = cfg.wifi_pass;
    doc["logger_ip"]           = cfg.logger_ip;
    doc["logger_serial"]       = cfg.logger_serial;
    doc["mdns_hostname"]       = cfg.mdns_hostname;
    doc["inv_max_w"]           = cfg.inv_max_w;
    doc["grid_max_w"]          = cfg.grid_max_w;
    doc["bat_cap_w"]           = cfg.bat_cap_w;
    doc["chart_autoscale"]     = ccfg.autoscale;
    doc["chart_max_kw"]        = ccfg.max_kw;
    doc["tg_token"]            = tgcfg.token;
    doc["tg_chat_id"]          = tgcfg.chat_id;
    doc["tg_batt_threshold"]   = tgcfg.batt_threshold;
    doc["tg_batt_warn"]        = tgcfg.batt_warn;
    doc["tg_notify_solar"]     = tgcfg.notify_solar;
    doc["tg_notify_grid"]      = tgcfg.notify_grid;
    doc["tg_notify_logger"]    = tgcfg.notify_logger;
    doc["bl_op_pct"]           = blcfg.op_pct;
    doc["bl_normal_pct"]       = blcfg.normal_pct;
    doc["bl_reduced_pct"]      = blcfg.reduced_pct;
    doc["bl_inactivity_enabled"] = blcfg.inactivity_enabled;
    doc["bl_inactivity_div10"] = blcfg.inactivity_div10;
    doc["bl_night_enabled"]    = blcfg.night_enabled;
    doc["bl_night_start_h"]    = blcfg.night_start_h;
    doc["bl_night_end_h"]      = blcfg.night_end_h;
    doc["ui_clock_mode"]       = clock_mode_enabled();

    String out;
    serializeJsonPretty(doc, out);

    server.sendHeader("Content-Disposition",
                      "attachment; filename=\"deye_config_backup.json\"");
    server.send(200, "application/json", out);
}

static void handle_config_import() {
    if (!check_auth()) return;
    if (!check_same_origin()) return;

    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Falta el cuerpo JSON");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        server.send(400, "text/plain", String("JSON inválido: ") + err.c_str());
        return;
    }

    // ── Red / inversor (mismas validaciones que handle_admin_post) ─────────
    AppConfig cfg{}; Storage.loadConfig(cfg);
    if (doc["wifi_ssid"].is<const char*>())
        strncpy(cfg.wifi_ssid, doc["wifi_ssid"] | "", sizeof(cfg.wifi_ssid) - 1);
    if (doc["wifi_pass"].is<const char*>())
        strncpy(cfg.wifi_pass, doc["wifi_pass"] | "", sizeof(cfg.wifi_pass) - 1);
    if (doc["mdns_hostname"].is<const char*>()) {
        String mdns = sanitize_mdns(String((const char*)(doc["mdns_hostname"] | "")));
        if (mdns.length() > 0 && mdns.length() < sizeof(cfg.mdns_hostname))
            mdns.toCharArray(cfg.mdns_hostname, sizeof(cfg.mdns_hostname));
    }
    if (doc["logger_ip"].is<const char*>()) {
        String lip = doc["logger_ip"] | "";
        if (lip.length() > 0 && lip.length() < sizeof(cfg.logger_ip) && is_valid_ipv4(lip))
            lip.toCharArray(cfg.logger_ip, sizeof(cfg.logger_ip));
    }
    if (doc["logger_serial"].is<uint32_t>()) cfg.logger_serial = doc["logger_serial"];
    if (doc["inv_max_w"].is<int>()) {
        int v = doc["inv_max_w"]; if (v >= 1 && v <= 65535) cfg.inv_max_w = (uint16_t)v;
    }
    if (doc["grid_max_w"].is<int>()) {
        int v = doc["grid_max_w"]; if (v >= 1 && v <= 65535) cfg.grid_max_w = (uint16_t)v;
    }
    if (doc["bat_cap_w"].is<int>()) {
        int v = doc["bat_cap_w"]; if (v >= 1 && v <= 65535) cfg.bat_cap_w = (uint16_t)v;
    }

    // ── Gráfica ───────────────────────────────────────────────────────────
    ChartConfig ccfg = Storage.loadChartConfig();
    if (doc["chart_autoscale"].is<bool>()) ccfg.autoscale = doc["chart_autoscale"];
    if (doc["chart_max_kw"].is<int>()) {
        int v = doc["chart_max_kw"];
        ccfg.max_kw = (uint8_t)(v < 1 ? 1 : v > 20 ? 20 : v);
    }

    // ── Telegram ──────────────────────────────────────────────────────────
    TelegramConfig tgcfg = Storage.loadTelegramConfig();
    if (doc["tg_token"].is<const char*>())
        strncpy(tgcfg.token, doc["tg_token"] | "", sizeof(tgcfg.token) - 1);
    if (doc["tg_chat_id"].is<const char*>())
        strncpy(tgcfg.chat_id, doc["tg_chat_id"] | "", sizeof(tgcfg.chat_id) - 1);
    if (doc["tg_batt_warn"].is<int>()) {
        int v = doc["tg_batt_warn"]; tgcfg.batt_warn = (uint8_t)(v < 5 ? 5 : v > 95 ? 95 : v);
    }
    if (doc["tg_batt_threshold"].is<int>()) {
        int v = doc["tg_batt_threshold"]; tgcfg.batt_threshold = (uint8_t)(v < 5 ? 5 : v > 50 ? 50 : v);
    }
    if (doc["tg_notify_solar"].is<bool>())  tgcfg.notify_solar  = doc["tg_notify_solar"];
    if (doc["tg_notify_grid"].is<bool>())   tgcfg.notify_grid   = doc["tg_notify_grid"];
    if (doc["tg_notify_logger"].is<bool>()) tgcfg.notify_logger = doc["tg_notify_logger"];

    // ── Pantalla ──────────────────────────────────────────────────────────
    auto clamp = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
    BacklightConfig blcfg = Storage.loadBacklightConfig();
    if (doc["bl_op_pct"].is<int>())      blcfg.op_pct      = (uint8_t)clamp(doc["bl_op_pct"],      10, 100);
    if (doc["bl_normal_pct"].is<int>())  blcfg.normal_pct  = (uint8_t)clamp(doc["bl_normal_pct"],   0, 100);
    if (doc["bl_reduced_pct"].is<int>()) blcfg.reduced_pct = (uint8_t)clamp(doc["bl_reduced_pct"],  0, 100);
    if (doc["bl_inactivity_enabled"].is<bool>()) blcfg.inactivity_enabled = doc["bl_inactivity_enabled"];
    if (doc["bl_inactivity_div10"].is<int>())
        blcfg.inactivity_div10 = (uint8_t)clamp(doc["bl_inactivity_div10"], 1, 18);
    if (doc["bl_night_enabled"].is<bool>()) blcfg.night_enabled = doc["bl_night_enabled"];
    if (doc["bl_night_start_h"].is<int>())  blcfg.night_start_h = (uint8_t)clamp(doc["bl_night_start_h"], 0, 23);
    if (doc["bl_night_end_h"].is<int>())    blcfg.night_end_h   = (uint8_t)clamp(doc["bl_night_end_h"],   0, 23);

    // ── Persistir ─────────────────────────────────────────────────────────
    Storage.saveConfig(cfg);
    Storage.saveChartConfig(ccfg);
    Storage.saveTelegramConfig(tgcfg);
    Storage.saveBacklightConfig(blcfg);
    if (doc["ui_clock_mode"].is<bool>()) clock_mode_set_enabled(doc["ui_clock_mode"]);

    server.send(200, "text/plain", "OK");
}

// ═════════════════════════════════════════════════════════════════════════
// GET /api/data/export  → descarga TODO el histórico (raw/hourly/daily) en
//                          un fichero binario, para restaurarlo en otro
//                          dispositivo con /api/data/import
//
// Formato del contenedor (todo little-endian, como el resto del firmware):
//   [4B]  magic del contenedor "DYB1"
//   [4B]  DataStore::MAGIC del dispositivo origen
//   [1B]  DataStore::VERSION del dispositivo origen
//   [4B]  capacidad raw   (registros)
//   [4B]  capacidad hourly
//   [4B]  capacidad daily
//   Después, 5 veces (mismo orden que DataStore::getBackupPaths):
//     [4B tamaño][tamaño bytes] — meta2.bin, day_idx.bin, raw.bin, hrly.bin, day.bin
//
// Se transfieren los ficheros tal cual (no un dump reinterpretado registro a
// registro): más rápido, no requiere miles de escrituras individuales en el
// dispositivo destino, y preserva el formato exacto. Por eso el destino debe
// tener la misma versión de formato y capacidad — se valida en la importación
// antes de escribir nada (ver DataStore::isCompatibleBackup).
// ═════════════════════════════════════════════════════════════════════════
static const uint32_t DATA_BACKUP_MAGIC = 0x31425944;  // "DYB1" little-endian

static void handle_data_export() {
    if (!check_auth()) return;

    const char* paths[DataStore::BACKUP_FILE_COUNT];
    Store.getBackupPaths(paths);

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.sendHeader("Content-Type", "application/octet-stream");
    server.sendHeader("Content-Disposition",
                      "attachment; filename=\"deye_data_backup.bin\"");
    server.send(200);

    uint32_t container_magic = DATA_BACKUP_MAGIC;
    uint32_t ds_magic   = Store.getFormatMagic();
    uint8_t  ds_version = Store.getFormatVersion();
    uint32_t raw_cap    = Store.getRawCapacity();
    uint32_t hrly_cap   = Store.getHourlyCapacity();
    uint32_t day_cap    = Store.getDailyCapacity();

    uint8_t hdr[21];
    memcpy(hdr,      &container_magic, 4);
    memcpy(hdr + 4,  &ds_magic,        4);
    hdr[8] = ds_version;
    memcpy(hdr + 9,  &raw_cap,  4);
    memcpy(hdr + 13, &hrly_cap, 4);
    memcpy(hdr + 17, &day_cap,  4);
    server.sendContent((const char*)hdr, sizeof(hdr));

    uint8_t buf[1024];
    for (uint8_t i = 0; i < DataStore::BACKUP_FILE_COUNT; i++) {
        File f = LittleFS.open(paths[i], "r");
        uint32_t sz = f ? (uint32_t)f.size() : 0;
        server.sendContent((const char*)&sz, 4);
        while (f && f.available()) {
            size_t n = f.read(buf, sizeof(buf));
            if (n == 0) break;
            server.sendContent((const char*)buf, n);
        }
        if (f) f.close();
    }
}

// ═════════════════════════════════════════════════════════════════════════
// POST /api/data/import → sube un fichero generado por /api/data/export.
//
// Se escribe en ficheros temporales ("<ruta>.new") sin tocar los reales
// mientras el dispositivo sigue funcionando (DataStore mantiene los suyos
// abiertos permanentemente). Si todo se recibe y valida correctamente, se
// deja un marcador ("/data_import_pending") y se reinicia: el intercambio
// de ficheros ocurre en main.cpp, ANTES de Store.begin(), momento en el que
// ningún fichero está abierto y el intercambio es seguro.
// ═════════════════════════════════════════════════════════════════════════
enum class DataImportPhase : uint8_t { HEADER, SECTION_LEN, SECTION_DATA, DONE, ERR };

static bool            s_data_import_authed = false;
static DataImportPhase s_di_phase = DataImportPhase::HEADER;
static uint8_t         s_di_scratch[21];
static uint16_t        s_di_scratch_len = 0;
static uint8_t         s_di_section_idx = 0;
static uint32_t        s_di_section_remaining = 0;
static File             s_di_file;
static const char*     s_di_error = nullptr;

static String di_temp_path(uint8_t idx) {
    const char* paths[DataStore::BACKUP_FILE_COUNT];
    Store.getBackupPaths(paths);
    return String(paths[idx]) + ".new";
}

static void di_cleanup_temp_files() {
    for (uint8_t i = 0; i < DataStore::BACKUP_FILE_COUNT; i++)
        LittleFS.remove(di_temp_path(i));
}

static void di_reset() {
    s_di_phase = DataImportPhase::HEADER;
    s_di_scratch_len = 0;
    s_di_section_idx = 0;
    s_di_section_remaining = 0;
    s_di_error = nullptr;
    if (s_di_file) s_di_file.close();
    di_cleanup_temp_files();   // por si quedó algo de un intento anterior fallido
}

static void di_fail(const char* why) {
    DBGSERIAL.printf("[DataImport] Error: %s\n", why);
    s_di_error = why;
    s_di_phase = DataImportPhase::ERR;
    if (s_di_file) s_di_file.close();
}

static void di_process(const uint8_t* buf, size_t len) {
    if (s_di_phase == DataImportPhase::ERR || s_di_phase == DataImportPhase::DONE) return;

    size_t pos = 0;
    while (pos < len) {
        if (s_di_phase == DataImportPhase::HEADER) {
            size_t need = sizeof(s_di_scratch) - s_di_scratch_len;
            size_t take = min(need, len - pos);
            memcpy(s_di_scratch + s_di_scratch_len, buf + pos, take);
            s_di_scratch_len += take; pos += take;
            if (s_di_scratch_len < sizeof(s_di_scratch)) return;   // esperar más datos

            uint32_t container_magic, ds_magic, raw_cap, hrly_cap, day_cap;
            memcpy(&container_magic, s_di_scratch,      4);
            memcpy(&ds_magic,        s_di_scratch + 4,  4);
            uint8_t ds_version = s_di_scratch[8];
            memcpy(&raw_cap,  s_di_scratch + 9,  4);
            memcpy(&hrly_cap, s_di_scratch + 13, 4);
            memcpy(&day_cap,  s_di_scratch + 17, 4);

            if (container_magic != DATA_BACKUP_MAGIC) {
                di_fail("el fichero no es una copia de seguridad de datos válida");
                return;
            }
            if (!Store.isCompatibleBackup(ds_magic, ds_version, raw_cap, hrly_cap, day_cap)) {
                di_fail("copia de seguridad incompatible (versión de firmware distinta)");
                return;
            }
            s_di_scratch_len = 0;
            s_di_phase = DataImportPhase::SECTION_LEN;
            continue;
        }

        if (s_di_phase == DataImportPhase::SECTION_LEN) {
            if (s_di_section_idx >= DataStore::BACKUP_FILE_COUNT) {
                s_di_phase = DataImportPhase::DONE;
                return;
            }
            size_t need = 4 - s_di_scratch_len;
            size_t take = min(need, len - pos);
            memcpy(s_di_scratch + s_di_scratch_len, buf + pos, take);
            s_di_scratch_len += take; pos += take;
            if (s_di_scratch_len < 4) return;

            memcpy(&s_di_section_remaining, s_di_scratch, 4);
            s_di_scratch_len = 0;

            // Comprobación de espacio libre ANTES de escribir: los ficheros
            // ".new" conviven temporalmente con los reales hasta el
            // intercambio en el próximo arranque, así que hace falta espacio
            // libre adicional (no solo el que ya usan los ficheros actuales).
            // Margen de seguridad para las operaciones COW de LittleFS —
            // sin él, un historial casi lleno en ambos dispositivos puede
            // agotar la partición de 8 MB a mitad de la escritura.
            static constexpr uint32_t FREE_SPACE_MARGIN = 128UL * 1024;
            uint32_t free_space = LittleFS.totalBytes() - LittleFS.usedBytes();
            if (s_di_section_remaining + FREE_SPACE_MARGIN > free_space) {
                DBGSERIAL.printf("[DataImport] Sin espacio: necesita %lu B (+%lu margen), libres %lu B\n",
                              (unsigned long)s_di_section_remaining,
                              (unsigned long)FREE_SPACE_MARGIN,
                              (unsigned long)free_space);
                di_fail("espacio insuficiente en flash — borra historial existente antes de importar");
                return;
            }

            s_di_file = LittleFS.open(di_temp_path(s_di_section_idx), "w");
            if (!s_di_file) { di_fail("no se pudo crear fichero temporal"); return; }
            s_di_phase = DataImportPhase::SECTION_DATA;
            continue;
        }

        if (s_di_phase == DataImportPhase::SECTION_DATA) {
            if (s_di_section_remaining == 0) {
                if (s_di_file) s_di_file.close();
                s_di_section_idx++;
                s_di_phase = DataImportPhase::SECTION_LEN;
                continue;
            }
            size_t take = min((size_t)s_di_section_remaining, len - pos);
            if (s_di_file.write(buf + pos, take) != take) {
                di_fail("error escribiendo fichero temporal");
                return;
            }
            pos += take; s_di_section_remaining -= take;
            continue;
        }
    }
}

static void handle_data_import_upload() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        if (!same_origin_request()) {
            DBGSERIAL.println("[DataImport] Rechazado: Origin/Referer no coincide (posible CSRF)");
            s_data_import_authed = false;
            return;
        }
        String pw = Storage.loadAdminPassword();
        if (pw.isEmpty()) {
            s_data_import_authed = false;
        } else if (auth_is_locked_out()) {
            s_data_import_authed = false;
            auth_send_lockout_response();
            return;
        } else if (server.authenticate("admin", pw.c_str())) {
            auth_record_success();
            s_data_import_authed = true;
        } else {
            auth_record_failure();
            s_data_import_authed = false;
        }
        if (!s_data_import_authed) return;
        di_reset();

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!s_data_import_authed) return;
        di_process(upload.buf, upload.currentSize);
    }
    // UPLOAD_FILE_END: la validación final y la respuesta las hace
    // handle_data_import_post(), que se llama justo después.
}

static void handle_data_import_post() {
    if (!s_data_import_authed) {
        send_auth_denied_response();
        return;
    }
    if (s_di_phase != DataImportPhase::DONE) {
        di_cleanup_temp_files();
        server.send(400, "text/plain",
            String("Error: ") + (s_di_error ? s_di_error : "copia de seguridad incompleta"));
        return;
    }

    File marker = LittleFS.open("/data_import_pending", "w");
    if (marker) marker.close();

    DBGSERIAL.println("[DataImport] Copia de seguridad válida — reiniciando para aplicarla");
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
}

// ═════════════════════════════════════════════════════════════════════════
// Ruta POST /api/data/wipe  →  Borra todo el histórico y reinicia
// ═════════════════════════════════════════════════════════════════════════
// El borrado real se hace en main.cpp ANTES de Store.begin(), igual que la
// restauración de una copia de seguridad — en ese punto ningún fichero de
// datos está abierto todavía, así que borrar ahí es seguro.
static void handle_data_wipe() {
    if (!check_auth()) return;
    if (!check_same_origin()) return;

    File marker = LittleFS.open("/data_wipe_pending", "w");
    if (marker) marker.close();

    DBGSERIAL.println("[DataWipe] Borrado de historial solicitado — reiniciando para aplicarlo");
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
}

// ═════════════════════════════════════════════════════════════════════════
// Ruta POST /api/restart  →  Reinicia el dispositivo
// ═════════════════════════════════════════════════════════════════════════
static void handle_restart() {
    if (!check_auth()) return;
    if (!check_same_origin()) return;
    server.send(200, "text/html; charset=utf-8",
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='6;url=/'>"
        "<title>Reiniciando...</title></head>"
        "<body style='background:#0d1117;color:#eaeaea;font-family:system-ui;"
        "text-align:center;padding:40px'>"
        "<h2 style='color:#4a9eff'>Reiniciando dispositivo...</h2>"
        "<p style='color:#6e7681;margin-top:10px'>"
        "La p&aacute;gina se redirigir&aacute; al dashboard en 6 segundos.</p>"
        "</body></html>");
    Store.flush();
    delay(500);
    ESP.restart();
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

    // ── Daily: fuente primaria = fetchDailyStats() vía g_daily ──────────────
    // El caché solo tiene los totales grabados al cierre de cada hora,
    // por lo que al inicio del día está a cero aunque el inversor ya acumule.
    DailyStats d{};
    time_t now; time(&now);
    struct tm tm_now; localtime_r(&now, &tm_now);
    tm_now.tm_hour = 0; tm_now.tm_min = 0;
    tm_now.tm_sec  = 0; tm_now.tm_isdst = -1;
    uint32_t today_ep = (uint32_t)mktime(&tm_now);

    if (s_daily && s_mutex &&
        xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        d = *s_daily;
        xSemaphoreGive(s_mutex);
    }
    // Fallback a caché si todavía no hay datos vivos
    if (!d.valid) {
        DailyRecord dr{};
        if (Cache.getDaily(today_ep, dr)) d = daily_record_to_stats(dr);
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

    DBGSERIAL.printf("[API] daily valid=%d pv=%.2f load=%.2f cache_day=%lu\n",
          d.valid, d.pv_kwh, d.load_kwh,
          (unsigned long)today_ep);

    server.send(200, "application/json", json);
}

// ── Helper: epoch desde string YYYYMMDD ───────────────────────────────────
static uint32_t parse_date(const String& s) {
    if (s.length() != 8) return 0;
    for (int i = 0; i < 8; i++) if (!isdigit(s[i])) return 0;
    int y = s.substring(0,4).toInt();
    int m = s.substring(4,6).toInt();
    int d = s.substring(6,8).toInt();
    if (y < 2020 || y > 2150 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    struct tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d; tm.tm_isdst = -1;
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
    char json[768];
    snprintf(json, sizeof(json),
        "{\"version\":\"" APP_VERSION "\","
        "\"raw\":{\"count\":%lu,\"capacity\":%lu},"
        "\"hourly\":{\"count\":%lu,\"capacity\":%lu},"
        "\"daily\":{\"count\":%lu,\"capacity\":%lu},"
        "\"psram_free_kb\":%lu,"
        "\"uptime_s\":%lu,\"heap_free_kb\":%lu,\"heap_min_kb\":%lu,"
        "\"fs_total_kb\":%lu,\"fs_used_kb\":%lu,"
        "\"wifi_rssi\":%d,\"ip\":\"%s\"}",
        (unsigned long)Store.getRawCount(),
        (unsigned long)Store.getRawCapacity(),
        (unsigned long)Store.getHourlyCount(),
        (unsigned long)Store.getHourlyCapacity(),
        (unsigned long)Store.getDailyCount(),
        (unsigned long)Store.getDailyCapacity(),
        (unsigned long)ESP.getFreePsram() / 1024,
        (unsigned long)((millis() - g_uptime_start) / 1000),
        (unsigned long)ESP.getFreeHeap() / 1024,
        (unsigned long)ESP.getMinFreeHeap() / 1024,
        (unsigned long)LittleFS.totalBytes() / 1024,
        (unsigned long)LittleFS.usedBytes() / 1024,
        (int)WiFi.RSSI(),
        WiFi.localIP().toString().c_str());

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
    server.send(200, "application/json", json);
}

// ── GET /api/alerts?since_id=N ────────────────────────────────────────────
// Alertas del sistema (batería, solar, red, logger) desde el id indicado.
// El dashboard lo sondea para mostrar notificaciones del navegador mientras
// la pestaña está abierta (ver WebAlerts en web_alerts.h) — no requiere
// autenticación, mismo nivel de exposición que /api/data.
static void handle_alerts() {
    uint32_t since_id = 0;
    if (server.hasArg("since_id")) since_id = (uint32_t)server.arg("since_id").toInt();

    WebAlertEntry entries[16];
    uint32_t n = WebAlerts_getSince(since_id, entries, 16);

    JsonDocument doc;
    doc["last_id"] = WebAlerts_lastId();
    JsonArray arr = doc["alerts"].to<JsonArray>();
    char text[48];
    for (uint32_t i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["id"]    = entries[i].id;
        o["ts"]    = entries[i].timestamp;
        o["level"] = WebAlerts_level(entries[i].type);
        o["text"]  = WebAlerts_text(entries[i], text, sizeof(text));
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
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
        if ((to_ep - from_ep) > 366u * 86400u) {
            server.send(400, "application/json", "{\"error\":\"range too large (max 366 days)\"}");
            return;
        }

        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.sendHeader("Content-Type", "application/json");
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
    server.send(200);

    if (gran == "5min") {
        // from_ts: solo devolver registros con timestamp > from_ts
        // Si no se especifica, devolver todos
        uint32_t from_ts = 0;
        if (server.hasArg("from_ts"))
            from_ts = (uint32_t)server.arg("from_ts").toInt();

        // Lock antes de getRawDay: mantener el mutex hasta terminar de iterar
        // sobre el puntero recs para evitar que pushRaw() modifique el buffer.
        Cache.lock();
        uint32_t  count_out = 0;
        const Record5Min* recs = Cache.getRawDay(dep, count_out);

        uint32_t first_idx = 0;
        if (from_ts > 0 && recs) {
            while (first_idx < count_out && recs[first_idx].timestamp <= from_ts)
                first_idx++;
        }
        uint32_t send_count = count_out - first_idx;

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
        // Copiar los datos hourly mientras se mantiene el lock para evitar
        // que pushHourly() modifique el buffer durante el envío HTTP.
        HourlyRecord hr_local[24]{};
        bool hr_valid = false;
        DailyRecord dr{};
        DailyStats  daily{};
        Cache.lock();
        const HourlyRecord* hr = Cache.getHourly(dep);
        if (hr) { memcpy(hr_local, hr, 24 * sizeof(HourlyRecord)); hr_valid = true; }
        if (Cache.getDaily(dep, dr)) daily = daily_record_to_stats(dr);
        Cache.unlock();

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
        if (hr_valid) {
            for (int h = 0; h < 24; h++) {
                if (!(hr_local[h].flags & 0x01) || hr_local[h].sample_count == 0) continue;
                char buf[160];
                snprintf(buf, sizeof(buf),
                    "%s{\"h\":%d,\"pv_w\":%d,\"grid_w\":%d,"
                    "\"batt_w\":%d,\"load_w\":%d,\"soc\":%d,\"n\":%d}",
                    first ? "" : ",",
                    h,
                    hr_local[h].avg_pv_w, hr_local[h].avg_grid_w,
                    hr_local[h].avg_batt_w, hr_local[h].avg_load_w,
                    hr_local[h].soc_end, hr_local[h].sample_count);
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

// ═════════════════════════════════════════════════════════════════════════
// PWA: manifest.json e icono SVG
// ═════════════════════════════════════════════════════════════════════════
// ── GET /api/export?from=YYYYMMDD&to=YYYYMMDD ─────────────────────────────
// Descarga un CSV con los totales diarios del rango solicitado (máx 366 días)
static void handle_export_csv() {
    String from_s = server.arg("from");
    String to_s   = server.arg("to");

    uint32_t from_ep = parse_date(from_s);
    uint32_t to_ep   = parse_date(to_s);

    if (!from_ep || !to_ep || to_ep < from_ep) {
        server.send(400, "text/plain", "Parametros invalidos (from/to YYYYMMDD)");
        return;
    }
    if ((to_ep - from_ep) > 366u * 86400u) {
        server.send(400, "text/plain", "Rango maximo 366 dias");
        return;
    }

    String fname = "deye_" + from_s + "_" + to_s + ".csv";
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.sendHeader("Content-Type", "text/csv");
    server.sendHeader("Content-Disposition",
                      "attachment; filename=\"" + fname + "\"");
    server.send(200);

    server.sendContent(
        "fecha,pv_kwh,export_kwh,import_kwh,"
        "load_kwh,bchg_kwh,bdis_kwh,soc_inicio,soc_fin\r\n");

    for (uint32_t d = from_ep; d <= to_ep; d += 86400) {
        DailyRecord dr{};
        if (!Cache.getDaily(d, dr)) continue;
        DailyStats ds = daily_record_to_stats(dr);
        char row[128];
        snprintf(row, sizeof(row),
                 "%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d\r\n",
                 epoch_to_date(d).c_str(),
                 ds.pv_kwh, ds.export_kwh, ds.import_kwh,
                 ds.load_kwh, ds.batt_charge_kwh, ds.batt_discharge_kwh,
                 dr.soc_start, dr.soc_end);
        server.sendContent(row);
    }
    server.sendContent("");
}

static void handle_manifest() {
    server.send(200, "application/manifest+json",
        R"({"name":"Deye Monitor","short_name":"Deye","start_url":"/",)"
        R"("display":"standalone","theme_color":"#0d1117",)"
        R"("background_color":"#0d1117",)"
        R"("icons":[{"src":"/icon.svg","sizes":"any","type":"image/svg+xml"}]})");
}

static void handle_icon() {
    server.send(200, "image/svg+xml",
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 96 96">)"
        R"(<rect width="96" height="96" rx="18" fill="#0d1117"/>)"
        R"(<polygon points="54,12 28,52 50,52 42,84 68,44 46,44" fill="#f5c518"/>)"
        R"(</svg>)");
}

// ═════════════════════════════════════════════════════════════════════════
// Chart.js y su plugin de anotaciones, servidos desde el propio firmware
// (ver web_assets_chartjs.h) en vez de cargarlos de un CDN externo — /chart
// funciona así sin conexión a internet. Cache-Control moderado: el
// contenido solo cambia con una actualización de firmware.
// ═════════════════════════════════════════════════════════════════════════
// DEYE-40: send_P, NO send(). WebServer::send(code, type, const char*) construye un
// String con TODO el contenido (los ~240 KB de chart.js en el heap, en cada
// petición; el propio framework avisa: «Use send_P for long arrays»). send_P
// escribe directamente desde la flash al socket, sin copia.
static void handle_chartjs() {
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.send_P(200, "application/javascript", CHARTJS_UMD_MIN_JS);
}

static void handle_chartjs_annotation() {
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.send_P(200, "application/javascript", CHARTJS_ANNOTATION_MIN_JS);
}

void webserver_begin() {
    // Necesario para que server.header("Origin"/"Referer") devuelva algo —
    // por defecto el WebServer de Arduino no captura cabeceras arbitrarias.
    static const char* csrf_headers[] = {"Origin", "Referer"};
    server.collectHeaders(csrf_headers, 2);

    server.on("/",              HTTP_GET,  handle_root);
    server.on("/manifest.json", HTTP_GET,  handle_manifest);
    server.on("/icon.svg",      HTTP_GET,  handle_icon);
    server.on("/assets/chart.js",            HTTP_GET, handle_chartjs);
    server.on("/assets/chart-annotation.js", HTTP_GET, handle_chartjs_annotation);
    server.on("/update",        HTTP_GET,  handle_update_get);
    server.on("/api/data", HTTP_GET,  handle_api_data);
    server.on("/update",   HTTP_POST, handle_update_post, handle_upload);
    server.on("/api/fw/status",  HTTP_GET,  handle_fw_status);
    server.on("/api/fw/check",   HTTP_POST, handle_fw_check);
    server.on("/api/fw/install", HTTP_POST, handle_fw_install);
    server.on("/api/history",     HTTP_GET, handle_history);
    server.on("/api/latest_date", HTTP_GET, handle_latest_date);
    server.on("/api/alerts",      HTTP_GET, handle_alerts);
    server.on("/api/status",      HTTP_GET, handle_status);
    server.on("/chart",           HTTP_GET, handle_chart);
    server.on("/admin",           HTTP_GET,  handle_admin_get);
    server.on("/admin",           HTTP_POST, handle_admin_post);
    server.on("/api/config/export", HTTP_GET,  handle_config_export);
    server.on("/api/config/import", HTTP_POST, handle_config_import);
    server.on("/api/data/export",   HTTP_GET,  handle_data_export);
    server.on("/api/data/import",   HTTP_POST, handle_data_import_post, handle_data_import_upload);
    server.on("/api/data/wipe",     HTTP_POST, handle_data_wipe);
    server.on("/api/restart",     HTTP_POST, handle_restart);
    server.on("/api/export",      HTTP_GET,  handle_export_csv);
    server.onNotFound([]() {
        server.send(404, "text/plain", "Not found");
    });
    server.begin();
    DBGSERIAL.println("[Web] Servidor en http://" + WiFi.localIP().toString());

    xTaskCreatePinnedToCore(webserver_task, "webserver",
                             8192, nullptr, 1, nullptr, 0);
}