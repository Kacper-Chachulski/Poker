#include "poker/game_session.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "equity_common.hpp"
#include "poker/ev.hpp"

namespace poker {

namespace {

std::string to_upper_copy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return text;
}

std::optional<Card> parse_card_token(const std::string& token) {
    return Card::from_string(token);
}

std::string recommendation_name(BettingAction action) {
    switch (action) {
        case BettingAction::check: return "CHECK";
        case BettingAction::bet: return "BET";
        case BettingAction::call: return "CALL";
        case BettingAction::raise: return "RAISE";
        case BettingAction::fold: return "FOLD";
        case BettingAction::all_in: return "ALL-IN";
    }
    return "UNKNOWN";
}

}  // namespace

TablePosition parse_table_position(const std::string& text) {
    const std::string upper = to_upper_copy(text);
    if (upper == "UTG") return TablePosition::utg;
    if (upper == "MP") return TablePosition::mp;
    if (upper == "CO") return TablePosition::co;
    if (upper == "BTN") return TablePosition::btn;
    if (upper == "SB") return TablePosition::sb;
    if (upper == "BB") return TablePosition::bb;
    throw std::invalid_argument("Invalid position: " + text);
}

GameSession::GameSession(GameSessionConfig config) : config_(config) {
    reset_hand();
}

std::size_t GameSession::total_players() const { return 6U; }

std::size_t GameSession::hero_seat() const {
    return hero_seat_;
}

std::size_t GameSession::button_seat() const {
    return (hero_seat_ + 1U) % total_players();
}

std::size_t GameSession::small_blind_seat() const {
    return (button_seat() + 1U) % total_players();
}

std::size_t GameSession::big_blind_seat() const { return (small_blind_seat() + 1U) % total_players(); }

void GameSession::reset_hand() {
    if (config_.hero_position < 1U || config_.hero_position > total_players()) {
        throw std::invalid_argument("Hero position must be between 1 and 6");
    }
    hero_seat_ = config_.hero_position - 1U;
    seats_.assign(6U, SessionSeatState{});
    acted_.assign(6U, false);
    for (std::size_t seat = 0U; seat < seats_.size(); ++seat) {
        seats_[seat].stack = config_.starting_stack;
        seats_[seat].active = false;
    }
    seats_[hero_seat()].hero = true;
    seats_[hero_seat()].active = true;
    std::size_t assigned = 0U;
    for (std::size_t offset = 1U; assigned < config_.opponents; ++offset) {
        const std::size_t seat = (hero_seat_ + offset) % 6U;
        if (seat == hero_seat_) {
            continue;
        }
        if (!seats_[seat].hero && !seats_[seat].active) {
            seats_[seat].active = true;
            ++assigned;
        }
    }
    board_.clear();
    street_ = Street::preflop;
    pot_ = 0.0;
    current_bet_ = 0.0;
    history_.clear();
    action_order_.player_count = total_players();
    action_order_.button_seat = button_seat();
    action_order_.street = street_;
    action_order_.seats.assign(total_players(), PlayerStatus::folded);
    for (std::size_t seat = 0U; seat < seats_.size(); ++seat) {
        action_order_.seats[seat] = seats_[seat].active ? PlayerStatus::active : PlayerStatus::folded;
    }

    seat_order_.clear();
    seat_order_.reserve(total_players());
    for (std::size_t offset = 0U; offset < total_players(); ++offset) {
        seat_order_.push_back((hero_seat_ + offset) % total_players());
    }
}

void GameSession::reset_street(Street street) {
    street_ = street;
    current_bet_ = 0.0;
    action_order_.reset(street);
    for (std::size_t seat = 0U; seat < seats_.size(); ++seat) {
        if (seats_[seat].active) {
            seats_[seat].contribution = 0.0;
        }
        acted_[seat] = !seats_[seat].active;
    }
}

std::string GameSession::seat_label(std::size_t seat) const {
    if (seat == hero_seat()) return "Hero";
    return std::string("Player ") + std::to_string(seat + 1U);
}

std::string GameSession::street_label() const {
    switch (street_) {
        case Street::preflop: return "PREFLOP";
        case Street::flop: return "FLOP";
        case Street::turn: return "TURN";
        case Street::river: return "RIVER";
    }
    return "PREFLOP";
}

void GameSession::apply_posted_amount(std::size_t seat, double amount) {
    seats_[seat].stack -= amount;
    seats_[seat].contribution += amount;
    pot_ += amount;
    current_bet_ = std::max(current_bet_, seats_[seat].contribution);
}

void GameSession::apply_fold(std::size_t seat) { seats_[seat].active = false; }

void GameSession::mark_all_active_to_act(std::size_t start_seat) {
    std::fill(acted_.begin(), acted_.end(), true);
    std::size_t seat = start_seat;
    for (std::size_t count = 0U; count < seats_.size(); ++count) {
        if (seats_[seat].active) {
            acted_[seat] = false;
        }
        seat = next_active_seat(seat);
    }
}

void GameSession::apply_check_or_call(std::size_t seat) {
    const double to_call = std::max(0.0, current_bet_ - seats_[seat].contribution);
    apply_posted_amount(seat, std::min(to_call, seats_[seat].stack));
}

void GameSession::apply_raise_to(std::size_t seat, double amount) {
    if (amount > seats_[seat].stack + seats_[seat].contribution) {
        throw std::invalid_argument("Raise exceeds remaining stack");
    }
    const double needed = amount - seats_[seat].contribution;
    if (needed < 0.0) {
        throw std::invalid_argument("Raise amount must not be below current contribution");
    }
    apply_posted_amount(seat, needed);
    current_bet_ = std::max(current_bet_, seats_[seat].contribution);
}

BettingState GameSession::build_betting_state_for_seat(std::size_t seat) const {
    BettingState betting{};
    betting.current_pot = pot_;
    betting.call_amount = std::max(0.0, current_bet_ - seats_[seat].contribution);
    betting.hero_stack = seats_[seat].stack;
    betting.check_allowed = betting.call_amount == 0.0;
    betting.minimum_raise_amount = betting.call_amount == 0.0 ? 1.0 : std::max(1.0, betting.call_amount);
    return betting;
}

std::vector<std::size_t> GameSession::active_seats() const {
    std::vector<std::size_t> seats;
    for (std::size_t seat = 0U; seat < seats_.size(); ++seat) {
        if (seats_[seat].active) {
            seats.push_back(seat);
        }
    }
    return seats;
}

std::vector<std::size_t> GameSession::ordered_seats_from(std::size_t seat) const {
    std::vector<std::size_t> ordered;
    ordered.reserve(total_players());
    for (std::size_t offset = 0U; offset < total_players(); ++offset) {
        ordered.push_back((seat + offset) % total_players());
    }
    return ordered;
}

std::size_t GameSession::active_opponent_count() const {
    std::size_t count = 0U;
    for (std::size_t seat = 0U; seat < seats_.size(); ++seat) {
        if (seat != hero_seat() && seats_[seat].active) {
            ++count;
        }
    }
    return count;
}

std::size_t GameSession::next_active_seat(std::size_t seat) const {
    const std::optional<std::size_t> next = action_order_.next_to_act(seat);
    return next.value_or(seat);
}

std::size_t GameSession::action_start_seat(Street street) const {
    ActionOrderState order = action_order_;
    order.street = street;
    const std::optional<std::size_t> start = order.first_to_act();
    return start.value_or(button_seat());
}

bool GameSession::betting_round_complete(std::size_t start_seat, std::size_t current_seat) const {
    for (std::size_t seat = 0U; seat < seats_.size(); ++seat) {
        if (seats_[seat].active && seat != current_seat && seats_[seat].contribution != current_bet_) {
            return false;
        }
    }
    return current_seat == start_seat || seats_[start_seat].contribution == current_bet_;
}

bool GameSession::betting_round_complete() const {
    for (std::size_t seat = 0U; seat < seats_.size(); ++seat) {
        if (!seats_[seat].active) {
            continue;
        }
        if (seats_[seat].contribution != current_bet_ || !acted_[seat]) {
            return false;
        }
    }
    return true;
}

bool GameSession::hand_over() const {
    return active_opponent_count() == 0U || seats_[hero_seat()].active == false || action_order_.hand_over();
}

void GameSession::print_status_line(std::ostream& output) const {
    const BettingState betting = build_betting_state_for_seat(hero_seat());
    HoldemHand hero{};
    hero.hole[0] = config_.hero_cards[0];
    hero.hole[1] = config_.hero_cards[1];
    hero.board_count = static_cast<std::uint8_t>(board_.size());
    for (std::size_t index = 0U; index < board_.size(); ++index) {
        hero.board[index] = board_[index];
    }
    EquityOptions equity_options = config_.equity_options;
    const std::uint64_t theoretical_states = theoretical_exact_states(hero, active_opponent_count());
    if (!detail::exact_equity_allowed(theoretical_states)) {
        equity_options.method = EquityMethod::monte_carlo;
        if (equity_options.simulations == 0U) {
            equity_options.simulations = 1000U;
        }
    }
    EquityResult equity = calculate_equity(hero, active_opponent_count(), equity_options);
    const auto pot_odds = calculate_pot_odds(betting);
    output << "\nPOT: " << pot_ << "\n";
    output << "TO CALL: " << betting.call_amount << "\n";
    output << "EQUITY: " << std::fixed << std::setprecision(2) << equity.equity * 100.0 << "%\n";
    if (betting.call_amount > 0.0) {
        output << "REQUIRED EQUITY: " << std::fixed << std::setprecision(2) << pot_odds.required_equity * 100.0 << "%\n";
    }
    const DecisionResult decision = evaluate_decision(betting, equity.equity);
    if (decision.best_action.has_value()) {
        output << "RECOMMENDATION: " << recommendation_name(*decision.best_action);
        if (*decision.best_action == BettingAction::bet || *decision.best_action == BettingAction::raise || *decision.best_action == BettingAction::all_in) {
            if (decision.suggested_amount.has_value()) {
                output << " TO " << *decision.suggested_amount;
            }
        }
        output << "\n";
        if (decision.rationale.has_value()) {
            output << "Reason: " << *decision.rationale << "\n";
        }
    }
}

void GameSession::prompt_and_apply_action(std::istream& input, std::ostream& output, std::size_t seat) {
    const BettingState betting = build_betting_state_for_seat(seat);
    if (seat == hero_seat()) {
        HoldemHand hero{};
        hero.hole[0] = config_.hero_cards[0];
        hero.hole[1] = config_.hero_cards[1];
        hero.board_count = static_cast<std::uint8_t>(board_.size());
        for (std::size_t index = 0U; index < board_.size(); ++index) {
            hero.board[index] = board_[index];
        }

        EquityOptions equity_options = config_.equity_options;
        const std::uint64_t theoretical_states = theoretical_exact_states(hero, active_opponent_count());
        if (!detail::exact_equity_allowed(theoretical_states)) {
            equity_options.method = EquityMethod::monte_carlo;
            if (equity_options.simulations == 0U) {
                equity_options.simulations = 1000U;
            }
        }

        const EquityResult equity = calculate_equity(hero, active_opponent_count(), equity_options);
        const DecisionResult decision = evaluate_decision(betting, equity.equity);
        output << "POT: " << pot_ << "\n";
        output << "TO CALL: " << betting.call_amount << "\n";
        output << "EQUITY: " << std::fixed << std::setprecision(2) << equity.equity * 100.0 << "%\n";
        if (betting.call_amount > 0.0) {
            const PotOddsResult pot_odds = calculate_pot_odds(betting);
            output << "REQUIRED EQUITY: " << std::fixed << std::setprecision(2) << pot_odds.required_equity * 100.0 << "%\n";
        }
        if (decision.best_action.has_value()) {
            output << "RECOMMENDATION: " << recommendation_name(*decision.best_action);
            if (decision.suggested_amount.has_value()) {
                output << " TO " << *decision.suggested_amount;
            }
            output << "\n";
        }
        output << "Hero action [c/f/r <amount>]: ";
    } else {
        output << seat_label(seat) << " action [c/f/r <amount>]: ";
    }

    std::string command;
    std::getline(input, command);
    if (command.empty()) {
        throw std::invalid_argument("Action is required");
    }

    std::istringstream stream(command);
    std::string verb;
    stream >> verb;
    if (verb == "f") {
        apply_fold(seat);
        history_.push_back({seat, SessionActionType::fold, 0.0});
        return;
    }

    if (verb == "c") {
        const double to_call = std::max(0.0, current_bet_ - seats_[seat].contribution);
        if (to_call == 0.0) {
            history_.push_back({seat, SessionActionType::check, 0.0});
        } else {
            apply_check_or_call(seat);
            history_.push_back({seat, SessionActionType::call, to_call});
        }
        return;
    }

    if (verb == "r") {
        double amount = 0.0;
        if (!(stream >> amount)) {
            throw std::invalid_argument("r requires an amount");
        }
        apply_raise_to(seat, amount);
        for (std::size_t other = 0U; other < acted_.size(); ++other) {
            if (seats_[other].active && other != seat) {
                acted_[other] = false;
            }
        }
        history_.push_back({seat, current_bet_ == amount ? SessionActionType::bet : SessionActionType::raise, amount});
        return;
    }

    throw std::invalid_argument("Unknown action: " + verb);
}

GameState GameSession::current_game_state() const {
    GameState state{};
    state.street = street_;
    state.hero.hole[0] = config_.hero_cards[0];
    state.hero.hole[1] = config_.hero_cards[1];
    state.hero.board_count = static_cast<std::uint8_t>(board_.size());
    for (std::size_t index = 0U; index < board_.size(); ++index) {
        state.hero.board[index] = board_[index];
    }
    state.betting.current_pot = pot_;
    state.betting.call_amount = std::max(0.0, current_bet_ - seats_[hero_seat()].contribution);
    state.betting.hero_stack = seats_[hero_seat()].stack;
    state.betting.check_allowed = state.betting.call_amount == 0.0;
    state.betting.minimum_raise_amount = 1.0;
    state.player_count = active_opponent_count() + 1U;
    for (std::size_t seat = 0U; seat < seats_.size(); ++seat) {
        if (seat == hero_seat() || !seats_[seat].active) continue;
        state.opponents.emplace_back(RandomOpponent{});
    }
    return state;
}

void GameSession::post_blinds(std::ostream& output) {
    if (total_players() < 2U) return;
    apply_posted_amount(small_blind_seat(), config_.small_blind);
    apply_posted_amount(big_blind_seat(), config_.big_blind);
    output << "Blinds posted.\n";
}

void GameSession::reveal_next_board(std::istream& input, std::ostream& output) {
    std::string line;
    if (street_ == Street::preflop) {
        output << "Enter flop (3 cards): ";
        std::getline(input, line);
        std::istringstream stream(line);
        std::string token;
        while (stream >> token) {
            auto parsed = parse_card_token(token);
            if (!parsed) throw std::invalid_argument("Invalid flop card");
            board_.push_back(*parsed);
        }
        if (board_.size() != 3U) throw std::invalid_argument("Flop requires 3 cards");
        street_ = Street::flop;
    } else if (street_ == Street::flop) {
        output << "Enter turn card: ";
        std::getline(input, line);
        auto parsed = parse_card_token(line);
        if (!parsed) throw std::invalid_argument("Invalid turn card");
        board_.push_back(*parsed);
        street_ = Street::turn;
    } else if (street_ == Street::turn) {
        output << "Enter river card: ";
        std::getline(input, line);
        auto parsed = parse_card_token(line);
        if (!parsed) throw std::invalid_argument("Invalid river card");
        board_.push_back(*parsed);
        street_ = Street::river;
    }
}

void GameSession::run_street(std::istream& input, std::ostream& output) {
    const std::size_t start_seat = action_start_seat(street_);
    mark_all_active_to_act(start_seat);
    (void)run_betting_round(input, output, start_seat);
}

bool GameSession::run_betting_round(std::istream& input, std::ostream& output, std::size_t start_seat) {
    if (hand_over()) {
        return true;
    }
    std::size_t current = start_seat;
    while (true) {
        if (!seats_[current].active) {
            acted_[current] = true;
        } else if (!acted_[current]) {
            prompt_and_apply_action(input, output, current);
            acted_[current] = true;
            action_order_.seats[current] = seats_[current].active ? PlayerStatus::active : PlayerStatus::folded;
            if (hand_over()) {
                return true;
            }
            if (betting_round_complete()) {
                return false;
            }
        }

        current = next_active_seat(current);
        if (current == start_seat && betting_round_complete()) {
            return false;
        }
    }
}

void GameSession::run(std::istream& input, std::ostream& output) {
    reset_hand();
    output << "=== NEW HAND ===\n";
    output << "Hero cards: " << config_.hero_cards[0].to_string() << ' ' << config_.hero_cards[1].to_string() << "\n";
    output << "Opponents: " << config_.opponents << "\n";
    output << "SB: " << config_.small_blind << "\n";
    output << "BB: " << config_.big_blind << "\n";
    output << "Position: " << config_.hero_position << "\n";
    output << "Starting stack: " << config_.starting_stack << "\n";

    post_blinds(output);
    output << "=== PREFLOP ===\n";
    output << "Pot: " << pot_ << " | To call: " << std::max(0.0, current_bet_ - seats_[hero_seat()].contribution) << "\n";
    if (run_betting_round(input, output, action_start_seat(Street::preflop))) {
        output << "Hero folded. Hand over.\n";
        return;
    }

    if (active_opponent_count() <= 0U) {
        output << "Hand ended.\n";
        return;
    }

    output << "=== FLOP ===\n";
    reveal_next_board(input, output);
    reset_street(Street::flop);
    output << "=== FLOP BETTING ===\n";
    output << "Pot: " << pot_ << " | To call: 0\n";
    if (run_betting_round(input, output, action_start_seat(Street::flop))) {
        output << "Hero folded. Hand over.\n";
        return;
    }

    if (active_opponent_count() <= 0U) {
        output << "Hand ended.\n";
        return;
    }

    output << "=== TURN ===\n";
    reveal_next_board(input, output);
    reset_street(Street::turn);
    output << "=== TURN BETTING ===\n";
    output << "Pot: " << pot_ << " | To call: 0\n";
    if (run_betting_round(input, output, action_start_seat(Street::turn))) {
        output << "Hero folded. Hand over.\n";
        return;
    }

    if (active_opponent_count() <= 0U) {
        output << "Hand ended.\n";
        return;
    }

    output << "=== RIVER ===\n";
    reveal_next_board(input, output);
    reset_street(Street::river);
    output << "=== RIVER BETTING ===\n";
    output << "Pot: " << pot_ << " | To call: 0\n";
    if (run_betting_round(input, output, action_start_seat(Street::river))) {
        output << "Hero folded. Hand over.\n";
        return;
    }

    output << "Hand reached showdown.\n";
}

void run_game_session(std::istream& input, std::ostream& output) {
    auto read_line = [&input, &output](const char* prompt) -> std::string {
        output << prompt;
        std::string line;
        if (!std::getline(input, line)) {
            throw std::runtime_error("Failed to read session setup input");
        }
        return line;
    };

    while (true) {
        const std::string hero_cards_text = read_line("Hero hole cards (e.g. As Kd): ");
        std::istringstream hero_stream(hero_cards_text);
        std::string hero_first_text;
        std::string hero_second_text;
        if (!(hero_stream >> hero_first_text >> hero_second_text)) {
            throw std::invalid_argument("Hero hole cards must contain two cards");
        }

        const std::string opponents_text = read_line("Number of opponents: ");
        const std::string small_blind_text = read_line("Small blind: ");
        const std::string big_blind_text = read_line("Big blind: ");
        const std::string position_text = read_line("Hero position (1-6, where 1 is first to act preflop): ");
        const std::string starting_stack_text = read_line("Starting stack: ");

        GameSessionConfig config{};
        const std::optional<Card> hero_first = Card::from_string(hero_first_text);
        const std::optional<Card> hero_second = Card::from_string(hero_second_text);
        if (!hero_first || !hero_second) {
            throw std::invalid_argument("Invalid hero hole cards");
        }
        config.hero_cards = {*hero_first, *hero_second};
        config.opponents = static_cast<std::size_t>(std::stoull(opponents_text));
        config.small_blind = std::stod(small_blind_text);
        config.big_blind = std::stod(big_blind_text);
        config.hero_position = static_cast<std::size_t>(std::stoull(position_text));
        config.starting_stack = std::stod(starting_stack_text);

        GameSession session{config};
        session.run(input, output);

        output << "Start another hand? (y/n): ";
        std::string answer;
        if (!std::getline(input, answer) || (!answer.empty() && answer[0] != 'y' && answer[0] != 'Y')) {
            break;
        }
    }
}

}  // namespace poker
