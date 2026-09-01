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
    if (upper == "MP") return TablePosition::utg_plus_1;
    if (upper == "CO") return TablePosition::utg_plus_2;
    if (upper == "BTN") return TablePosition::btn;
    if (upper == "SB") return TablePosition::sb;
    if (upper == "BB") return TablePosition::bb;
    throw std::invalid_argument("Invalid position: " + text);
}

GameSession::GameSession(GameSessionConfig config) : config_(config) {
    reset_hand();
}

std::size_t GameSession::total_players() const { return config_.opponents + 1U; }

TablePosition GameSession::hero_position() const { return hero_position_; }

TablePosition GameSession::button_position() const { return TablePosition::btn; }

TablePosition GameSession::small_blind_position() const { return TablePosition::sb; }

TablePosition GameSession::big_blind_position() const { return TablePosition::bb; }

// Helper: find index in player_positions_
static std::size_t index_of_position(const std::vector<TablePosition>& positions, TablePosition pos) {
    for (std::size_t i = 0; i < positions.size(); ++i) {
        if (positions[i] == pos) return i;
    }
    detail::throw_invalid("Position not found");
    return 0U;
}

void GameSession::reset_hand() {
    const std::size_t players = total_players();
    if (config_.hero_position < 1U || config_.hero_position > players) {
        throw std::invalid_argument("Hero position must be between 1 and " + std::to_string(players));
    }

    // Build canonical player positions clockwise starting from BTN
    player_positions_.clear();
    if (players == 2U) {
        player_positions_.push_back(TablePosition::btn);
        player_positions_.push_back(TablePosition::bb);
    } else {
        const std::vector<TablePosition> base = {TablePosition::btn,
                                                 TablePosition::sb,
                                                 TablePosition::bb,
                                                 TablePosition::utg,
                                                 TablePosition::utg_plus_1,
                                                 TablePosition::utg_plus_2};
        for (std::size_t i = 0; i < players; ++i) player_positions_.push_back(base[i]);
    }

    // Prepare player state aligned to player_positions_
    players_.assign(players, SessionPlayerState{});
    acted_.assign(players, false);
    for (std::size_t i = 0; i < players; ++i) {
        players_[i].stack = config_.starting_stack;
        players_[i].active = false;
    }

    // Initialize action_order_ (uses numeric indices aligned with player_positions_)
    action_order_.player_count = players;
    action_order_.button_seat = 0; // BTN is index 0 in player_positions_
    street_ = Street::preflop;
    action_order_.street = street_;

    // Determine first-to-act preflop using existing ActionOrderState logic (numeric indices)
    {
        ActionOrderState tmp = action_order_;
        tmp.seats.assign(players, PlayerStatus::folded);
        // No active seats yet; we'll set active below after mapping hero and opponents
        std::optional<std::size_t> first_idx = tmp.first_to_act();
        // first_idx may be nullopt if hand over; we'll compute preflop order after active set
    }

    // Map relative preflop position -> absolute TablePosition
    // To compute mapping we need the preflop action order; temporarily assume all positions active for ordering
    std::vector<std::size_t> full_order_idx;
    full_order_idx.reserve(players);
    // find start index for preflop: if players==2, start=button (0) else start=(button+3)%players
    std::size_t start_idx = (players == 2U) ? 0U : (0U + 3U) % players;
    for (std::size_t offset = 0; offset < players; ++offset) full_order_idx.push_back((start_idx + offset) % players);

    const std::size_t hero_order_index = (config_.hero_position - 1U) % players;
    const std::size_t hero_idx = full_order_idx[hero_order_index];
    hero_position_ = player_positions_[hero_idx];

    // Mark hero and assign opponents clockwise from hero_idx
    players_[hero_idx].hero = true;
    players_[hero_idx].active = true;
    std::size_t assigned = 0U;
    for (std::size_t offset = 1U; assigned < config_.opponents; ++offset) {
        const std::size_t idx = (hero_idx + offset) % players;
        if (!players_[idx].hero && !players_[idx].active) {
            players_[idx].active = true;
            ++assigned;
        }
    }

    // Initialize action_order_.seats from players_.active
    action_order_.seats.assign(players, PlayerStatus::folded);
    for (std::size_t i = 0; i < players; ++i) {
        action_order_.seats[i] = players_[i].active ? PlayerStatus::active : PlayerStatus::folded;
    }

    // acted_ initial state
    for (std::size_t i = 0; i < players; ++i) acted_[i] = !players_[i].active;

    board_.clear();
    pot_ = 0.0;
    current_bet_ = 0.0;
    history_.clear();
    position_order_ = player_positions_; // copy for clarity
}

void GameSession::reset_street(Street street) {
    street_ = street;
    current_bet_ = 0.0;
    action_order_.reset(street);
    for (std::size_t i = 0U; i < players_.size(); ++i) {
        if (players_[i].active) players_[i].contribution = 0.0;
        acted_[i] = !players_[i].active;
    }
}

std::string GameSession::position_label(TablePosition pos) const {
    const std::size_t idx = index_of_position(player_positions_, pos);
    if (player_positions_[idx] == hero_position_) return "Hero";
    return std::string("Player ") + std::to_string(idx + 1U);
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

void GameSession::apply_posted_amount(TablePosition pos, double amount) {
    const std::size_t idx = index_of_position(player_positions_, pos);
    players_[idx].stack -= amount;
    players_[idx].contribution += amount;
    pot_ += amount;
    current_bet_ = std::max(current_bet_, players_[idx].contribution);
}

void GameSession::apply_fold(TablePosition pos) {
    const std::size_t idx = index_of_position(player_positions_, pos);
    players_[idx].active = false;
}

void GameSession::mark_all_active_to_act(TablePosition start_pos) {
    std::fill(acted_.begin(), acted_.end(), true);
    std::size_t start = index_of_position(player_positions_, start_pos);
    std::size_t seat = start;
    for (std::size_t count = 0U; count < players_.size(); ++count) {
        if (players_[seat].active) acted_[seat] = false;
        const std::optional<std::size_t> next = action_order_.next_to_act(seat);
        seat = next.value_or(seat + 1U >= players_.size() ? 0U : seat + 1U);
    }
}

void GameSession::apply_check_or_call(TablePosition pos) {
    const std::size_t idx = index_of_position(player_positions_, pos);
    const double to_call = std::max(0.0, current_bet_ - players_[idx].contribution);
    apply_posted_amount(pos, std::min(to_call, players_[idx].stack));
}

void GameSession::apply_raise_to(TablePosition pos, double amount) {
    const std::size_t idx = index_of_position(player_positions_, pos);
    if (amount > players_[idx].stack + players_[idx].contribution) {
        throw std::invalid_argument("Raise exceeds remaining stack");
    }
    const double needed = amount - players_[idx].contribution;
    if (needed < 0.0) {
        throw std::invalid_argument("Raise amount must not be below current contribution");
    }
    apply_posted_amount(pos, needed);
    current_bet_ = std::max(current_bet_, players_[idx].contribution);
}

BettingState GameSession::build_betting_state_for_position(TablePosition pos) const {
    const std::size_t idx = index_of_position(player_positions_, pos);
    BettingState betting{};
    betting.current_pot = pot_;
    betting.call_amount = std::max(0.0, current_bet_ - players_[idx].contribution);
    betting.hero_stack = players_[idx].stack;
    betting.check_allowed = betting.call_amount == 0.0;
    betting.minimum_raise_amount = betting.call_amount == 0.0 ? 1.0 : std::max(1.0, betting.call_amount);
    return betting;
}


std::vector<TablePosition> GameSession::active_positions() const {
    std::vector<TablePosition> positions;
    for (std::size_t i = 0U; i < players_.size(); ++i) {
        if (players_[i].active) positions.push_back(player_positions_[i]);
    }
    return positions;
}

std::vector<TablePosition> GameSession::ordered_positions_from(TablePosition pos) const {
    const std::size_t start = index_of_position(player_positions_, pos);
    std::vector<TablePosition> ordered;
    ordered.reserve(total_players());
    for (std::size_t offset = 0U; offset < total_players(); ++offset) ordered.push_back(player_positions_[(start + offset) % total_players()]);
    return ordered;
}

std::size_t GameSession::active_opponent_count() const {
    std::size_t count = 0U;
    const std::size_t hero_idx = index_of_position(player_positions_, hero_position_);
    for (std::size_t i = 0U; i < players_.size(); ++i) {
        if (i != hero_idx && players_[i].active) ++count;
    }
    return count;
}

TablePosition GameSession::next_active_position(TablePosition pos) const {
    const std::size_t idx = index_of_position(player_positions_, pos);
    const std::optional<std::size_t> next = action_order_.next_to_act(idx);
    const std::size_t next_idx = next.value_or(idx);
    return player_positions_[next_idx];
}

TablePosition GameSession::action_start_position(Street street) const {
    ActionOrderState order = action_order_;
    order.street = street;
    const std::optional<std::size_t> start = order.first_to_act();
    const std::size_t idx = start.value_or(static_cast<std::size_t>(action_order_.button_seat));
    return player_positions_[idx];
}

bool GameSession::betting_round_complete(TablePosition start_pos, TablePosition current_pos) const {
    const std::size_t start = index_of_position(player_positions_, start_pos);
    const std::size_t current = index_of_position(player_positions_, current_pos);
    for (std::size_t i = 0U; i < players_.size(); ++i) {
        if (players_[i].active && i != current && players_[i].contribution != current_bet_) return false;
    }
    return current == start || players_[start].contribution == current_bet_;
}

bool GameSession::betting_round_complete() const {
    for (std::size_t i = 0U; i < players_.size(); ++i) {
        if (!players_[i].active) continue;
        if (players_[i].contribution != current_bet_ || !acted_[i]) return false;
    }
    return true;
}

bool GameSession::hand_over() const {
    const std::size_t hero_idx = index_of_position(player_positions_, hero_position_);
    return active_opponent_count() == 0U || players_[hero_idx].active == false || action_order_.hand_over();
}

void GameSession::print_status_line(std::ostream& output) const {
    const BettingState betting = build_betting_state_for_position(hero_position_);
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

void GameSession::prompt_and_apply_action(std::istream& input, std::ostream& output, TablePosition pos) {
    const BettingState betting = build_betting_state_for_position(pos);
    const std::size_t idx = index_of_position(player_positions_, pos);
    if (player_positions_[idx] == hero_position_) {
        HoldemHand hero{};
        hero.hole[0] = config_.hero_cards[0];
        hero.hole[1] = config_.hero_cards[1];
        hero.board_count = static_cast<std::uint8_t>(board_.size());
        for (std::size_t index = 0U; index < board_.size(); ++index) hero.board[index] = board_[index];

        EquityOptions equity_options = config_.equity_options;
        const std::uint64_t theoretical_states = theoretical_exact_states(hero, active_opponent_count());
        if (!detail::exact_equity_allowed(theoretical_states)) {
            equity_options.method = EquityMethod::monte_carlo;
            if (equity_options.simulations == 0U) equity_options.simulations = 1000U;
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
            if (decision.suggested_amount.has_value()) output << " TO " << *decision.suggested_amount;
            output << "\n";
        }
        output << "Hero action [c/f/r <amount>]: ";
    } else {
        output << position_label(pos) << " action [c/f/r <amount>]: ";
    }

    std::string command;
    std::getline(input, command);
    if (command.empty()) throw std::invalid_argument("Action is required");

    std::istringstream stream(command);
    std::string verb;
    stream >> verb;
    if (verb == "f") {
        apply_fold(pos);
        history_.push_back({pos, SessionActionType::fold, 0.0});
        return;
    }

    if (verb == "c") {
        const double to_call = std::max(0.0, current_bet_ - players_[idx].contribution);
        if (to_call == 0.0) {
            history_.push_back({pos, SessionActionType::check, 0.0});
        } else {
            apply_check_or_call(pos);
            history_.push_back({pos, SessionActionType::call, to_call});
        }
        return;
    }

    if (verb == "r") {
        double amount = 0.0;
        if (!(stream >> amount)) throw std::invalid_argument("r requires an amount");
        apply_raise_to(pos, amount);
        for (std::size_t other = 0U; other < acted_.size(); ++other) {
            if (players_[other].active && other != idx) acted_[other] = false;
        }
        history_.push_back({pos, current_bet_ == amount ? SessionActionType::bet : SessionActionType::raise, amount});
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
    const std::size_t hero_idx = index_of_position(player_positions_, hero_position_);
    state.betting.call_amount = std::max(0.0, current_bet_ - players_[hero_idx].contribution);
    state.betting.hero_stack = players_[hero_idx].stack;
    state.betting.check_allowed = state.betting.call_amount == 0.0;
    state.betting.minimum_raise_amount = 1.0;
    state.player_count = active_opponent_count() + 1U;
    for (std::size_t i = 0U; i < players_.size(); ++i) {
        if (i == hero_idx || !players_[i].active) continue;
        state.opponents.emplace_back(RandomOpponent{});
    }
    return state;
}

void GameSession::post_blinds(std::ostream& output) {
    if (total_players() < 2U) return;
    apply_posted_amount(small_blind_position(), config_.small_blind);
    apply_posted_amount(big_blind_position(), config_.big_blind);
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
    const TablePosition start = action_start_position(street_);
    mark_all_active_to_act(start);
    (void)run_betting_round(input, output, start);
}

bool GameSession::run_betting_round(std::istream& input, std::ostream& output, TablePosition start_pos) {
    if (hand_over()) return true;
    std::size_t current = index_of_position(player_positions_, start_pos);
    const std::size_t start_idx = current;
    while (true) {
        if (!players_[current].active) {
            acted_[current] = true;
        } else if (!acted_[current]) {
            prompt_and_apply_action(input, output, player_positions_[current]);
            acted_[current] = true;
            action_order_.seats[current] = players_[current].active ? PlayerStatus::active : PlayerStatus::folded;
            if (hand_over()) return true;
            if (betting_round_complete()) return false;
        }

        const std::optional<std::size_t> next = action_order_.next_to_act(current);
        current = next.value_or(current);
        if (current == start_idx && betting_round_complete()) return false;
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
    const std::size_t hero_idx = index_of_position(player_positions_, hero_position_);
    output << "Pot: " << pot_ << " | To call: " << std::max(0.0, current_bet_ - players_[hero_idx].contribution) << "\n";
    if (run_betting_round(input, output, action_start_position(Street::preflop))) {
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
    if (run_betting_round(input, output, action_start_position(Street::flop))) {
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
    if (run_betting_round(input, output, action_start_position(Street::turn))) {
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
    if (run_betting_round(input, output, action_start_position(Street::river))) {
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
