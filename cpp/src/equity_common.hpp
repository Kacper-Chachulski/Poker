#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "poker/hand.hpp"

namespace poker::detail {

constexpr std::size_t kDeckSize = 52U;
constexpr std::size_t kMaxKnownCards = 7U;
constexpr std::size_t kMaxOpponents = 5U;

inline void throw_invalid(const char* message) {
    throw std::invalid_argument(message);
}

inline bool is_valid_board_size(std::size_t board_count) noexcept {
    return board_count == 0U || board_count == 3U || board_count == 4U || board_count == 5U;
}

inline void validate_hand(const HoldemHand& hero) {
    if (!hero.hole[0].valid() || !hero.hole[1].valid()) {
        throw_invalid("Hero must contain exactly 2 valid cards");
    }

    if (hero.hole[0] == hero.hole[1]) {
        throw_invalid("Hero cards must be unique");
    }

    if (!is_valid_board_size(hero.board_count)) {
        throw_invalid("Board must contain 0, 3, 4, or 5 cards");
    }

    for (std::size_t index = 0; index < hero.board_count; ++index) {
        if (!hero.board[index].valid()) {
            throw_invalid("Board contains an invalid card");
        }
    }

    std::array<Card, kMaxKnownCards> known_cards{};
    known_cards[0] = hero.hole[0];
    known_cards[1] = hero.hole[1];
    for (std::size_t index = 0; index < hero.board_count; ++index) {
        known_cards[2U + index] = hero.board[index];
    }

    for (std::size_t left = 0; left < 2U + hero.board_count; ++left) {
        for (std::size_t right = left + 1U; right < 2U + hero.board_count; ++right) {
            if (known_cards[left] == known_cards[right]) {
                throw_invalid("Duplicate cards are not allowed");
            }
        }
    }
}

inline std::size_t known_card_count(const HoldemHand& hero) noexcept {
    return 2U + hero.board_count;
}

inline std::size_t missing_board_cards(const HoldemHand& hero) noexcept {
    return 5U - hero.board_count;
}

inline std::size_t max_supported_opponents(const HoldemHand& hero) noexcept {
    const std::size_t remaining_after_known = kDeckSize - known_card_count(hero);
    const std::size_t usable_cards = remaining_after_known - missing_board_cards(hero);
    const std::size_t deck_limit = usable_cards / 2U;
    return (deck_limit < kMaxOpponents) ? deck_limit : kMaxOpponents;
}

inline void validate_opponents(const HoldemHand& hero, std::size_t opponents) {
    if (opponents > kMaxOpponents) {
        throw_invalid("Opponents must be between 0 and 5");
    }

    const std::size_t maximum = max_supported_opponents(hero);
    if (opponents > maximum) {
        throw_invalid("Not enough cards remaining for the requested board and opponents");
    }
}

inline void validate_equity_request(const HoldemHand& hero, std::size_t opponents) {
    validate_hand(hero);
    validate_opponents(hero, opponents);
}

inline void validate_simulations(std::uint64_t simulations) {
    if (simulations == 0U) {
        throw_invalid("Simulations must be greater than 0");
    }
}

}  // namespace poker::detail