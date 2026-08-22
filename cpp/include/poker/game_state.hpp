#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
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

enum class PlayerStatus : std::uint8_t {
    active,
    folded,
    all_in,
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

struct ActionOrderState {
    std::size_t player_count{0U};
    std::size_t button_seat{0U};
    Street street{Street::preflop};
    std::vector<PlayerStatus> seats{};

    void reset(Street new_street);
    void set_status(std::size_t seat, PlayerStatus status);
    PlayerStatus status(std::size_t seat) const;
    bool is_active(std::size_t seat) const;
    std::size_t active_player_count() const;
    bool hand_over() const;
    std::optional<std::size_t> first_to_act() const;
    std::optional<std::size_t> next_to_act(std::size_t from_seat) const;
};

std::size_t board_card_count(Street street);
Street street_from_board_count(std::size_t board_count);

void validate_game_state(const GameState& state);

void validate_action_order_state(const ActionOrderState& state);

std::uint64_t theoretical_exact_states(const GameState& state);

EquityResult calculate_equity(const GameState& state, const EquityOptions& options = {});

}  // namespace poker