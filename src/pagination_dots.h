#pragma once
#include <lvgl.h>

// N: número de tiles
void pagination_dots_init(lv_obj_t* parent, int n);
void pagination_dots_set(int active_idx);

// DEYE-56: los puntos son tocables. Al tocar uno (o junto a él, en la franja
// inferior) se llama a `cb` con su índice. Se usa un manejador del dispositivo de
// entrada en vez de hacer clicable la barra: así no se pierden los deslizamientos
// que empiezan en esa franja (la barra no es hija del tileview y no los reenviaría).
typedef void (*pagination_dots_cb_t)(int idx);
void pagination_dots_set_handler(pagination_dots_cb_t cb);
