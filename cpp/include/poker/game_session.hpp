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
    utg,
    mp,
    co,
    btn,
    sb,
    bb,
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
    std::size_t seat{0U};
    SessionActionType type{SessionActionType::check};
    double amount{0.0};
};

struct SessionSeatState {
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
    std::vector<SessionSeatState> seats_{};
    std::vector<bool> occupied_{};
    std::vector<Card> board_{};
    Street street_{Street::preflop};
    double pot_{0.0};
    double current_bet_{0.0};
    std::vector<bool> acted_{};
    ActionOrderState action_order_{};
    std::size_t hero_seat_{0U};
    std::vector<std::size_t> seat_order_{};
    std::vector<SessionAction> history_{};

    void reset_hand();
    void reset_street(Street street);
    void post_blinds(std::ostream& output);
    void run_street(std::istream& input, std::ostream& output);
    bool run_betting_round(std::istream& input, std::ostream& output, std::size_t start_seat);
    void reveal_next_board(std::istream& input, std::ostream& output);

    std::size_t total_players() const;
    std::size_t occupied_players() const;
    std::size_t hero_seat() const;
    std::size_t button_seat() const;
    std::size_t small_blind_seat() const;
    std::size_t big_blind_seat() const;
    std::size_t action_start_seat(Street street) const;

    std::vector<std::size_t> active_seats() const;
    std::size_t active_opponent_count() const;
    std::size_t next_active_seat(std::size_t seat) const;
    bool betting_round_complete(std::size_t start_seat, std::size_t current_seat) const;
    std::vector<std::size_t> ordered_seats_from(std::size_t seat) const;

    void apply_posted_amount(std::size_t seat, double amount);
    void apply_fold(std::size_t seat);
    void apply_check_or_call(std::size_t seat);
    void apply_raise_to(std::size_t seat, double amount);
    void mark_all_active_to_act(std::size_t start_seat);
    void print_status_line(std::ostream& output) const;
    void print_recommendation(std::ostream& output) const;
    void print_hand_over(std::ostream& output, const std::string& message) const;
    void prompt_and_apply_action(std::istream& input, std::ostream& output, std::size_t seat);
    bool betting_round_complete() const;
    bool hand_over() const;

    BettingState build_betting_state_for_seat(std::size_t seat) const;
    std::string seat_label(std::size_t seat) const;
    std::string street_label() const;
};

void run_game_session(std::istream& input, std::ostream& output);

TablePosition parse_table_position(const std::string& text);

}  // namespace poker