#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "poker/betting.hpp"
#include "poker/equity.hpp"
#include "poker/hand.hpp"
#include "poker/range.hpp"

namespace poker {

enum class Street : std::uint8_t {
    preflop,
    flop,
    turn,
    river,
};

struct RandomOpponent {};

using Opponent = std::variant<HandCombo, HandRange, RandomOpponent>;

struct GameState {
    Street street{Street::preflop};
    HoldemHand hero{};
    BettingState betting{};
    std::vector<Opponent> opponents{};
    std::size_t player_count{0U};
};

std::size_t board_card_count(Street street);
Street street_from_board_count(std::size_t board_count);

void validate_game_state(const GameState& state);

EquityResult calculate_equity(const GameState& state, const EquityOptions& options = {});

}  // namespace poker