#pragma once
#include <stdint.h>
#include <time.h>

// DEYE-05: aritmética de días en hora LOCAL.
// Un "día" no siempre dura 86400 s: el domingo del cambio de hora de primavera
// dura 23 h y el de otoño 25 h. Sumar/restar 86400 a una medianoche local
// produce epochs desfasados 1 h en esos días. Estas funciones normalizan con
// mktime (tm_isdst=-1) para obtener siempre la medianoche local real.

// Medianoche local de (día de `midnight` + n_days). `midnight` debe ser una
// medianoche local (o cualquier instante del día: se normaliza a 00:00).
inline uint32_t local_midnight_plus_days(uint32_t midnight, int n_days) {
    time_t t = (time_t)midnight;
    struct tm tm_r; localtime_r(&t, &tm_r);
    tm_r.tm_mday += n_days;
    tm_r.tm_hour = 0; tm_r.tm_min = 0; tm_r.tm_sec = 0; tm_r.tm_isdst = -1;
    return (uint32_t)mktime(&tm_r);
}

// Medianoche local del día siguiente (límite superior exclusivo del día).
inline uint32_t next_local_midnight(uint32_t midnight) {
    time_t t = (time_t)midnight;
    struct tm tm_r; localtime_r(&t, &tm_r);
    tm_r.tm_mday += 1;
    tm_r.tm_hour = 0; tm_r.tm_min = 0; tm_r.tm_sec = 0; tm_r.tm_isdst = -1;
    return (uint32_t)mktime(&tm_r);
}

// Nº de días naturales entre dos medianoches locales (a <= b), robusto a DST.
inline uint32_t local_days_between(uint32_t a, uint32_t b) {
    if (b <= a) return 0;
    return (uint32_t)(((uint64_t)(b - a) + 43200ULL) / 86400ULL);   // redondeo
}

// Diferencia en días naturales con signo (dep - today), robusta a DST.
// A diferencia de `(dep - today) / 86400`, no trunca hacia cero.
inline int local_day_offset(uint32_t dep, uint32_t today) {
    int64_t d = (int64_t)dep - (int64_t)today;
    return (int)((d >= 0 ? d + 43200 : d - 43200) / 86400);
}
