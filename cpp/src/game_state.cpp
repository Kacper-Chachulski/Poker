#include "poker/game_state.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include "equity_common.hpp"
#include "poker/equity.hpp"

namespace poker {

namespace {

std::size_t board_count_for_street(Street street) {
    switch (street) {
        case Street::preflop: return 0U;
        case Street::flop: return 3U;
        case Street::turn: return 4U;
        case Street::river: return 5U;
    }

    detail::throw_invalid("Invalid street");
    return 0U;
}

std::vector<Card> collect_known_cards(const GameState& state) {
    std::vector<Card> known_cards{};
    known_cards.reserve(17U);

    known_cards.push_back(state.hero.hole[0]);
    known_cards.push_back(state.hero.hole[1]);
    for (std::size_t index = 0U; index < state.hero.board_count; ++index) {
        known_cards.push_back(state.hero.board[index]);
    }

    for (const Opponent& opponent : state.opponents) {
        if (const HandCombo* combo = std::get_if<HandCombo>(&opponent)) {
            const std::array<Card, 2> cards = combo->cards();
            known_cards.push_back(cards[0]);
            known_cards.push_back(cards[1]);
        }
    }

    return known_cards;
}

void validate_known_cards(const std::vector<Card>& cards) {
    for (std::size_t left = 0U; left < cards.size(); ++left) {
        for (std::size_t right = left + 1U; right < cards.size(); ++right) {
            if (cards[left] == cards[right]) {
                detail::throw_invalid("Duplicate cards are not allowed");
            }
        }
    }
}

std::size_t random_opponent_count(const GameState& state) {
    std::size_t count = 0U;
    for (const Opponent& opponent : state.opponents) {
        if (std::holds_alternative<RandomOpponent>(opponent)) {
            ++count;
        }
    }
    return count;
}

std::size_t known_opponent_count(const GameState& state) {
    std::size_t count = 0U;
    for (const Opponent& opponent : state.opponents) {
        if (!std::holds_alternative<RandomOpponent>(opponent)) {
            ++count;
        }
    }
    return count;
}

HandRange make_singleton_range(const HandCombo& combo) {
    return HandRange::from_combo(combo);
}

}  // namespace

std::size_t board_card_count(Street street) {
    return board_count_for_street(street);
}

Street street_from_board_count(std::size_t board_count) {
    switch (board_count) {
        case 0U: return Street::preflop;
        case 3U: return Street::flop;
        case 4U: return Street::turn;
        case 5U: return Street::river;
        default: detail::throw_invalid("Board must contain 0, 3, 4, or 5 cards");
    }

    return Street::preflop;
}

void validate_game_state(const GameState& state) {
    detail::validate_hand(state.hero);
    validate_betting_state(state.betting);

    if (state.street != street_from_board_count(state.hero.board_count)) {
        detail::throw_invalid("Street and board card count do not match");
    }

    if (state.player_count < 2U) {
        detail::throw_invalid("Player count must be at least 2");
    }

    if (state.player_count > 6U) {
        detail::throw_invalid("Player count cannot exceed 6");
    }

    if (state.opponents.size() != state.player_count - 1U) {
        detail::throw_invalid("Player count must match the number of opponents plus hero");
    }

    for (const Opponent& opponent : state.opponents) {
        if (const HandCombo* combo = std::get_if<HandCombo>(&opponent)) {
            if (!combo->valid()) {
                detail::throw_invalid("Opponent hand must contain exactly 2 valid cards");
            }
        } else if (const HandRange* range = std::get_if<HandRange>(&opponent)) {
            if (range->empty()) {
                detail::throw_invalid("Opponent range must not be empty");
            }
        }
    }

    validate_known_cards(collect_known_cards(state));
}

EquityResult calculate_equity(const GameState& state, const EquityOptions& options) {
    validate_game_state(state);

    const std::size_t random_count = random_opponent_count(state);
    const std::size_t known_count = known_opponent_count(state);

    if (known_count == 0U) {
        return calculate_equity(state.hero, random_count, options);
    }

    if (state.opponents.size() != 1U) {
        detail::throw_invalid("GameState equity currently supports only one known opponent or all-random opponents");
    }

    const Opponent& opponent = state.opponents.front();
    if (const HandCombo* combo = std::get_if<HandCombo>(&opponent)) {
        return calculate_equity(state.hero, make_singleton_range(*combo), options);
    }

    if (const HandRange* range = std::get_if<HandRange>(&opponent)) {
        return calculate_equity(state.hero, *range, options);
    }

    return calculate_equity(state.hero, 1U, options);
}

}  // namespace poker