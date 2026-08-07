#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>
#include <LittleFS.h>
#include "solarman.h"   // DailyStats

static constexpr uint32_t STORE_DAYS = 730;

// ── Registro de 5 minutos ─────────────────────────────────────────────────
// 16 bytes exactos — no cambiar orden sin incrementar versión de formato
#pragma pack(push, 1)
struct Record5Min {
    uint32_t timestamp;     // epoch UTC

    // Potencias instantáneas (W) — valores RAW del inversor
    int16_t  pv_w;          // ≥ 0
    int16_t  grid_w;        // + importando / - exportando
    int16_t  batt_w;        // + descargando / - cargando  (RAW)
    int16_t  load_w;        // ≥ 0

    uint8_t  soc;           // %
    uint8_t  flags;         // bit0=válido  bit1=parcial (arranque tras corte)

    int16_t  extra[1];      // reserva para 1 medida futura
};  // = 32 bytes

static_assert(sizeof(Record5Min) == 16, "Record5Min must be 16 bytes");

// ── HourlyRecord — agregación horaria pre-calculada ───────────────────────
struct HourlyRecord {
    uint32_t hour_epoch;     // medianoche local + hora×3600
    int16_t  avg_pv_w;       // media W del periodo
    int16_t  avg_grid_w;
    int16_t  avg_batt_w;
    int16_t  avg_load_w;
    uint8_t  soc_end;        // SOC al final de la hora
    uint8_t  sample_count;   // muestras recibidas (0-12)
    uint8_t  flags;          // bit0=válido bit1=parcial
    uint8_t  _pad1;
    uint16_t day_pv;         // acumulado diario al final de la hora (0.1 kWh)
    uint16_t day_export;
    uint16_t day_import;
    uint16_t day_load;
    uint16_t day_bchg;
    uint16_t day_bdis;
    int16_t  extra[2];       // reserva
};  // 32 bytes
static_assert(sizeof(HourlyRecord) == 32, "");

// ── DailyRecord — totales del día ─────────────────────────────────────────
struct DailyRecord {
    uint32_t day_epoch;      // medianoche local
    uint16_t pv_10wh;        // energía en 0.1 kWh
    uint16_t export_10wh;
    uint16_t import_10wh;
    uint16_t load_10wh;
    uint16_t bchg_10wh;
    uint16_t bdis_10wh;
    uint8_t  soc_start;
    uint8_t  soc_end;
    uint8_t  flags;          // bit0=válido bit1=día completo
    uint8_t  _pad;
    int16_t  extra[6];       // reserva
};  // 32 bytes
static_assert(sizeof(DailyRecord) == 32, "");
#pragma pack(pop)

struct HourAgg {
    float   pv_w;       // media W del periodo
    float   grid_w;
    float   batt_w;
    float   load_w;
    uint8_t soc;        // último valor de la hora
    // Totales acumulados al final de la hora (0.1 kWh)
    uint16_t day_pv, day_export, day_import;
    uint16_t day_load, day_bchg, day_bdis;
    bool    valid;
};

// ── Conversión a DailyStats ───────────────────────────────────────────────
inline DailyStats daily_record_to_stats(const DailyRecord& r) {
    DailyStats d{};
    d.pv_kwh             = r.pv_10wh     / 10.0f;
    d.export_kwh         = r.export_10wh / 10.0f;
    d.import_kwh         = r.import_10wh / 10.0f;
    d.load_kwh           = r.load_10wh   / 10.0f;
    d.batt_charge_kwh    = r.bchg_10wh   / 10.0f;
    d.batt_discharge_kwh = r.bdis_10wh   / 10.0f;
    d.valid = (r.flags & 0x01);
    return d;
}

// ── Buffer circular en LittleFS ───────────────────────────────────────────
class DataStore {
public:
    static DataStore& instance();

    bool begin();

    // Forzar guardado de metadatos antes de reiniciar
    void flush();

    // ── Raw 5 min ─────────────────────────────────────────────────────────
    bool     push(const Record5Min& r);
    uint32_t readDay(uint32_t day_epoch, Record5Min* out, uint32_t max);
    bool     getLastRecord(Record5Min& out);
    uint32_t getLastRawDayEpoch() const;   // epoch medianoche del último día con datos 5-min

    // ── Hourly ────────────────────────────────────────────────────────────
    // Finaliza la hora anterior y escribe el HourlyRecord
    bool     pushHourly(const HourlyRecord& r);
    // Lee las 24 horas de un día (out debe tener espacio para 24 elementos)
    // Devuelve número de horas con datos
    uint8_t  readHourly(uint32_t day_epoch, HourlyRecord out[24]);
    bool     getLastHourly(uint32_t day_epoch, HourlyRecord& out);

    // ── Daily ─────────────────────────────────────────────────────────────
    bool     pushDaily(const DailyRecord& r);
    bool     readDaily(uint32_t day_epoch, DailyRecord& out);
    // Todos los registros diarios ordenados de más antiguo a más reciente
    uint32_t readAllDaily(DailyRecord* out, uint32_t max);
    DailyRecord getCurrentDaily();

    // ── Capacidad ─────────────────────────────────────────────────────────
    uint32_t getRawCount()       const { return _raw.count;       }
    uint32_t getHourlyCount()    const { return _hrly.count;      }
    uint32_t getDailyCount()     const { return _day.count;       }
    uint32_t getRawCapacity()    const { return _raw.capacity;    }
    uint32_t getHourlyCapacity() const { return _hrly.capacity;   }
    uint32_t getDailyCapacity()  const { return _day.capacity;    }

    // ── Copia de seguridad completa (backup/restore entre dispositivos) ────
    // Los 5 ficheros que componen el almacenamiento, en el orden fijo usado
    // por el contenedor de backup (ver web_server.cpp). day_idx.bin es una
    // caché derivable de raw.bin — se incluye igualmente para que el
    // dispositivo destino no tenga que reconstruirla escaneando todo raw.bin.
    static constexpr uint8_t BACKUP_FILE_COUNT = 5;
    void getBackupPaths(const char* out[BACKUP_FILE_COUNT]) const {
        out[0] = META_FILE; out[1] = IDX_FILE;
        out[2] = _raw.path;  out[3] = _hrly.path; out[4] = _day.path;
    }
    // Magic/versión de formato y capacidades de este dispositivo — para
    // escribir la cabecera al exportar y validar compatibilidad al importar.
    uint32_t getFormatMagic()   const { return MAGIC; }
    uint8_t  getFormatVersion() const { return VERSION; }
    // true si (magic, version, capacidades) coinciden con este dispositivo.
    // Un backup de una capacidad distinta (p.ej. STORE_DAYS cambiado en otra
    // versión del firmware) desincronizaría la aritmética de anillo si se
    // importara tal cual — se rechaza en vez de arriesgar datos corruptos.
    bool isCompatibleBackup(uint32_t magic, uint8_t version,
                             uint32_t raw_cap, uint32_t hrly_cap, uint32_t day_cap) const {
        return magic == MAGIC && version == VERSION &&
               raw_cap == _raw.capacity && hrly_cap == _hrly.capacity && day_cap == _day.capacity;
    }

private:
    DataStore() = default;

    SemaphoreHandle_t _mutex = nullptr;

    // Tres buffers circulares independientes
    struct CircBuf {
        const char* path;
        uint32_t    capacity;
        uint32_t    head;
        uint32_t    count;
    };

    CircBuf  _raw  {"/raw.bin",  STORE_DAYS * 288, 0, 0};  // STORE_DAYS días × 288
    CircBuf  _hrly {"/hrly.bin",  STORE_DAYS * 24, 0, 0};  // STORE_DAYS días × 24
    CircBuf  _day  {"/day.bin",        STORE_DAYS, 0, 0};  // STORE_DAYS días

    // File handles abiertos permanentemente para rendimiento
    File _f_raw;
    File _f_hrly;
    File _f_day;

    // Índice de días en PSRAM — guarda posición FÍSICA (inmutable) en lugar de
    // lógica, para que no se desfase cuando head avanza al envolver el anillo.
    struct DayIdx { uint32_t day_epoch; uint32_t phys_start; };
    DayIdx*  _day_idx       = nullptr;
    uint32_t _day_idx_count  = 0;
    static constexpr uint32_t DAY_IDX_MAX = STORE_DAYS;

    // Meta en RAM, guardada solo periódicamente
    uint32_t _pushes_since_meta_save = 0;
    static constexpr uint32_t META_SAVE_INTERVAL = 12;

    static const char* META_FILE;
    static const char* IDX_FILE;
    static const uint32_t MAGIC     = 0x5A5ADEA2;
    static const uint32_t IDX_MAGIC = 0x49445831;  // 'IDX1'
    static const uint8_t  VERSION   = 5;           // versión 5: sin pre-alocación de ficheros

    DailyRecord currentDaily;

    bool loadMeta();
    bool saveMeta();

    bool writeFile(File& f, uint32_t phys, const void* data, size_t sz);
    bool readFile (File& f, uint32_t phys,       void* data, size_t sz);

    bool writeRaw(uint32_t phys, const Record5Min& r);
    bool readRaw (uint32_t phys, Record5Min& r);

    void   _day_idx_insert(uint32_t day_epoch, uint32_t phys_start);
    int    _day_idx_find  (uint32_t day_epoch) const;
    void   _day_idx_load  ();
    bool   _day_idx_save  ();
    bool   _day_idx_load_file();
    uint32_t physIdx(const CircBuf& cb, uint32_t logical) const;
    uint32_t lowerBoundHrly(uint32_t ts);
};

#define Store DataStore::instance()
