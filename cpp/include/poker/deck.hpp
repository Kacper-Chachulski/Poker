#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>

#include "poker/card.hpp"

namespace poker {

class Deck {
public:
    Deck();

    void reset() noexcept;

    template <typename RandomGenerator>
    void shuffle(RandomGenerator& generator) {
        std::shuffle(cards_.begin(), cards_.end(), generator);
        remaining_ = cards_.size();
    }

    bool draw(Card& out) noexcept;

    Card draw_unchecked() noexcept;

    std::size_t remaining() const noexcept;

    const Card* data() const noexcept;

    const Card* remaining_begin() const noexcept;

    const Card* remaining_end() const noexcept;

    const Card& operator[](std::size_t index) const noexcept;

private:
    std::array<Card, 52> cards_;
    std::size_t remaining_;
};

}  // namespace poker