#pragma once
// Внутреннее состояние ядра внутри World::priv. НЕ контракт: структуру расширяют T022/T023
// по мере реализации. Снаружи ядра не читать и не писать.
// Требование: тривиально копируемая, без указателей на себя и без аллокаций.
#include <cstdint>

namespace core {
namespace detail {

struct WorldPrivate {
    uint32_t reserved = 0;
};

}  // namespace detail
}  // namespace core
