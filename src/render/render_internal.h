#pragma once
// Внутреннее для реализаций рендера — не для игрового кода.
#include <cstddef>
#include <cstdint>

#include "render/render_api.h"

namespace render::internal {

// Линейный распределитель поверх одного блока, выделенного при init().
struct Arena {
    uint8_t* base = nullptr;
    size_t size = 0;
    size_t used = 0;

    // nullptr — не хватило места. align — степень двойки.
    void* alloc(size_t bytes, size_t align);
};

// Статистика, которую заполняют бэкенды.
FrameStats& mutable_stats();

// Строит атлас отладочного шрифта. Бэкенд вызывает в конце своего init().
bool debug_font_init();

}  // namespace render::internal
