#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "solarman_proto.h"
#include "slot_avg.h"
#include "time_utils.h"
#include "ring_index.h"
#include "alerts_logic.h"
#include "daily_tracker.h"

// Misma zona horaria que config.h (TIMEZONE)
static const char* TZ_SPAIN = "CET-1CEST,M3.5.0,M10.5.0/3";

void setUp(void) {
    setenv("TZ", TZ_SPAIN, 1);
    tzset();
}
void tearDown(void) {}

static uint32_t local_midnight(int y, int m, int d) {
    struct tm t{};
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d; t.tm_isdst = -1;
    return (uint32_t)mktime(&t);
}

// ── Solarman: CRC Modbus ──────────────────────────────────────────────────
void test_crc_known_vector() {
    // Ejemplo canónico Modbus: 01 03 00 00 00 0A → CRC 0xCDC5 (C5 CD en el cable)
    const uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    TEST_ASSERT_EQUAL_HEX16(0xCDC5, solarman_crc16(req, sizeof(req)));
}

// ── Solarman: construcción de frame ───────────────────────────────────────
void test_build_frame_layout() {
    uint8_t f[64];
    int n = solarman_build_frame(f, 1, 0x11223344u, 0x0102, 169, 23);
    TEST_ASSERT_EQUAL_INT(SOLARMAN_FRAME_LEN, n);
    TEST_ASSERT_EQUAL_HEX8(0xA5, f[0]);
    TEST_ASSERT_EQUAL_HEX8(23, f[1]);          // longitud payload LE
    TEST_ASSERT_EQUAL_HEX8(0, f[2]);
    TEST_ASSERT_EQUAL_HEX8(0x10, f[3]);
    TEST_ASSERT_EQUAL_HEX8(0x45, f[4]);
    TEST_ASSERT_EQUAL_HEX8(0x02, f[5]);        // seq LE
    TEST_ASSERT_EQUAL_HEX8(0x01, f[6]);
    TEST_ASSERT_EQUAL_HEX8(0x44, f[7]);        // serial LE
    TEST_ASSERT_EQUAL_HEX8(0x33, f[8]);
    TEST_ASSERT_EQUAL_HEX8(0x22, f[9]);
    TEST_ASSERT_EQUAL_HEX8(0x11, f[10]);
    TEST_ASSERT_EQUAL_HEX8(0x02, f[11]);       // inner header
    // Modbus: unit=1 FC=03 reg=169 count=23
    TEST_ASSERT_EQUAL_HEX8(0x01, f[26]);
    TEST_ASSERT_EQUAL_HEX8(0x03, f[27]);
    TEST_ASSERT_EQUAL_HEX8(0x00, f[28]);
    TEST_ASSERT_EQUAL_HEX8(169,  f[29]);
    TEST_ASSERT_EQUAL_HEX8(0x00, f[30]);
    TEST_ASSERT_EQUAL_HEX8(23,   f[31]);
    uint16_t crc = solarman_crc16(&f[26], 6);
    TEST_ASSERT_EQUAL_HEX8(crc & 0xFF, f[32]);
    TEST_ASSERT_EQUAL_HEX8(crc >> 8,   f[33]);
    uint8_t cs = 0;
    for (int i = 1; i < 34; i++) cs += f[i];
    TEST_ASSERT_EQUAL_HEX8(cs, f[34]);
    TEST_ASSERT_EQUAL_HEX8(0x15, f[35]);
}

// Respuesta sintética válida con `count` registros (valores vals[]).
static int make_response(uint8_t* r, uint16_t count, const uint16_t* vals) {
    memset(r, 0, 256);
    r[0] = 0xA5;
    r[SOLARMAN_DATA_OFFSET - 3] = 0x01;                  // unit
    r[SOLARMAN_DATA_OFFSET - 2] = 0x03;                  // FC
    r[SOLARMAN_DATA_OFFSET - 1] = (uint8_t)(count * 2);  // byte count
    for (uint16_t i = 0; i < count; i++) {
        r[SOLARMAN_DATA_OFFSET + i * 2]     = vals[i] >> 8;
        r[SOLARMAN_DATA_OFFSET + i * 2 + 1] = vals[i] & 0xFF;
    }
    int mb_start = SOLARMAN_DATA_OFFSET - 3;
    int mb_len = 3 + count * 2;
    uint16_t crc = solarman_crc16(&r[mb_start], (uint8_t)mb_len);
    r[mb_start + mb_len]     = crc & 0xFF;
    r[mb_start + mb_len + 1] = crc >> 8;
    int total = solarman_expected_len(count);
    r[total - 2] = 0x00;     // checksum V5 (no se valida)
    r[total - 1] = 0x15;
    return total;
}

void test_parse_ok_big_endian() {
    uint8_t r[256]; uint16_t vals[3] = {0x0102, 0xFFFF, 0};
    int n = make_response(r, 3, vals);
    uint16_t out[3] = {};
    TEST_ASSERT_EQUAL(SolarmanParse::OK, solarman_parse_response(r, n, 3, out));
    TEST_ASSERT_EQUAL_HEX16(0x0102, out[0]);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, out[1]);
    TEST_ASSERT_EQUAL_HEX16(0, out[2]);
}

void test_parse_errors() {
    uint8_t r[256]; uint16_t vals[2] = {1, 2}; uint16_t out[2];
    int n = make_response(r, 2, vals);
    TEST_ASSERT_EQUAL(SolarmanParse::SHORT, solarman_parse_response(r, n - 1, 2, out));

    uint8_t b[256]; memcpy(b, r, 256); b[0] = 0x00;
    TEST_ASSERT_EQUAL(SolarmanParse::BAD_FRAME, solarman_parse_response(b, n, 2, out));
    memcpy(b, r, 256); b[n - 1] = 0x00;
    TEST_ASSERT_EQUAL(SolarmanParse::BAD_FRAME, solarman_parse_response(b, n, 2, out));

    memcpy(b, r, 256); b[SOLARMAN_DATA_OFFSET - 2] = 0x83;   // excepción Modbus
    TEST_ASSERT_EQUAL(SolarmanParse::EXCEPTION, solarman_parse_response(b, n, 2, out));

    memcpy(b, r, 256); b[SOLARMAN_DATA_OFFSET - 2] = 0x04;   // FC distinto de 03 sin bit 7
    TEST_ASSERT_EQUAL(SolarmanParse::BAD_FC, solarman_parse_response(b, n, 2, out));

    memcpy(b, r, 256); b[SOLARMAN_DATA_OFFSET] ^= 0x01;      // dato corrupto
    TEST_ASSERT_EQUAL(SolarmanParse::BAD_CRC, solarman_parse_response(b, n, 2, out));
}

// DEYE-07: longitud total anunciada por la propia trama y detección temprana
// de excepciones (no hay que esperar al timeout con una trama corta)
void test_frame_total_len_from_header() {
    uint8_t r[256]; uint16_t vals[3] = {1, 2, 3};
    int n = make_response(r, 3, vals);
    // El campo de longitud (payload) más 13 bytes de cabecera/cola = longitud total
    r[1] = (uint8_t)((n - 13) & 0xFF); r[2] = (uint8_t)((n - 13) >> 8);
    TEST_ASSERT_EQUAL_INT(n, solarman_frame_total_len(r, n));
    TEST_ASSERT_EQUAL_INT(0, solarman_frame_total_len(r, 2));     // aún no hay cabecera
    r[0] = 0x00;
    TEST_ASSERT_EQUAL_INT(0, solarman_frame_total_len(r, n));     // no empieza por A5
}

void test_exception_detected_on_short_frame() {
    uint8_t r[256] = {};
    r[0] = 0xA5;
    r[SOLARMAN_DATA_OFFSET - 2] = 0x83;
    int short_len = SOLARMAN_DATA_OFFSET + 3;   // trama de excepción: mucho más corta que la esperada
    TEST_ASSERT_TRUE(short_len < solarman_expected_len(23));
    uint16_t out[23];
    TEST_ASSERT_TRUE(solarman_frame_is_exception(r, short_len));
    TEST_ASSERT_EQUAL(SolarmanParse::EXCEPTION, solarman_parse_response(r, short_len, 23, out));
    // Con menos bytes de los necesarios para ver el FC todavía no se puede afirmar
    TEST_ASSERT_FALSE(solarman_frame_is_exception(r, SOLARMAN_DATA_OFFSET - 2));
}

// ── Descubrimiento del logger ─────────────────────────────────────────────
void test_ipv4_validation() {
    TEST_ASSERT_TRUE(solarman_is_valid_ipv4("192.168.1.50"));
    TEST_ASSERT_TRUE(solarman_is_valid_ipv4("0.0.0.0"));
    TEST_ASSERT_FALSE(solarman_is_valid_ipv4("192.168.1"));
    TEST_ASSERT_FALSE(solarman_is_valid_ipv4("192.168.1.256"));
    TEST_ASSERT_FALSE(solarman_is_valid_ipv4("192.168.1.1.1"));
    TEST_ASSERT_FALSE(solarman_is_valid_ipv4("192.168..1"));
    TEST_ASSERT_FALSE(solarman_is_valid_ipv4("a.b.c.d"));
    TEST_ASSERT_FALSE(solarman_is_valid_ipv4(""));
}

void test_discovery_reply_parse() {
    char ip[16];
    const char* ok = "192.168.1.77,AABBCCDDEEFF,2712345678";
    TEST_ASSERT_TRUE(solarman_parse_discovery_reply(ok, (int)strlen(ok), ip, sizeof(ip)));
    TEST_ASSERT_EQUAL_STRING("192.168.1.77", ip);

    const char* nl = "10.0.0.5\r\n";
    TEST_ASSERT_TRUE(solarman_parse_discovery_reply(nl, (int)strlen(nl), ip, sizeof(ip)));
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", ip);

    const char* junk = "OK";
    TEST_ASSERT_FALSE(solarman_parse_discovery_reply(junk, 2, ip, sizeof(ip)));
    const char* big = "999.1.1.1,xx";
    TEST_ASSERT_FALSE(solarman_parse_discovery_reply(big, (int)strlen(big), ip, sizeof(ip)));
    const char* longip = "1234567890123456789,xx";
    TEST_ASSERT_FALSE(solarman_parse_discovery_reply(longip, (int)strlen(longip), ip, sizeof(ip)));
    TEST_ASSERT_FALSE(solarman_parse_discovery_reply(",x", 2, ip, sizeof(ip)));
}

// Regresión: el bloque diario (39 registros) debe caber en el búfer de 256 B
void test_daily_block_fits_buffer() {
    TEST_ASSERT_TRUE(solarman_expected_len(39) <= 256);
}

// ── SlotAverager ──────────────────────────────────────────────────────────
void test_slot_average_basic_and_rounding() {
    SlotAverager s;
    TEST_ASSERT_TRUE(s.empty());
    s.add(100, 10, -500, 300);
    s.add(201, 11, -501, 301);
    TEST_ASSERT_FALSE(s.empty());
    TEST_ASSERT_EQUAL_INT16(151, s.pv());      // 150.5 → 151
    TEST_ASSERT_EQUAL_INT16(11, s.grid());     // 10.5 → 11
    TEST_ASSERT_EQUAL_INT16(-501, s.batt());   // -500.5 → -501 (simétrico)
    TEST_ASSERT_EQUAL_INT16(301, s.load());    // 300.5 → 301
}

void test_slot_average_spike_is_smoothed() {
    // Un pico de 1 muestra de 12 no debe dominar el registro (bug previo: muestra puntual)
    SlotAverager s;
    for (int i = 0; i < 11; i++) s.add(1000, 0, 0, 500);
    s.add(4000, 0, 0, 500);
    TEST_ASSERT_EQUAL_INT16(1250, s.pv());
}

void test_slot_average_clamp_and_reset() {
    SlotAverager s;
    s.add(100000, -100000, 0, 0);
    TEST_ASSERT_EQUAL_INT16(32767, s.pv());
    TEST_ASSERT_EQUAL_INT16(-32767, s.grid());
    s.reset();
    TEST_ASSERT_TRUE(s.empty());
}

// ── time_utils (DST) ──────────────────────────────────────────────────────
void test_next_midnight_normal_day() {
    uint32_t m = local_midnight(2026, 6, 15);
    TEST_ASSERT_EQUAL_UINT32(86400, next_local_midnight(m) - m);
}

void test_next_midnight_spring_forward_is_23h() {
    uint32_t m = local_midnight(2026, 3, 29);   // último domingo de marzo
    TEST_ASSERT_EQUAL_UINT32(82800, next_local_midnight(m) - m);
}

void test_next_midnight_fall_back_is_25h() {
    uint32_t m = local_midnight(2026, 10, 25);  // último domingo de octubre
    TEST_ASSERT_EQUAL_UINT32(90000, next_local_midnight(m) - m);
}

void test_plus_days_crosses_dst() {
    uint32_t m = local_midnight(2026, 3, 25);
    TEST_ASSERT_EQUAL_UINT32(local_midnight(2026, 4, 2),
                             local_midnight_plus_days(m, 8));
    TEST_ASSERT_EQUAL_UINT32(local_midnight(2026, 3, 1),
                             local_midnight_plus_days(local_midnight(2026, 4, 1), -31));
    // Aritmética ingenua de 86400 s daría 1 h de error
    TEST_ASSERT_NOT_EQUAL(m + 8u * 86400u, local_midnight_plus_days(m, 8));
}

void test_days_between_and_offset_across_dst() {
    uint32_t a = local_midnight(2026, 3, 20);
    uint32_t b = local_midnight(2026, 4, 5);    // cruza primavera
    TEST_ASSERT_EQUAL_UINT32(16, local_days_between(a, b));
    TEST_ASSERT_EQUAL_INT(-16, local_day_offset(a, b));
    TEST_ASSERT_EQUAL_INT(16, local_day_offset(b, a));

    uint32_t c = local_midnight(2026, 10, 1);
    uint32_t d = local_midnight(2026, 11, 1);   // cruza otoño
    TEST_ASSERT_EQUAL_UINT32(31, local_days_between(c, d));
    TEST_ASSERT_EQUAL_INT(-31, local_day_offset(c, d));
    TEST_ASSERT_EQUAL_INT(0, local_day_offset(c, c));
}

// ── RingState ─────────────────────────────────────────────────────────────
void test_ring_fills_then_wraps() {
    RingState r(4);
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT32(i, r.nextWritePhys());
        r.commitPush();
    }
    TEST_ASSERT_EQUAL_UINT32(4, r.count);
    TEST_ASSERT_EQUAL_UINT32(0, r.head);
    // Lleno: el siguiente sobrescribe el más antiguo (phys 0) y avanza head
    TEST_ASSERT_EQUAL_UINT32(0, r.nextWritePhys());
    r.commitPush();
    TEST_ASSERT_EQUAL_UINT32(4, r.count);
    TEST_ASSERT_EQUAL_UINT32(1, r.head);
    TEST_ASSERT_EQUAL_UINT32(1, r.nextWritePhys());   // ahora el más antiguo es phys 1
    TEST_ASSERT_EQUAL_UINT32(1, r.phys(0));
    TEST_ASSERT_EQUAL_UINT32(0, r.phys(3));           // el más nuevo está en phys 0
}

void test_ring_logical_phys_roundtrip() {
    RingState r(7);
    for (int i = 0; i < 20; i++) r.commitPush();      // varias vueltas
    for (uint32_t l = 0; l < r.count; l++)
        TEST_ASSERT_EQUAL_UINT32(l, r.logicalOf(r.phys(l)));
}

// Simula el anillo con un array y comprueba que el orden lógico es siempre
// de más antiguo a más reciente, con y sin desbordamiento.
void test_ring_logical_order_matches_insertion() {
    const uint32_t CAP = 5;
    RingState r(CAP);
    uint32_t file[CAP] = {};
    for (uint32_t v = 1; v <= 13; v++) {
        file[r.nextWritePhys()] = v;
        r.commitPush();
        for (uint32_t l = 0; l < r.count; l++)
            TEST_ASSERT_EQUAL_UINT32(v - r.count + 1 + l, file[r.phys(l)]);
    }
}

// ── Índice de días ────────────────────────────────────────────────────────
void test_dayidx_append_find() {
    DayIdxEntry a[4]; uint32_t n = 0;
    TEST_ASSERT_EQUAL_INT(-1, dayidx_find(a, n, 100));
    TEST_ASSERT_TRUE(dayidx_append(a, n, 4, 100, 0));
    TEST_ASSERT_TRUE(dayidx_append(a, n, 4, 200, 288));
    TEST_ASSERT_TRUE(dayidx_append(a, n, 4, 300, 576));
    TEST_ASSERT_EQUAL_INT(0, dayidx_find(a, n, 100));
    TEST_ASSERT_EQUAL_INT(2, dayidx_find(a, n, 300));
    TEST_ASSERT_EQUAL_INT(-1, dayidx_find(a, n, 250));
    TEST_ASSERT_EQUAL_INT(-1, dayidx_find(a, n, 50));
    TEST_ASSERT_EQUAL_INT(-1, dayidx_find(a, n, 999));
    TEST_ASSERT_EQUAL_UINT32(288, a[1].phys_start);
}

void test_dayidx_rejects_duplicates_and_clock_going_back() {
    DayIdxEntry a[4]; uint32_t n = 0;
    dayidx_append(a, n, 4, 200, 0);
    TEST_ASSERT_FALSE(dayidx_append(a, n, 4, 200, 5));   // duplicado
    TEST_ASSERT_FALSE(dayidx_append(a, n, 4, 100, 5));   // retrocede
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_UINT32(0, a[0].phys_start);
}

void test_dayidx_evicts_oldest_when_full() {
    DayIdxEntry a[3]; uint32_t n = 0;
    for (uint32_t d = 1; d <= 5; d++) dayidx_append(a, n, 3, d * 100, d);
    TEST_ASSERT_EQUAL_UINT32(3, n);
    TEST_ASSERT_EQUAL_INT(-1, dayidx_find(a, n, 100));
    TEST_ASSERT_EQUAL_INT(-1, dayidx_find(a, n, 200));
    TEST_ASSERT_EQUAL_INT(0, dayidx_find(a, n, 300));
    TEST_ASSERT_EQUAL_INT(2, dayidx_find(a, n, 500));
}

// Integración: anillo + índice, como hace DataStore::push/readDay
void test_ring_with_dayidx_survives_wrap() {
    const uint32_t PER_DAY = 4, CAP = 10;
    RingState r(CAP);
    DayIdxEntry idx[8]; uint32_t n = 0;
    uint32_t ts[CAP] = {};
    uint32_t t = 0;
    for (uint32_t day = 1; day <= 5; day++)
        for (uint32_t k = 0; k < PER_DAY; k++, t++) {
            uint32_t phys = r.nextWritePhys();
            if (k == 0) dayidx_append(idx, n, 8, day * 1000, phys);
            ts[phys] = day * 1000 + k;
            r.commitPush();
        }
    // 20 registros en un anillo de 10: quedan los días 4 y 5 completos
    int i5 = dayidx_find(idx, n, 5000);
    TEST_ASSERT_TRUE(i5 >= 0);
    uint32_t li = r.logicalOf(idx[i5].phys_start);
    TEST_ASSERT_EQUAL_UINT32(5000, ts[r.phys(li)]);
    TEST_ASSERT_EQUAL_UINT32(5003, ts[r.phys(li + 3)]);
    int i4 = dayidx_find(idx, n, 4000);
    TEST_ASSERT_EQUAL_UINT32(4000, ts[r.phys(r.logicalOf(idx[i4].phys_start))]);
    // El día 3 empezaba en phys 8 (ya sobrescrito parcialmente): su entrada
    // sigue existiendo pero el lector debe parar al salir del rango del día
    TEST_ASSERT_TRUE(dayidx_find(idx, n, 3000) >= 0);
}

// ── Alertas: solar ────────────────────────────────────────────────────────
void test_solar_start_needs_3_consecutive() {
    SolarMonitor m;
    TEST_ASSERT_EQUAL(SolarMonitor::Event::NONE, m.update(100));
    TEST_ASSERT_EQUAL(SolarMonitor::Event::NONE, m.update(100));
    TEST_ASSERT_EQUAL(SolarMonitor::Event::START, m.update(100));
    TEST_ASSERT_TRUE(m.active);
    TEST_ASSERT_EQUAL(SolarMonitor::Event::NONE, m.update(100));   // no repite
}

void test_solar_dip_resets_debounce() {
    SolarMonitor m;
    m.update(100); m.update(100);
    m.update(10);                                   // baja → reinicia
    // Por debajo del umbral de inicio cae en la rama "else" y el contador vuelve a 0
    TEST_ASSERT_EQUAL_UINT8(0, m.debounce);
    TEST_ASSERT_EQUAL(SolarMonitor::Event::NONE, m.update(100));
    TEST_ASSERT_EQUAL(SolarMonitor::Event::NONE, m.update(100));
    TEST_ASSERT_EQUAL(SolarMonitor::Event::START, m.update(100));
}

void test_solar_stop_and_hysteresis() {
    SolarMonitor m; m.active = true;
    // Entre 20 y 50 W no cambia nada (histéresis)
    for (int i = 0; i < 10; i++)
        TEST_ASSERT_EQUAL(SolarMonitor::Event::NONE, m.update(35));
    m.update(5); m.update(5);
    TEST_ASSERT_EQUAL(SolarMonitor::Event::STOP, m.update(5));
    TEST_ASSERT_FALSE(m.active);
    m.active = true; m.resetForNewDay();
    TEST_ASSERT_FALSE(m.active);
}

// ── Alertas: batería ──────────────────────────────────────────────────────
void test_battery_warn_then_recover_with_hysteresis() {
    BatteryMonitor b;   // warn=25, crit=20
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::NONE, b.update(50, 25, 20));
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::WARN_LOW, b.update(24, 25, 20));
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::NONE, b.update(24, 25, 20));   // sin repetir
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::NONE, b.update(29, 25, 20));   // <30: aún no
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::RECOVERED, b.update(30, 25, 20));
    TEST_ASSERT_TRUE(b.state == BatteryMonitor::State::NORMAL);
}

void test_battery_critical_path() {
    BatteryMonitor b;
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::CRITICAL, b.update(10, 25, 20));  // directo
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::NONE, b.update(10, 25, 20));
    // Sube a >= crit+5: pasa a WARN en silencio
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::NONE, b.update(26, 25, 20));
    TEST_ASSERT_TRUE(b.state == BatteryMonitor::State::WARN);
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::RECOVERED, b.update(31, 25, 20));
}

void test_battery_warn_to_critical_and_disabled_levels() {
    BatteryMonitor b;
    b.update(24, 25, 20);                                                   // WARN
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::CRITICAL, b.update(19, 25, 20));
    BatteryMonitor off;                                                     // todo a 0 = desactivado
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::NONE, off.update(1, 0, 0));
    BatteryMonitor only_warn;
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::WARN_LOW, only_warn.update(10, 25, 0));
}

void test_battery_recovery_threshold_clamps_at_100() {
    BatteryMonitor b;
    b.update(90, 98, 0);                         // WARN con warn=98 → rec=100
    TEST_ASSERT_EQUAL(BatteryMonitor::Event::RECOVERED, b.update(100, 98, 0));
}

// ── Alertas: red ──────────────────────────────────────────────────────────
void test_grid_no_outage_before_first_activity() {
    GridMonitor g;
    for (int i = 0; i < 50; i++)
        TEST_ASSERT_EQUAL(GridMonitor::Event::NONE, g.update(0));   // arranque sin red vista
}

void test_grid_outage_after_12_zero_readings_and_restore() {
    GridMonitor g;
    g.update(300);
    for (int i = 0; i < 11; i++)
        TEST_ASSERT_EQUAL(GridMonitor::Event::NONE, g.update(0));
    TEST_ASSERT_EQUAL(GridMonitor::Event::OUTAGE, g.update(0));
    TEST_ASSERT_EQUAL(GridMonitor::Event::NONE, g.update(0));       // no repite
    TEST_ASSERT_EQUAL(GridMonitor::Event::RESTORED, g.update(-150)); // exportando también es "red activa"
    TEST_ASSERT_FALSE(g.outage);
}

void test_grid_blip_resets_debounce() {
    GridMonitor g;
    g.update(300);
    for (int i = 0; i < 11; i++) g.update(0);
    g.update(10);                                                   // vuelve un instante
    for (int i = 0; i < 11; i++)
        TEST_ASSERT_EQUAL(GridMonitor::Event::NONE, g.update(0));
    TEST_ASSERT_EQUAL(GridMonitor::Event::OUTAGE, g.update(0));
}

// ── Alertas: datalogger ───────────────────────────────────────────────────
void test_logger_fail_notifies_once_until_success() {
    LoggerFailMonitor l;
    for (int i = 0; i < 4; i++) TEST_ASSERT_FALSE(l.onFailure());
    TEST_ASSERT_TRUE(l.onFailure());                // 5º fallo
    l.markNotified();
    for (int i = 0; i < 10; i++) TEST_ASSERT_FALSE(l.onFailure());
    l.onSuccess();
    for (int i = 0; i < 4; i++) TEST_ASSERT_FALSE(l.onFailure());
    TEST_ASSERT_TRUE(l.onFailure());                // se rearma tras una lectura OK
}

void test_logger_fail_not_marked_retries_at_wraparound_only() {
    // Si el llamador no marca (Telegram sin configurar), no hay aviso doble
    // en el mismo ciclo de fallos: el umbral solo se cruza una vez.
    LoggerFailMonitor l;
    for (int i = 0; i < 4; i++) l.onFailure();
    TEST_ASSERT_TRUE(l.onFailure());
    TEST_ASSERT_FALSE(l.onFailure());
}

// ── DEYE-46: espacio libre en flash ───────────────────────────────────────
void test_fs_free_percent() {
    TEST_ASSERT_EQUAL_UINT8(100, FsSpaceMonitor::freePercent(8000, 0));
    TEST_ASSERT_EQUAL_UINT8(50,  FsSpaceMonitor::freePercent(8000, 4000));
    TEST_ASSERT_EQUAL_UINT8(9,   FsSpaceMonitor::freePercent(1000, 910));    // 9 % (trunca)
    TEST_ASSERT_EQUAL_UINT8(0,   FsSpaceMonitor::freePercent(1000, 1000));
    TEST_ASSERT_EQUAL_UINT8(0,   FsSpaceMonitor::freePercent(1000, 5000));   // usado > total: no desborda
    TEST_ASSERT_EQUAL_UINT8(0,   FsSpaceMonitor::freePercent(0, 0));
}

void test_fs_warns_once_then_rearms_with_hysteresis() {
    FsSpaceMonitor m;
    const uint64_t T = 8ULL * 1024 * 1024;
    TEST_ASSERT_EQUAL(FsSpaceMonitor::Event::NONE, m.update(T, T / 2));          // 50 % libre
    TEST_ASSERT_EQUAL(FsSpaceMonitor::Event::NONE, m.update(T, T * 90 / 100));   // 10 % libre: aún no (< 10)
    TEST_ASSERT_EQUAL(FsSpaceMonitor::Event::LOW_SPACE, m.update(T, T * 95 / 100)); // 5 %
    TEST_ASSERT_EQUAL(FsSpaceMonitor::Event::NONE, m.update(T, T * 96 / 100));   // sin repetir
    TEST_ASSERT_EQUAL(FsSpaceMonitor::Event::NONE, m.update(T, T * 88 / 100));   // 12 %: entre 10 y 15, sigue desarmado
    TEST_ASSERT_TRUE(m.warned);
    TEST_ASSERT_EQUAL(FsSpaceMonitor::Event::NONE, m.update(T, T * 80 / 100));   // 20 %: se rearma
    TEST_ASSERT_FALSE(m.warned);
    TEST_ASSERT_EQUAL(FsSpaceMonitor::Event::LOW_SPACE, m.update(T, T * 97 / 100)); // vuelve a avisar
}

void test_fs_unmounted_never_warns() {
    FsSpaceMonitor m;
    TEST_ASSERT_EQUAL(FsSpaceMonitor::Event::NONE, m.update(0, 0));
    TEST_ASSERT_FALSE(m.warned);
}

// ── DEYE-08: backoff del datalogger ───────────────────────────────────────
void test_logger_backoff_schedule() {
    LoggerFailMonitor l;
    TEST_ASSERT_EQUAL_UINT32(5000, l.nextDelayMs(5000, 60000));      // sin fallos
    for (int i = 0; i < 5; i++) l.onFailure();
    TEST_ASSERT_EQUAL_UINT32(5000, l.nextDelayMs(5000, 60000));      // hasta el umbral: intervalo normal
    l.onFailure();
    TEST_ASSERT_EQUAL_UINT32(10000, l.nextDelayMs(5000, 60000));     // 6º fallo
    l.onFailure();
    TEST_ASSERT_EQUAL_UINT32(20000, l.nextDelayMs(5000, 60000));
    l.onFailure();
    TEST_ASSERT_EQUAL_UINT32(40000, l.nextDelayMs(5000, 60000));
    l.onFailure();
    TEST_ASSERT_EQUAL_UINT32(60000, l.nextDelayMs(5000, 60000));     // tope
    for (int i = 0; i < 100000; i++) l.onFailure();                  // sin desbordar
    TEST_ASSERT_EQUAL_UINT32(60000, l.nextDelayMs(5000, 60000));
    l.onSuccess();
    TEST_ASSERT_EQUAL_UINT32(5000, l.nextDelayMs(5000, 60000));      // vuelve al ritmo normal
}

void test_logger_notification_unchanged_by_backoff() {
    // El aviso sigue saltando en el 5º fallo (los 4 primeros usan 5 s: ~25 s)
    LoggerFailMonitor l;
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_FALSE(l.onFailure());
        TEST_ASSERT_EQUAL_UINT32(5000, l.nextDelayMs(5000, 60000));
    }
    TEST_ASSERT_TRUE(l.onFailure());
}

// ── DEYE-01: DailyTracker ─────────────────────────────────────────────────
struct TestDaily { float pv_kwh, export_kwh, import_kwh, load_kwh; };
typedef DailyTracker<TestDaily> TestTracker;

void test_tracker_keeps_last_good_per_day() {
    TestTracker t;
    TEST_ASSERT_NULL(t.forDay(1000));
    t.onFetch({10, 1, 2, 8}, 1000, false);
    t.onFetch({12, 1, 3, 9}, 1000, false);
    TEST_ASSERT_NOT_NULL(t.forDay(1000));
    TEST_ASSERT_EQUAL_FLOAT(12, t.forDay(1000)->pv_kwh);
    TEST_ASSERT_NULL(t.forDay(2000));
}

void test_tracker_rolls_over_and_keeps_yesterday() {
    TestTracker t;
    t.onFetch({20, 5, 3, 15}, 1000, false);   // lectura final de ayer
    t.onFetch({0, 0, 0, 0.1f}, 2000, false);  // primera de hoy (contadores reiniciados)
    TEST_ASSERT_EQUAL_FLOAT(20, t.forDay(1000)->pv_kwh);   // ayer intacto -> se puede cerrar
    TEST_ASSERT_EQUAL_FLOAT(0,  t.forDay(2000)->pv_kwh);
}

void test_tracker_rejects_early_counter_reset_before_midnight() {
    // El reloj del inversor va adelantado: pone los contadores a cero antes de
    // nuestra medianoche. En la ventana final del día esa lectura se descarta.
    TestTracker t;
    t.onFetch({20, 5, 3, 15}, 1000, true);
    TEST_ASSERT_FALSE(t.onFetch({0, 0, 0, 0}, 1000, true));
    TEST_ASSERT_EQUAL_FLOAT(20, t.forDay(1000)->pv_kwh);
    TEST_ASSERT_EQUAL_FLOAT(5,  t.forDay(1000)->export_kwh);
}

void test_tracker_accepts_decrease_outside_window_and_after_midnight() {
    TestTracker t;
    t.onFetch({20, 5, 3, 15}, 1000, false);
    // Fuera de la ventana final un descenso es válido (p.ej. reinicio del inversor)
    TEST_ASSERT_TRUE(t.onFetch({1, 0, 0, 0.5f}, 1000, false));
    TEST_ASSERT_EQUAL_FLOAT(1, t.forDay(1000)->pv_kwh);
    // Reinicio TARDÍO: ya es el día nuevo (dep distinto) y llegan ceros: se acepta
    TestTracker u;
    u.onFetch({20, 5, 3, 15}, 1000, true);
    TEST_ASSERT_TRUE(u.onFetch({20, 5, 3, 15}, 2000, false));   // aún con totales de ayer
    TEST_ASSERT_TRUE(u.onFetch({0, 0, 0, 0}, 2000, false));     // ya reiniciado: no se congela
    TEST_ASSERT_EQUAL_FLOAT(0, u.forDay(2000)->pv_kwh);
    TEST_ASSERT_EQUAL_FLOAT(20, u.forDay(1000)->pv_kwh);
}

void test_tracker_ignores_rounding_noise() {
    TestTracker t;
    t.onFetch({10.0f, 1, 1, 5}, 1000, true);
    TEST_ASSERT_TRUE(t.onFetch({9.97f, 1, 1, 5}, 1000, true));   // < EPS: ruido, no un reinicio
}

// ── DEYE-04: búsqueda en el anillo sobre la copia de epochs ───────────────
void test_ring_lower_bound_no_wrap_and_wrapped() {
    const uint32_t CAP = 6;
    RingState r(CAP);
    uint32_t eps[CAP] = {};
    // Sin vueltas: epochs 100,200,300,400
    for (uint32_t v = 1; v <= 4; v++) { eps[r.nextWritePhys()] = v * 100; r.commitPush(); }
    TEST_ASSERT_EQUAL_UINT32(0, ring_lower_bound(r, eps, 50));
    TEST_ASSERT_EQUAL_UINT32(0, ring_lower_bound(r, eps, 100));
    TEST_ASSERT_EQUAL_UINT32(2, ring_lower_bound(r, eps, 250));
    TEST_ASSERT_EQUAL_UINT32(3, ring_lower_bound(r, eps, 400));
    TEST_ASSERT_EQUAL_UINT32(4, ring_lower_bound(r, eps, 401));   // todos menores -> count
    // Con vueltas: 100..400 + 500..900 (se pierden 100,200,300)
    for (uint32_t v = 5; v <= 9; v++) { eps[r.nextWritePhys()] = v * 100; r.commitPush(); }
    TEST_ASSERT_EQUAL_UINT32(6, r.count);
    TEST_ASSERT_EQUAL_UINT32(0, ring_lower_bound(r, eps, 1));      // el más antiguo es 400
    TEST_ASSERT_EQUAL_UINT32(0, ring_lower_bound(r, eps, 400));
    TEST_ASSERT_EQUAL_UINT32(1, ring_lower_bound(r, eps, 401));
    TEST_ASSERT_EQUAL_UINT32(4, ring_lower_bound(r, eps, 800));
    TEST_ASSERT_EQUAL_UINT32(5, ring_lower_bound(r, eps, 900));
    TEST_ASSERT_EQUAL_UINT32(6, ring_lower_bound(r, eps, 901));
}

void test_ring_lower_bound_empty() {
    RingState r(4);
    uint32_t eps[4] = {};
    TEST_ASSERT_EQUAL_UINT32(0, ring_lower_bound(r, eps, 123));
}

void test_ring_find_exact_from_end() {
    RingState r(4);
    uint32_t eps[4] = {};
    TEST_ASSERT_EQUAL_INT(-1, ring_find_exact_from_end(r, eps, 10));
    for (uint32_t v = 1; v <= 6; v++) { eps[r.nextWritePhys()] = v * 10; r.commitPush(); }
    // Quedan 30,40,50,60 en orden lógico 0..3
    TEST_ASSERT_EQUAL_INT(3, ring_find_exact_from_end(r, eps, 60));
    TEST_ASSERT_EQUAL_INT(0, ring_find_exact_from_end(r, eps, 30));
    TEST_ASSERT_EQUAL_INT(-1, ring_find_exact_from_end(r, eps, 20));   // ya sobrescrito
    TEST_ASSERT_EQUAL_INT(-1, ring_find_exact_from_end(r, eps, 35));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_known_vector);
    RUN_TEST(test_build_frame_layout);
    RUN_TEST(test_parse_ok_big_endian);
    RUN_TEST(test_parse_errors);
    RUN_TEST(test_frame_total_len_from_header);
    RUN_TEST(test_exception_detected_on_short_frame);
    RUN_TEST(test_ipv4_validation);
    RUN_TEST(test_discovery_reply_parse);
    RUN_TEST(test_daily_block_fits_buffer);
    RUN_TEST(test_fs_free_percent);
    RUN_TEST(test_fs_warns_once_then_rearms_with_hysteresis);
    RUN_TEST(test_fs_unmounted_never_warns);
    RUN_TEST(test_logger_backoff_schedule);
    RUN_TEST(test_logger_notification_unchanged_by_backoff);
    RUN_TEST(test_tracker_keeps_last_good_per_day);
    RUN_TEST(test_tracker_rolls_over_and_keeps_yesterday);
    RUN_TEST(test_tracker_rejects_early_counter_reset_before_midnight);
    RUN_TEST(test_tracker_accepts_decrease_outside_window_and_after_midnight);
    RUN_TEST(test_tracker_ignores_rounding_noise);
    RUN_TEST(test_ring_lower_bound_no_wrap_and_wrapped);
    RUN_TEST(test_ring_lower_bound_empty);
    RUN_TEST(test_ring_find_exact_from_end);
    RUN_TEST(test_slot_average_basic_and_rounding);
    RUN_TEST(test_slot_average_spike_is_smoothed);
    RUN_TEST(test_slot_average_clamp_and_reset);
    RUN_TEST(test_next_midnight_normal_day);
    RUN_TEST(test_next_midnight_spring_forward_is_23h);
    RUN_TEST(test_next_midnight_fall_back_is_25h);
    RUN_TEST(test_plus_days_crosses_dst);
    RUN_TEST(test_days_between_and_offset_across_dst);
    RUN_TEST(test_ring_fills_then_wraps);
    RUN_TEST(test_ring_logical_phys_roundtrip);
    RUN_TEST(test_ring_logical_order_matches_insertion);
    RUN_TEST(test_dayidx_append_find);
    RUN_TEST(test_dayidx_rejects_duplicates_and_clock_going_back);
    RUN_TEST(test_dayidx_evicts_oldest_when_full);
    RUN_TEST(test_ring_with_dayidx_survives_wrap);
    RUN_TEST(test_solar_start_needs_3_consecutive);
    RUN_TEST(test_solar_dip_resets_debounce);
    RUN_TEST(test_solar_stop_and_hysteresis);
    RUN_TEST(test_battery_warn_then_recover_with_hysteresis);
    RUN_TEST(test_battery_critical_path);
    RUN_TEST(test_battery_warn_to_critical_and_disabled_levels);
    RUN_TEST(test_battery_recovery_threshold_clamps_at_100);
    RUN_TEST(test_grid_no_outage_before_first_activity);
    RUN_TEST(test_grid_outage_after_12_zero_readings_and_restore);
    RUN_TEST(test_grid_blip_resets_debounce);
    RUN_TEST(test_logger_fail_notifies_once_until_success);
    RUN_TEST(test_logger_fail_not_marked_retries_at_wraparound_only);
    return UNITY_END();
}
