#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "poker/card.hpp"

namespace poker {

struct HoldemHand {
    std::array<Card, 2> hole{};
    std::array<Card, 5> board{};
    std::uint8_t board_count{0};

    std::size_t total_cards() const noexcept;

    std::array<Card, 7> cards() const noexcept;
};

}  // namespace poker