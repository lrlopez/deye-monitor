#include "solarman.h"
#include "config.h"
#include <string.h>

// ── CRC16 Modbus ──────────────────────────────────────────────────────────
uint16_t SolarmanClient::modbusCRC(const uint8_t* data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

SolarmanClient::SolarmanClient(const char* ip, uint16_t port,
                               uint32_t loggerSerial, uint8_t unitId)
    : _ip(ip), _port(port), _serial(loggerSerial), _unitId(unitId), _seq(0) {}

// ── Constructor del frame SolarmanV5 ─────────────────────────────────────
//
//  [A5] [len_lo len_hi] [10 45] [seq_lo seq_hi] [serial x4 LE]
//  ├─ Payload (15 bytes cabecera + Modbus RTU) ─┤
//  [checksum] [15]
//
//  Payload inner header: 0x02 + 4 bytes timestamp (0) + 10 bytes padding
//
int SolarmanClient::buildV5Frame(uint8_t* buf, uint16_t startReg, uint16_t count) {
    // Modbus RTU: Read Holding Registers (FC=03), 8 bytes
    uint8_t mb[8];
    mb[0] = _unitId;
    mb[1] = 0x03;
    mb[2] = (startReg >> 8) & 0xFF;
    mb[3] =  startReg       & 0xFF;
    mb[4] = (count    >> 8) & 0xFF;
    mb[5] =  count          & 0xFF;
    uint16_t crc = modbusCRC(mb, 6);
    mb[6] = crc & 0xFF;
    mb[7] = (crc >> 8) & 0xFF;

    const uint8_t INNER_HDR = 15;
    uint16_t payloadLen = INNER_HDR + sizeof(mb);  // 23

    int i = 0;
    buf[i++] = 0xA5;                    // start
    buf[i++] = payloadLen & 0xFF;       // length LE
    buf[i++] = (payloadLen >> 8) & 0xFF;
    buf[i++] = 0x10;                    // control lo
    buf[i++] = 0x45;                    // control hi
    buf[i++] = _seq & 0xFF;             // sequence LE
    buf[i++] = (_seq >> 8) & 0xFF;
    _seq++;
    buf[i++] = _serial        & 0xFF;   // serial LE (4 bytes)
    buf[i++] = (_serial >>  8)& 0xFF;
    buf[i++] = (_serial >> 16)& 0xFF;
    buf[i++] = (_serial >> 24)& 0xFF;

    // Inner header: 1 byte tipo + 4 timestamp + 10 padding = 15 bytes
    buf[i++] = 0x02;
    memset(&buf[i], 0x00, 14);
    i += 14;

    // Modbus RTU
    memcpy(&buf[i], mb, sizeof(mb));
    i += sizeof(mb);

    // Checksum = suma de buf[1..i-1]
    uint8_t cs = 0;
    for (int j = 1; j < i; j++) cs += buf[j];
    buf[i++] = cs;
    buf[i++] = 0x15;    // end

    return i;   // 36 bytes total
}

// ── Lectura de registros por TCP ──────────────────────────────────────────
bool SolarmanClient::readRegisters(uint16_t startReg, uint16_t count, uint16_t* values) {
    WiFiClient client;
    client.setTimeout(3);

    if (!client.connect(_ip, _port, 3000)) {
        DBGSERIAL.println("[Solarman] Timeout conectando al datalogger");
        return false;
    }

    uint8_t frame[64];
    int frameLen = buildV5Frame(frame, startReg, count);
    client.write(frame, frameLen);

    // DATA_OFFSET = 11 (header V5) + 14 (inner header respuesta) + 3 (Modbus cabecera)
    const int DATA_OFFSET = 28;
    const int expectedLen = DATA_OFFSET + count * 2 + 4; // +2 CRC Modbus +1 checksum +1 0x15

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

    if (received < expectedLen) {
        DBGSERIAL.printf("[Solarman] Respuesta incompleta: %d/%d bytes\n", received, expectedLen);
        return false;
    }
    if (resp[0] != 0xA5 || resp[received - 1] != 0x15) {
        DBGSERIAL.println("[Solarman] Frame V5 inválido");
        return false;
    }
    // FC check relativo a DATA_OFFSET (evita hardcoding)
    if (resp[DATA_OFFSET - 2] != 0x03) {
        DBGSERIAL.printf("[Solarman] FC inesperado: 0x%02X\n", resp[DATA_OFFSET - 2]);
        return false;
    }

    // CRC Modbus: cubre unit_id + FC + byte_count + datos (3 + count*2 bytes)
    // Transmitido LSB primero, inmediatamente después de los datos
    {
        const int mb_start    = DATA_OFFSET - 3;
        const int mb_data_len = 3 + count * 2;
        uint16_t crc_recv = (uint16_t)resp[mb_start + mb_data_len]
                          | ((uint16_t)resp[mb_start + mb_data_len + 1] << 8);
        uint16_t crc_calc = modbusCRC(&resp[mb_start], mb_data_len);
        if (crc_recv != crc_calc) {
            DBGSERIAL.printf("[Solarman] CRC Modbus inválido: recv=0x%04X calc=0x%04X\n",
                           crc_recv, crc_calc);
            return false;
        }
    }

    for (uint16_t r = 0; r < count; r++) {
        int pos = DATA_OFFSET + r * 2;
        values[r] = ((uint16_t)resp[pos] << 8) | resp[pos + 1];
    }
    return true;
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