#include "fw_update.h"
#include "fw_update_logic.h"
#include "fw_update_ca.h"
#include "config.h"
#include "data_store.h"
#include "safe_serial.h"
#include "tuning.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include <time.h>

// Protocolo con main.cpp (hilo LVGL): congelar el render mientras se escribe la
// flash (en las P4 la escritura compite con el DMA del display) y, si algo falla,
// descongelar y retirar el overlay.
extern volatile bool g_ota_request;
extern volatile bool g_ota_active;
extern volatile bool g_ota_cancel;

static const uint32_t MIN_FREE_HEAP = 50000;   // bloque contiguo mínimo para un handshake TLS
static const uint32_t TIME_VALID_EPOCH = 1700000000UL;

// ── Estado compartido ─────────────────────────────────────────────────────
namespace {
struct Shared {
    SemaphoreHandle_t mtx;
    FwStatus   st;
    FwRelease  rel;            // resultado de la última consulta
    bool       rel_ok = false;
    volatile bool busy = false;
    Shared() : mtx(xSemaphoreCreateMutex()) {
        memset(&st, 0, sizeof(st));
        st.state = FwState::IDLE;
        fw_copy(st.env, sizeof(st.env), FW_ENV_NAME);
        fw_copy(st.current, sizeof(st.current), APP_VERSION);
    }
};
Shared& S() { static Shared s; return s; }       // inicialización thread-safe (C++11)

struct Lock {
    Lock()  { xSemaphoreTake(S().mtx, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(S().mtx); }
};
}  // namespace

const char* fw_state_name(FwState s) {
    switch (s) {
    case FwState::IDLE:        return "idle";
    case FwState::CHECKING:    return "checking";
    case FwState::UP_TO_DATE:  return "uptodate";
    case FwState::AVAILABLE:   return "available";
    case FwState::DOWNLOADING: return "downloading";
    case FwState::INSTALLED:   return "installed";
    case FwState::ERROR:       return "error";
    }
    return "idle";
}

void fw_update_status(FwStatus& out) {
    Lock l;
    out = S().st;
}

static void set_state(FwState s) {
    Lock l;
    S().st.state = s;
    if (s != FwState::ERROR) S().st.error[0] = '\0';
}

static void fail(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void fail(const char* fmt, ...) {
    char msg[96];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    APP_LOGW("FwUpdate", "%s", msg);
    Lock l;
    S().st.state = FwState::ERROR;
    fw_copy(S().st.error, sizeof(S().st.error), msg);
}

// ── Consulta ──────────────────────────────────────────────────────────────
static void do_check() {
    {
        Lock l;
        FwStatus& st = S().st;
        st.state = FwState::CHECKING;
        st.error[0] = '\0'; st.latest[0] = '\0'; st.notes_url[0] = '\0'; st.asset[0] = '\0';
        st.size = 0; st.done = 0; st.has_digest = false;
        S().rel_ok = false;
    }

    if (!FW_ENV_NAME[0])                 { fail("Este firmware no sabe qué binario le corresponde"); return; }
    if (WiFi.status() != WL_CONNECTED)   { fail("Sin conexión WiFi"); return; }
    if (time(nullptr) < (time_t)TIME_VALID_EPOCH) {
        fail("Reloj sin sincronizar: hace falta para validar el certificado TLS"); return;
    }
    if (ESP.getMaxAllocHeap() < MIN_FREE_HEAP) { fail("Memoria libre insuficiente para TLS"); return; }

    String body;
    int code = 0;
    {
        WiFiClientSecure client;
        client.setCACert(FW_UPDATE_ROOT_CAS);
        client.setTimeout(15);
        HTTPClient http;
        http.setTimeout(15000);
        http.setUserAgent("DeyeMonitor/" APP_VERSION);
        if (!http.begin(client, "https://api.github.com/repos/" FW_UPDATE_REPO "/releases/latest")) {
            fail("No se pudo iniciar la conexión"); return;
        }
        http.addHeader("Accept", "application/vnd.github+json");
        code = http.GET();
        if (code == 200) {
            int sz = http.getSize();
            if (sz > 60000) { http.end(); fail("Respuesta de GitHub demasiado grande"); return; }
            body = http.getString();
        } else if (code < 0) {
            char tls[80] = {0};
            client.lastError(tls, sizeof(tls));
            http.end();
            fail("Conexión con GitHub fallida: %s%s%s", HTTPClient::errorToString(code).c_str(),
                 tls[0] ? " / " : "", tls);
            return;
        }
        http.end();
    }
    if (code == 403 || code == 429) { fail("GitHub limita las consultas; reinténtalo más tarde"); return; }
    if (code == 404)                { fail("No hay ninguna release publicada"); return; }
    if (code != 200)                { fail("GitHub respondió HTTP %d", code); return; }

    FwRelease rel;
    if (!fw_parse_release(body.c_str(), FW_ENV_NAME, rel)) {
        fail("%s", rel.error[0] ? rel.error : "Respuesta no válida");
        return;
    }

    const bool newer = fw_is_newer(APP_VERSION, rel.tag);
    APP_LOGI("FwUpdate", "Última release %s (instalada %s) -> %s", rel.tag, APP_VERSION,
             newer ? "hay actualización" : "al día");
    Lock l;
    S().rel = rel;
    S().rel_ok = true;
    FwStatus& st = S().st;
    fw_copy(st.latest, sizeof(st.latest), rel.tag);
    fw_copy(st.notes_url, sizeof(st.notes_url), rel.html_url);
    fw_copy(st.asset, sizeof(st.asset), rel.asset_name);
    st.size = rel.asset_size;
    st.has_digest = rel.has_digest;
    st.state = newer ? FwState::AVAILABLE : FwState::UP_TO_DATE;
}

// ── Escritura en la partición OTA ─────────────────────────────────────────
// Sumidero para HTTPClient::writeToStream(): recibe el cuerpo ya descodificado
// (con o sin chunked), comprueba la cabecera del primer bloque, lo escribe en la
// partición OTA, calcula el SHA-256 y publica el progreso.
namespace {
class UpdateSink : public Stream {
public:
    UpdateSink() { mbedtls_sha256_init(&_ctx); mbedtls_sha256_starts(&_ctx, 0); }
    ~UpdateSink() { mbedtls_sha256_free(&_ctx); }

    size_t write(const uint8_t* b, size_t n) override {
        if (_failed || n == 0) return _failed ? 0 : n;
        if (_written == 0 && !fw_looks_like_esp_image(b, n)) {
            _failed = true; _why = "El fichero descargado no es una imagen de firmware";
            return 0;
        }
        if (Update.write(const_cast<uint8_t*>(b), n) != n) {
            _failed = true; _why = Update.errorString();
            return 0;
        }
        mbedtls_sha256_update(&_ctx, b, n);
        _written += n;
        S().st.done = _written;          // escritura de 32 bits: no necesita el mutex
        if (_written - _logged >= 262144) {
            _logged = _written;
            APP_LOGI("FwUpdate", "%u bytes, heap libre %u (mín %u)", (unsigned)_written,
                     (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
        }
#if TUNE_FW_CHUNK_DELAY_MS > 0
        vTaskDelay(pdMS_TO_TICKS(TUNE_FW_CHUNK_DELAY_MS));   // deja que la WiFi vacíe su cola
#endif
        return n;
    }
    size_t write(uint8_t c) override { return write(&c, 1); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }

    bool        failed()  const { return _failed; }
    const char* why()     const { return _why; }
    uint32_t    written() const { return _written; }
    void finish(uint8_t out[32]) { mbedtls_sha256_finish(&_ctx, out); }

private:
    mbedtls_sha256_context _ctx;
    uint32_t    _written = 0;
    uint32_t    _logged  = 0;
    bool        _failed  = false;
    const char* _why     = "";
};
}  // namespace

// ── Instalación ───────────────────────────────────────────────────────────
static void do_install() {
    FwRelease rel;
    {
        Lock l;
        if (!S().rel_ok) { return; }
        rel = S().rel;
        S().st.state = FwState::DOWNLOADING;
        S().st.error[0] = '\0';
        S().st.done = 0;
        S().st.size = rel.asset_size;
    }

    if (WiFi.status() != WL_CONNECTED)         { fail("Sin conexión WiFi"); return; }
    if (ESP.getMaxAllocHeap() < MIN_FREE_HEAP) { fail("Memoria libre insuficiente para TLS"); return; }

    // Congelar el render y esperar a que el hilo LVGL confirme el overlay antes de
    // tocar la flash (mismo protocolo que la subida manual)
    g_ota_request = true;
    uint32_t t0 = millis();
    while (!g_ota_active && millis() - t0 < 2500) vTaskDelay(pdMS_TO_TICKS(10));

    auto abort_install = [&](bool update_started) {
        if (update_started) Update.abort();
        g_ota_cancel = true;               // descongela la UI y retira el overlay
    };

    WiFiClientSecure client;
    client.setCACert(FW_UPDATE_ROOT_CAS);
    client.setTimeout(20);
    HTTPClient http;
    http.setTimeout(20000);
    http.setUserAgent("DeyeMonitor/" APP_VERSION);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // github.com -> release-assets.githubusercontent.com
    if (!http.begin(client, rel.asset_url)) {
        abort_install(false); fail("No se pudo iniciar la descarga"); return;
    }
    int code = http.GET();
    if (code != 200) {
        char tls[80] = {0};
        if (code < 0) client.lastError(tls, sizeof(tls));
        http.end();
        abort_install(false);
        if (code < 0) fail("Descarga fallida: %s%s%s", HTTPClient::errorToString(code).c_str(),
                           tls[0] ? " / " : "", tls);
        else          fail("La descarga devolvió HTTP %d", code);
        return;
    }
    const int len = http.getSize();
    if (len > 0 && (uint32_t)len != rel.asset_size) {
        http.end(); abort_install(false);
        fail("El tamaño descargado (%d) no coincide con el publicado (%u)", len, (unsigned)rel.asset_size);
        return;
    }
    if (!Update.begin(rel.asset_size)) {
        http.end(); abort_install(false);
        fail("No se puede iniciar la actualización: %s", Update.errorString());
        return;
    }

    UpdateSink sink;
    const int w = http.writeToStream(&sink);
    http.end();

    if (sink.failed()) { abort_install(true); fail("%s", sink.why()); return; }
    if (w < 0 || sink.written() != rel.asset_size) {
        abort_install(true);
        fail("Descarga incompleta (%u de %u bytes)", (unsigned)sink.written(), (unsigned)rel.asset_size);
        return;
    }

    // Verificar el hash ANTES de activar la imagen
    uint8_t digest[32];
    sink.finish(digest);
    if (rel.has_digest) {
        if (!fw_sha256_equals_hex(digest, rel.sha256)) {
            abort_install(true); fail("El SHA-256 del binario no coincide con el publicado"); return;
        }
    } else {
        APP_LOGW("FwUpdate", "La release no publica SHA-256: solo se confía en TLS");
    }

    if (!Update.end(true)) {     // valida la imagen y la marca como siguiente arranque
        abort_install(false);
        fail("Error al finalizar: %s", Update.errorString());
        return;
    }

    APP_LOGI("FwUpdate", "Instalado %s (%u bytes). Reiniciando.", rel.tag, (unsigned)sink.written());
    set_state(FwState::INSTALLED);
    Store.flush();
    vTaskDelay(pdMS_TO_TICKS(2500));        // da tiempo a que la página muestre el resultado
    ESP.restart();
}

// ── Tareas ────────────────────────────────────────────────────────────────
static void check_task(void*)   { do_check();   S().busy = false; vTaskDelete(nullptr); }
static void install_task(void*) { do_install(); S().busy = false; vTaskDelete(nullptr); }

static bool spawn(TaskFunction_t fn, const char* name) {
    if (S().busy) return false;
    S().busy = true;
    // 16 KB: handshake TLS + ArduinoJson (mismo orden que la tarea de Telegram)
    if (xTaskCreatePinnedToCore(fn, name, 16384, nullptr, 1, nullptr, 0) != pdPASS) {
        S().busy = false;
        return false;
    }
    return true;
}

bool fw_update_check_async() { return spawn(check_task, "fw_check"); }

bool fw_update_install_async() {
    {
        Lock l;
        if (!S().rel_ok || S().st.state != FwState::AVAILABLE) return false;
    }
    return spawn(install_task, "fw_install");
}
