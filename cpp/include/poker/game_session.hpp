#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <array>
#include <vector>

#include "poker/card.hpp"
#include "poker/decision.hpp"
#include "poker/equity.hpp"
#include "poker/game_state.hpp"

namespace poker {

enum class TablePosition : std::uint8_t {
    btn,
    sb,
    bb,
    utg,
    utg_plus_1,
    utg_plus_2,
};

struct GameSessionConfig {
    std::array<Card, 2> hero_cards{};
    std::size_t opponents{0U};
    double small_blind{0.0};
    double big_blind{0.0};
    std::size_t hero_position{1U};
    double starting_stack{0.0};
    EquityOptions equity_options{};
};

enum class SessionActionType : std::uint8_t {
    check,
    call,
    fold,
    bet,
    raise,
    all_in,
};

struct SessionAction {
    TablePosition position{TablePosition::btn};
    SessionActionType type{SessionActionType::check};
    double amount{0.0};
};

struct SessionPlayerState {
    bool hero{false};
    bool active{true};
    double stack{0.0};
    double contribution{0.0};
};

class GameSession {
public:
    explicit GameSession(GameSessionConfig config);

    void run(std::istream& input, std::ostream& output);

    GameState current_game_state() const;

private:
    GameSessionConfig config_{};
    std::vector<SessionPlayerState> players_{}; // aligned with player_positions_
    std::vector<TablePosition> player_positions_{}; // canonical positions present this hand (clockwise from BTN)
    std::vector<Card> board_{};
    Street street_{Street::preflop};
    double pot_{0.0};
    double current_bet_{0.0};
    std::vector<bool> acted_{}; // aligned with player_positions_
    ActionOrderState action_order_{};
    TablePosition hero_position_{TablePosition::btn};
    std::vector<TablePosition> position_order_{}; // legacy name for clarity
    std::vector<SessionAction> history_{};

    void reset_hand();
    void reset_street(Street street);
    void post_blinds(std::ostream& output);
    void run_street(std::istream& input, std::ostream& output);
    bool run_betting_round(std::istream& input, std::ostream& output, TablePosition start_pos);
    void reveal_next_board(std::istream& input, std::ostream& output);

    std::size_t total_players() const;
    std::size_t occupied_players() const;
    TablePosition hero_position() const;
    TablePosition button_position() const;
    TablePosition small_blind_position() const;
    TablePosition big_blind_position() const;
    TablePosition action_start_position(Street street) const;

    std::vector<TablePosition> active_positions() const;
    std::size_t active_opponent_count() const;
    TablePosition next_active_position(TablePosition pos) const;
    bool betting_round_complete(TablePosition start_pos, TablePosition current_pos) const;
    std::vector<TablePosition> ordered_positions_from(TablePosition pos) const;

    void apply_posted_amount(TablePosition pos, double amount);
    void apply_fold(TablePosition pos);
    void apply_check_or_call(TablePosition pos);
    void apply_raise_to(TablePosition pos, double amount);
    void mark_all_active_to_act(TablePosition start_pos);
    void print_status_line(std::ostream& output) const;
    void print_recommendation(std::ostream& output) const;
    void print_hand_over(std::ostream& output, const std::string& message) const;
    void prompt_and_apply_action(std::istream& input, std::ostream& output, TablePosition pos);
    bool betting_round_complete() const;
    bool hand_over() const;

    BettingState build_betting_state_for_position(TablePosition pos) const;
    std::string position_label(TablePosition pos) const;
    std::string street_label() const;
};

void run_game_session(std::istream& input, std::ostream& output);

TablePosition parse_table_position(const std::string& text);

}  // namespace poker