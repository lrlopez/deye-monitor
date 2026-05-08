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

    if (!client.connect(_ip, _port)) {
        Serial0.println("[Solarman] Timeout conectando al datalogger");
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
    while (received < expectedLen && (millis() - t0) < 3000) {
        if (client.available())
            resp[received++] = client.read();
    }
    client.stop();

    if (received < expectedLen) {
        Serial0.printf("[Solarman] Respuesta incompleta: %d/%d bytes\n", received, expectedLen);
        return false;
    }
    if (resp[0] != 0xA5 || resp[received - 1] != 0x15) {
        Serial0.println("[Solarman] Frame V5 inválido");
        return false;
    }
    // FC check relativo a DATA_OFFSET (evita hardcoding)
    if (resp[DATA_OFFSET - 2] != 0x03) {
        Serial0.printf("[Solarman] FC inesperado: 0x%02X\n", resp[DATA_OFFSET - 2]);
        return false;
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

    Serial0.print("Leyendo del logger... ");
    if (!readRegisters(REG_BASE, REG_COUNT, regs)) {
        out.valid = false;
        Serial0.println("error!");
        return false;
    }

    Serial0.println("ok!");
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