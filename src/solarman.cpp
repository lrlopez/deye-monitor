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

// ── Lectura de registros por TCP ──────────────────────────────────────────
bool SolarmanClient::readRegisters(uint16_t startReg, uint16_t count, uint16_t* values) {
    WiFiClient client;
    client.setTimeout(3);      // segundos — tiempo máximo de espera para recibir datos

    if (!client.connect(_ip, _port, 3000)) {   // ms — timeout de conexión TCP
        DBGSERIAL.println("[Solarman] Timeout conectando al datalogger");
        return false;
    }

    uint8_t frame[64];
    int frameLen = buildV5Frame(frame, startReg, count);
    client.write(frame, frameLen);

    const int DATA_OFFSET = SOLARMAN_DATA_OFFSET;
    const int expectedLen = solarman_expected_len(count);

    static_assert(28 + REG_DAILY_COUNT * 2 + 4 <= 256,
                  "resp[] demasiado pequeño para REG_DAILY_COUNT");
    uint8_t resp[256];
    int received = 0;
    uint32_t t0 = millis();
    while (received < expectedLen && received < (int)sizeof(resp) &&
           (millis() - t0) < 3000) {
        if (client.available())
            resp[received++] = client.read();
        else
            vTaskDelay(pdMS_TO_TICKS(1));
    }
    client.stop();

    switch (solarman_parse_response(resp, received, count, values)) {
    case SolarmanParse::OK: return true;
    case SolarmanParse::SHORT:
        DBGSERIAL.printf("[Solarman] Respuesta incompleta: %d/%d bytes\n", received, expectedLen);
        break;
    case SolarmanParse::BAD_FRAME:
        DBGSERIAL.println("[Solarman] Frame V5 inválido");
        break;
    case SolarmanParse::BAD_FC:
        DBGSERIAL.printf("[Solarman] FC inesperado: 0x%02X\n", resp[DATA_OFFSET - 2]);
        break;
    case SolarmanParse::BAD_CRC:
        DBGSERIAL.println("[Solarman] CRC Modbus inválido");
        break;
    }
    return false;
}


// ── API pública ───────────────────────────────────────────────────────────
bool SolarmanClient::fetchEnergyData(EnergyData& out) {
    uint16_t regs[REG_COUNT];

    DBGSERIAL.print("Leyendo del logger... ");
    if (!readRegisters(REG_BASE, REG_COUNT, regs)) {
        out.valid = false;
        DBGSERIAL.println("error!");
        return false;
    }

    DBGSERIAL.println("ok!");
    out.pv1_power  = (int32_t)(uint16_t)regs[IDX_PV1_POWER];
    out.pv2_power  = (int32_t)(uint16_t)regs[IDX_PV2_POWER];
    out.pv_power   = out.pv1_power + out.pv2_power;
    out.batt_soc   = (int32_t)(uint16_t)regs[IDX_BATT_SOC];
    out.batt_power = (int32_t)(int16_t) regs[IDX_BATT_POWER];  // signed!
    out.load_power = (int32_t)(uint16_t)regs[IDX_LOAD_POWER];
    out.grid_power = (int32_t)(int16_t) regs[IDX_GRID_POWER];  // signed!
    out.valid      = true;
    return true;
}

bool SolarmanClient::fetchDailyStats(DailyStats& out) {
    uint16_t regs[REG_DAILY_COUNT];
    if (!readRegisters(REG_DAILY_BASE, REG_DAILY_COUNT, regs)) {
        out.valid = false;
        return false;
    }

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
        DBGSERIAL.printf("[Daily] AVISO balance: reg108=%.2f calc=%.2f diff=%.2f kWh\n",
                      out.pv_kwh, pv_calc, delta);

    out.valid = true;

    DBGSERIAL.printf("[Daily] PV:%.2f Exp:%.2f Imp:%.2f "
                  "BatC:%.2f BatD:%.2f Load:%.2f kWh\n",
                  out.pv_kwh, out.export_kwh, out.import_kwh,
                  out.batt_charge_kwh, out.batt_discharge_kwh, out.load_kwh);
    return true;
}