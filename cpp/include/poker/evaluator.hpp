#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "poker/card.hpp"
#include "poker/hand.hpp"

namespace poker {

enum class HandCategory : std::uint8_t {
    high_card = 0,
    one_pair = 1,
    two_pair = 2,
    three_of_a_kind = 3,
    straight = 4,
    flush = 5,
    full_house = 6,
    four_of_a_kind = 7,
    straight_flush = 8,
};

struct HandValue {
    std::uint32_t score{0};

    constexpr HandCategory category() const noexcept {
        return static_cast<HandCategory>(score >> 24U);
    }

    friend constexpr bool operator==(HandValue lhs, HandValue rhs) noexcept {
        return lhs.score == rhs.score;
    }

    friend constexpr bool operator!=(HandValue lhs, HandValue rhs) noexcept {
        return !(lhs == rhs);
    }

    friend constexpr bool operator<(HandValue lhs, HandValue rhs) noexcept {
        return lhs.score < rhs.score;
    }

    friend constexpr bool operator>(HandValue lhs, HandValue rhs) noexcept {
        return rhs < lhs;
    }

    friend constexpr bool operator<=(HandValue lhs, HandValue rhs) noexcept {
        return !(rhs < lhs);
    }

    friend constexpr bool operator>=(HandValue lhs, HandValue rhs) noexcept {
        return !(lhs < rhs);
    }
};

HandValue evaluate(const Card* cards, std::size_t count);

inline HandValue evaluate(const std::array<Card, 7>& cards, std::size_t count) {
    return evaluate(cards.data(), count);
}

inline HandValue evaluate(const HoldemHand& hand) {
    return evaluate(hand.cards().data(), hand.total_cards());
}

}  // namespace poker