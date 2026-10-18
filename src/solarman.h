#pragma once
#include <Arduino.h>
#include <WiFi.h>

// Datos de energía compartidos entre tarea de red y dashboard
struct EnergyData {
    int32_t pv_power;    // W  (PV1 + PV2)
    int32_t pv1_power;   // W
    int32_t pv2_power;   // W
    int32_t batt_power;  // W  (<0 cargando, >0 descargando)
    int32_t batt_soc;    // %
    int32_t load_power;  // W
    int32_t grid_power;  // W  (>0 importando, <0 exportando)
    float   batt_voltage_v;   // V; 0 = no disponible (DEYE-36, registro sin verificar)
    bool    valid;
};

struct DailyStats {
    float pv_kwh;
    float export_kwh;
    float import_kwh;
    float batt_charge_kwh;
    float batt_discharge_kwh;
    float load_kwh;
    float inv_temp_c;    // °C del radiador; -999 = no disponible (DEYE-36, registro sin verificar)
    bool  valid;
};

// La temperatura solo es utilizable si el bloque diario se leyó (valid) y el registro era
// verosímil; los DailyStats a cero (sin lectura aún) tienen inv_temp_c = 0 exacto → no disponible.
inline bool inv_temp_available(const DailyStats& d) {
    return d.valid && d.inv_temp_c > -100.0f && d.inv_temp_c != 0.0f;
}

class SolarmanClient {
public:
    SolarmanClient(const char* ip, uint16_t port,
                   uint32_t loggerSerial, uint8_t unitId = 1);

    bool fetchEnergyData(EnergyData& out);
    bool fetchDailyStats(DailyStats& out);

    // Una sola conexión TCP para ambas lecturas (DEYE-08). Siempre lee los
    // instantáneos; si want_daily, lee también los diarios sobre el mismo socket
    // (si el logger lo cierra tras la primera respuesta, reconecta una vez).
    // Devuelve true si los instantáneos se leyeron bien. `daily_ok` indica si
    // los diarios también; si falla, `d` no se modifica.
    bool fetchAll(EnergyData& e, DailyStats& d, bool want_daily, bool& daily_ok);

private:
    const char* _ip;
    uint16_t    _port;
    uint32_t    _serial;
    uint8_t     _unitId;
    uint16_t    _seq;

    static uint16_t modbusCRC(const uint8_t* data, uint8_t len);
    int  buildV5Frame(uint8_t* buf, uint16_t startReg, uint16_t count);
    bool openConn(WiFiClient& client);
    // Petición + respuesta sobre un socket ya abierto (no lo cierra)
    bool exchange(WiFiClient& client, uint16_t startReg, uint16_t count, uint16_t* values);
    // Conexión nueva + exchange + cierre
    bool readRegisters(uint16_t startReg, uint16_t count, uint16_t* values);
    static void decodeEnergy(const uint16_t* regs, EnergyData& out);
    static void decodeDaily(const uint16_t* regs, DailyStats& out);
};
