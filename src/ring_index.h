#pragma once
#include <stdint.h>
#include <string.h>

// Lógica pura del almacenamiento (DEYE-60): buffer circular e índice de días,
// sin dependencias de Arduino/LittleFS para poder probarla en nativo.

// ── Buffer circular ───────────────────────────────────────────────────────
// Posiciones LÓGICAS (0 = el registro más antiguo) y FÍSICAS (offset en el
// fichero). Mientras no se llena, head = 0 y count crece; una vez lleno, cada
// push sobrescribe el más antiguo y avanza head.
struct RingState {
    uint32_t capacity;
    uint32_t head;
    uint32_t count;

    explicit RingState(uint32_t cap = 0) : capacity(cap), head(0), count(0) {}

    uint32_t phys(uint32_t logical) const { return (head + logical) % capacity; }
    // Posición física donde irá el próximo registro (no modifica el estado)
    uint32_t nextWritePhys() const { return count < capacity ? phys(count) : head; }
    // Registra que se ha añadido un registro en nextWritePhys()
    void commitPush() {
        if (count < capacity) count++;
        else head = (head + 1) % capacity;
    }
    // Posición lógica actual de una posición física (cambia al avanzar head)
    uint32_t logicalOf(uint32_t phys_pos) const {
        return (phys_pos - head + capacity) % capacity;
    }
};

// Búsqueda binaria en el anillo SIN leer del fichero (DEYE-04).
// `epochs[phys]` es una copia en RAM del epoch (hour_epoch/day_epoch) de cada
// posición física. Los registros están en orden cronológico creciente en
// posición lógica. Devuelve la primera posición lógica con epoch >= ts
// (count si todos son menores).
inline uint32_t ring_lower_bound(const RingState& r, const uint32_t* epochs, uint32_t ts) {
    uint32_t lo = 0, hi = r.count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (epochs[r.phys(mid)] < ts) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

// Posición lógica del registro con exactamente ese epoch, o -1. Busca desde el
// final: el registro que se actualiza casi siempre es el más reciente.
inline int ring_find_exact_from_end(const RingState& r, const uint32_t* epochs, uint32_t ts) {
    for (uint32_t k = r.count; k > 0; k--)
        if (epochs[r.phys(k - 1)] == ts) return (int)(k - 1);
    return -1;
}

// ── Índice de días ────────────────────────────────────────────────────────
// Guarda la posición FÍSICA (inmutable) del primer registro de cada día.
// Ordenado por day_epoch ascendente: permite búsqueda binaria.
struct DayIdxEntry { uint32_t day_epoch; uint32_t phys_start; };

// Búsqueda binaria; devuelve el índice o -1.
inline int dayidx_find(const DayIdxEntry* a, uint32_t count, uint32_t dep) {
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (a[mid].day_epoch < dep) lo = mid + 1;
        else hi = mid;
    }
    return (lo < count && a[lo].day_epoch == dep) ? (int)lo : -1;
}

// Añade un día al final. Solo acepta epochs estrictamente crecientes (mantiene
// el orden; un reloj que retrocede no corrompe el índice). Si está lleno,
// descarta el día más antiguo. Devuelve true si se insertó.
inline bool dayidx_append(DayIdxEntry* a, uint32_t& count, uint32_t max,
                          uint32_t dep, uint32_t phys_start) {
    if (count > 0 && dep <= a[count - 1].day_epoch) return false;
    if (count >= max) {
        memmove(a, a + 1, (count - 1) * sizeof(DayIdxEntry));
        count--;
    }
    a[count++] = {dep, phys_start};
    return true;
}
