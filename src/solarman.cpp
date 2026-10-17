#include "solarman.h"
#include "solarman_proto.h"
#include "config.h"
#include <string.h>

// CRC, construcción de frame y validación de respuesta: solarman_proto.h
uint16_t SolarmanClient::modbusCRC(const uint8_t* data, uint8_t len) {
    return solarman_crc16(data, len);
}

SolarmanClient::SolarmanClient(const char* ip, uint16_t port,
                               uint32_t loggerSerial, uint8_t unitId)
    : _ip(ip), _port(port), _serial(loggerSerial), _unitId(unitId), _seq(0) {}

int SolarmanClient::buildV5Frame(uint8_t* buf, uint16_t startReg, uint16_t count) {
    return solarman_build_frame(buf, _unitId, _serial, _seq++, startReg, count);
}

bool SolarmanClient::openConn(WiFiClient& client) {
    client.setTimeout((TUNE_LOGGER_TIMEOUT_MS + 999) / 1000);   // segundos — espera máxima al recibir datos
    if (!client.connect(_ip, _port, TUNE_LOGGER_TIMEOUT_MS)) {  // ms — timeout de conexión TCP
        APP_LOGW("Solarman", "Timeout conectando al datalogger");
        return false;
    }
    return true;
}

// ── Petición/respuesta sobre un socket abierto ────────────────────────────
bool SolarmanClient::exchange(WiFiClient& client, uint16_t startReg, uint16_t count,
                              uint16_t* values) {
    uint8_t frame[64];
    int frameLen = buildV5Frame(frame, startReg, count);
    client.write(frame, frameLen);

    const int DATA_OFFSET = SOLARMAN_DATA_OFFSET;
    const int expectedLen = solarman_expected_len(count);

    static_assert(28 + REG_DAILY_COUNT * 2 + 4 <= 256,
                  "resp[] demasiado pequeño para REG_DAILY_COUNT");
    uint8_t resp[256];
    int received = 0;
    int target   = expectedLen;     // se reduce si la propia trama anuncia ser más corta
    uint32_t t0 = millis();
    while (received < target && (millis() - t0) < TUNE_LOGGER_TIMEOUT_MS) {
        int avail = client.available();
        if (avail > 0) {
            // Lectura por bloques (DEYE-07), nunca más allá de lo esperado
            int want = target - received;
            if (avail < want) want = avail;
            int n = client.read(resp + received, want);
            if (n > 0) received += n;

            // Excepción Modbus (FC 0x83...): abortar ya en vez de esperar al timeout
            if (solarman_frame_is_exception(resp, received)) break;
            int total = solarman_frame_total_len(resp, received);
            if (total > 0 && total < target) target = total;
        } else if (!client.connected()) {
            break;                  // el logger cerró el socket y no queda nada por leer
        } else {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }

    switch (solarman_parse_response(resp, received, count, values)) {
    case SolarmanParse::OK: return true;
    case SolarmanParse::SHORT:
        APP_LOGW("Solarman", "Respuesta incompleta: %d/%d bytes", received, expectedLen);
        break;
    case SolarmanParse::BAD_FRAME:
        APP_LOGW("Solarman", "Frame V5 inválido");
        break;
    case SolarmanParse::BAD_FC:
        APP_LOGW("Solarman", "FC inesperado: 0x%02X", resp[DATA_OFFSET - 2]);
        break;
    case SolarmanParse::BAD_CRC:
        APP_LOGW("Solarman", "CRC Modbus inválido");
        break;
    case SolarmanParse::EXCEPTION:
        APP_LOGW("Solarman", "Excepción Modbus: FC=0x%02X", resp[DATA_OFFSET - 2]);
        break;
    }
    return false;
}

bool SolarmanClient::readRegisters(uint16_t startReg, uint16_t count, uint16_t* values) {
    WiFiClient client;
    if (!openConn(client)) return false;
    bool ok = exchange(client, startReg, count, values);
    client.stop();
    return ok;
}

// ── Decodificación ────────────────────────────────────────────────────────
void SolarmanClient::decodeEnergy(const uint16_t* regs, EnergyData& out) {
    out.pv1_power  = (int32_t)(uint16_t)regs[IDX_PV1_POWER];
    out.pv2_power  = (int32_t)(uint16_t)regs[IDX_PV2_POWER];
    out.pv_power   = out.pv1_power + out.pv2_power;
    out.batt_soc   = (int32_t)(uint16_t)regs[IDX_BATT_SOC];
    out.batt_power = (int32_t)(int16_t) regs[IDX_BATT_POWER];  // signed!
    out.load_power = (int32_t)(uint16_t)regs[IDX_LOAD_POWER];
    out.grid_power = (int32_t)(int16_t) regs[IDX_GRID_POWER];  // signed!
    out.valid      = true;
}

void SolarmanClient::decodeDaily(const uint16_t* regs, DailyStats& out) {
    out.batt_charge_kwh    = regs[IDX_D_BATT_CHG]  * 0.1f;
    out.batt_discharge_kwh = regs[IDX_D_BATT_DIS]  * 0.1f;
    out.import_kwh         = regs[IDX_D_GRID_BUY]  * 0.1f;
    out.export_kwh         = regs[IDX_D_GRID_SELL] * 0.1f;
    out.load_kwh           = regs[IDX_D_LOAD]       * 0.1f;
    out.pv_kwh             = regs[IDX_D_PV]         * 0.1f;

    // Verificación por balance energético (solo log, no sobreescribe)
    // PV_calc = Load + Export − Import + BatCarga − BatDescarga
    float pv_calc = out.load_kwh + out.export_kwh - out.import_kwh
                  + out.batt_charge_kwh - out.batt_discharge_kwh;
    float delta = out.pv_kwh - pv_calc;
    if (fabsf(delta) > 0.5f)
        APP_LOGW("Daily", "balance: reg108=%.2f calc=%.2f diff=%.2f kWh",
                 out.pv_kwh, pv_calc, delta);

    out.valid = true;

    APP_LOGD("Daily", "PV:%.2f Exp:%.2f Imp:%.2f BatC:%.2f BatD:%.2f Load:%.2f kWh",
             out.pv_kwh, out.export_kwh, out.import_kwh,
             out.batt_charge_kwh, out.batt_discharge_kwh, out.load_kwh);
}

// ── API pública ───────────────────────────────────────────────────────────
bool SolarmanClient::fetchEnergyData(EnergyData& out) {
    uint16_t regs[REG_COUNT];

    if (!readRegisters(REG_BASE, REG_COUNT, regs)) {
        out.valid = false;
        return false;     // la causa ya se ha registrado en exchange()/openConn()
    }

    APP_LOGD("Solarman", "lectura instantánea ok");
    decodeEnergy(regs, out);
    return true;
}

bool SolarmanClient::fetchDailyStats(DailyStats& out) {
    uint16_t regs[REG_DAILY_COUNT];
    if (!readRegisters(REG_DAILY_BASE, REG_DAILY_COUNT, regs)) {
        out.valid = false;
        return false;
    }
    decodeDaily(regs, out);
    return true;
}

bool SolarmanClient::fetchAll(EnergyData& e, DailyStats& d, bool want_daily, bool& daily_ok) {
    daily_ok = false;

    WiFiClient client;
    if (!openConn(client)) {
        e.valid = false;
        return false;     // causa ya registrada en openConn()
    }

    uint16_t regs[REG_COUNT];
    if (!exchange(client, REG_BASE, REG_COUNT, regs)) {
        client.stop();
        e.valid = false;
        return false;     // causa ya registrada en exchange()
    }
    APP_LOGD("Solarman", "lectura instantánea ok");
    decodeEnergy(regs, e);

    if (want_daily) {
        uint16_t dregs[REG_DAILY_COUNT];
        daily_ok = exchange(client, REG_DAILY_BASE, REG_DAILY_COUNT, dregs);
        client.stop();
        if (!daily_ok) {
            // Puede que el logger cierre el socket tras responder: un reintento
            // con conexión nueva distingue "no admite reutilizar" de un fallo real.
            WiFiClient retry;
            if (openConn(retry)) {
                daily_ok = exchange(retry, REG_DAILY_BASE, REG_DAILY_COUNT, dregs);
                retry.stop();
            }
        }
        if (daily_ok) decodeDaily(dregs, d);
    } else {
        client.stop();
    }
    return true;
}
