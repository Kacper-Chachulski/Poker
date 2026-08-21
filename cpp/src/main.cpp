#include <cstddef>
#include <chrono>
#include <array>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/ev.hpp"
#include "poker/hand.hpp"

namespace {

enum class EquityCliMode {
    specific_hand_vs_specific_hand,
    specific_hand_vs_range,
    range_vs_range,
};

enum class DecisionCliMode {
    pot_odds,
    ev,
};

struct CliOptions {
    EquityCliMode mode{EquityCliMode::specific_hand_vs_specific_hand};
    std::array<poker::Card, 2> hero_cards{};
    std::optional<std::string> hero_range_notation{};
    std::optional<poker::HandRange> hero_range{};
    std::optional<std::string> villain_range_notation{};
    std::optional<poker::HandRange> villain_range{};
    std::vector<poker::Card> board{};
    std::size_t opponents{0U};
    bool opponents_set{false};
    poker::EquityOptions equity_options{};
};

struct DecisionOptions {
    DecisionCliMode mode{DecisionCliMode::pot_odds};
    double pot_before_call{0.0};
    double call_amount{0.0};
    std::optional<double> equity{};
};

bool starts_with_option(std::string_view token) {
    return token.rfind("--", 0U) == 0U;
}

bool is_help_token(std::string_view token) {
    return token == "-h" || token == "--help";
}

double parse_double(std::string_view text, const char* label) {
    try {
        const double value = std::stod(std::string(text));
        if (!std::isfinite(value)) {
            throw std::invalid_argument("");
        }
        return value;
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("Invalid ") + label + ": " + std::string(text));
    }
}

poker::Card parse_card(std::string_view text) {
    const std::optional<poker::Card> parsed = poker::Card::from_string(text);
    if (!parsed) {
        throw std::invalid_argument(std::string("Invalid card notation: ") + std::string(text));
    }
    return *parsed;
}

std::uint64_t parse_u64(std::string_view text, const char* label) {
    try {
        return static_cast<std::uint64_t>(std::stoull(std::string(text)));
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("Invalid ") + label + ": " + std::string(text));
    }
}

poker::EquityMethod parse_method(std::string_view text) {
    if (text == "exact") {
        return poker::EquityMethod::exact;
    }
    if (text == "montecarlo") {
        return poker::EquityMethod::monte_carlo;
    }
    throw std::invalid_argument(std::string("Invalid method: ") + std::string(text));
}

std::string format_number(std::uint64_t value) {
    std::string text = std::to_string(value);
    for (std::ptrdiff_t index = static_cast<std::ptrdiff_t>(text.size()) - 3; index > 0; index -= 3) {
        text.insert(static_cast<std::size_t>(index), ",");
    }
    return text;
}

std::string trim_trailing_zeros(std::string text) {
    if (const std::size_t decimal = text.find('.'); decimal != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

std::string format_amount(double value) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream << std::setprecision(2) << value;
    return trim_trailing_zeros(stream.str());
}

std::string format_equity_percent(double equity) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream << std::setprecision(2) << equity * 100.0 << "%";
    return stream.str();
}

std::string format_pot_odds_ratio(const poker::PotOddsResult& result) {
    if (!result.pot_odds_ratio.has_value()) {
        return "N/A";
    }

    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream << std::setprecision(2) << *result.pot_odds_ratio;
    return trim_trailing_zeros(stream.str()) + ":1";
}

std::string format_signed_money(double value) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream << std::setprecision(2) << std::showpos << value;
    return stream.str();
}

std::string format_cards(const std::array<poker::Card, 2>& cards) {
    return cards[0].to_string() + std::string(" ") + cards[1].to_string();
}

std::string format_board(const std::vector<poker::Card>& board) {
    if (board.empty()) {
        return "(none)";
    }

    std::string text;
    for (std::size_t index = 0U; index < board.size(); ++index) {
        if (index > 0U) {
            text += ' ';
        }
        text += board[index].to_string();
    }
    return text;
}

std::string format_range_label(const std::string& notation, const poker::HandRange& range) {
    return notation + " (" + format_number(range.size()) + " combos)";
}

void print_equity_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  poker equity <hero card 1> <hero card 2> [--board cards...] [--method exact|montecarlo] [--simulations N] [--seed N]\n"
        << "  poker equity <hero card 1> <hero card 2> --villain-range \"RANGE\" [--board cards...] [--method exact|montecarlo] [--simulations N] [--seed N]\n"
        << "  poker equity --hero-range \"RANGE\" --villain-range \"RANGE\" [--board cards...] [--method exact|montecarlo] [--simulations N] [--seed N]\n\n"
        << "Examples:\n"
        << "  poker equity As Ks --villain-range \"QQ+, AJs+, KQs\" --board Qh 7c 2s --method exact\n"
        << "  poker equity --hero-range \"QQ+, AKs\" --villain-range \"JJ+, AQs+\" --board Qh 7c 2s --method montecarlo --simulations 1000000 --seed 12345\n";
}

void print_decision_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  poker pot-odds --pot N --call N\n"
        << "  poker ev --pot N --call N --equity N\n\n"
        << "Examples:\n"
        << "  poker pot-odds --pot 100 --call 50\n"
        << "  poker ev --pot 100 --call 50 --equity 0.40\n";
}

void parse_board_values(CliOptions& options, std::size_t& index, int argc, char* argv[]) {
    ++index;
    while (index < static_cast<std::size_t>(argc) && !starts_with_option(argv[index])) {
        if (options.board.size() >= 5U) {
            throw std::invalid_argument("Board cannot contain more than 5 cards");
        }
        options.board.push_back(parse_card(argv[index]));
        ++index;
    }
}

CliOptions parse_equity_arguments(int argc, char* argv[]) {
    if (argc < 2) {
        throw std::invalid_argument("Missing command");
    }

    if (std::string_view(argv[1]) != "equity") {
        throw std::invalid_argument("Only the 'equity' command is supported");
    }

    CliOptions options{};
    std::vector<poker::Card> positional_cards{};

    std::size_t index = 2U;
    while (index < static_cast<std::size_t>(argc)) {
        const std::string_view token = argv[index];

        if (token == "--board") {
            parse_board_values(options, index, argc, argv);
            continue;
        }

        if (token == "--hero-range") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--hero-range requires a value");
            }
            if (options.hero_range_notation) {
                throw std::invalid_argument("--hero-range may only be specified once");
            }
            options.hero_range_notation = std::string(argv[index + 1U]);
            options.hero_range = poker::HandRange::parse(*options.hero_range_notation);
            index += 2U;
            continue;
        }

        if (token == "--villain-range") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--villain-range requires a value");
            }
            if (options.villain_range_notation) {
                throw std::invalid_argument("--villain-range may only be specified once");
            }
            options.villain_range_notation = std::string(argv[index + 1U]);
            options.villain_range = poker::HandRange::parse(*options.villain_range_notation);
            index += 2U;
            continue;
        }

        if (token == "--opponents") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--opponents requires a value");
            }
            options.opponents = static_cast<std::size_t>(parse_u64(argv[index + 1U], "opponents"));
            options.opponents_set = true;
            index += 2U;
            continue;
        }

        if (token == "--simulations") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--simulations requires a value");
            }
            options.equity_options.simulations = parse_u64(argv[index + 1U], "simulations");
            index += 2U;
            continue;
        }

        if (token == "--seed") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--seed requires a value");
            }
            options.equity_options.seed = parse_u64(argv[index + 1U], "seed");
            index += 2U;
            continue;
        }

        if (token == "--method") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--method requires a value");
            }
            options.equity_options.method = parse_method(argv[index + 1U]);
            index += 2U;
            continue;
        }

        if (starts_with_option(token)) {
            throw std::invalid_argument(std::string("Unknown option: ") + std::string(token));
        }

        if (options.hero_range) {
            throw std::invalid_argument("Positional hero cards cannot be combined with range arguments");
        }

        if (positional_cards.size() >= 2U) {
            throw std::invalid_argument("Too many positional hero cards");
        }

        positional_cards.push_back(parse_card(token));
        ++index;
    }

    if (options.equity_options.method == poker::EquityMethod::monte_carlo &&
        options.equity_options.simulations == 0U) {
        throw std::invalid_argument("--simulations must be greater than 0 for Monte Carlo mode");
    }

    if (options.hero_range && !positional_cards.empty()) {
        throw std::invalid_argument("Positional hero cards cannot be combined with range arguments");
    }

    if (options.hero_range && options.opponents_set) {
        throw std::invalid_argument("--opponents is not supported with range-based equity");
    }
    if (options.villain_range && options.opponents_set) {
        throw std::invalid_argument("--opponents is not supported with range-based equity");
    }

    if (options.hero_range) {
        if (!options.villain_range) {
            throw std::invalid_argument("--hero-range requires --villain-range");
        }
        options.mode = EquityCliMode::range_vs_range;
        return options;
    }

    if (options.villain_range) {
        if (positional_cards.size() != 2U) {
            throw std::invalid_argument("Specific-hand vs range requires exactly two hero cards");
        }
        options.mode = EquityCliMode::specific_hand_vs_range;
        options.hero_cards[0] = positional_cards[0];
        options.hero_cards[1] = positional_cards[1];
        return options;
    }

    if (positional_cards.size() != 2U) {
        throw std::invalid_argument("Specific-hand equity requires exactly two hero cards");
    }

    options.mode = EquityCliMode::specific_hand_vs_specific_hand;
    options.hero_cards[0] = positional_cards[0];
    options.hero_cards[1] = positional_cards[1];
    return options;
}

DecisionOptions parse_decision_arguments(int argc, char* argv[]) {
    if (argc < 2) {
        throw std::invalid_argument("Missing command");
    }

    const std::string_view command = argv[1];
    if (command != "pot-odds" && command != "ev") {
        throw std::invalid_argument("Only the 'pot-odds' and 'ev' commands are supported");
    }

    DecisionOptions options{};
    options.mode = command == "ev" ? DecisionCliMode::ev : DecisionCliMode::pot_odds;

    bool pot_set = false;
    bool call_set = false;
    bool equity_set = false;

    std::size_t index = 2U;
    while (index < static_cast<std::size_t>(argc)) {
        const std::string_view token = argv[index];

        if (token == "--pot") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--pot requires a value");
            }
            options.pot_before_call = parse_double(argv[index + 1U], "pot");
            pot_set = true;
            index += 2U;
            continue;
        }

        if (token == "--call") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--call requires a value");
            }
            options.call_amount = parse_double(argv[index + 1U], "call");
            call_set = true;
            index += 2U;
            continue;
        }

        if (token == "--equity") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--equity requires a value");
            }
            options.equity = parse_double(argv[index + 1U], "equity");
            equity_set = true;
            index += 2U;
            continue;
        }

        if (starts_with_option(token)) {
            throw std::invalid_argument(std::string("Unknown option: ") + std::string(token));
        }

        throw std::invalid_argument(std::string("Unexpected argument: ") + std::string(token));
    }

    if (!pot_set) {
        throw std::invalid_argument("--pot is required");
    }
    if (!call_set) {
        throw std::invalid_argument("--call is required");
    }
    if (options.mode == DecisionCliMode::ev && !equity_set) {
        throw std::invalid_argument("--equity is required for the ev command");
    }

    return options;
}

poker::HoldemHand make_holdem_hand(const std::array<poker::Card, 2>& hero_cards, const std::vector<poker::Card>& board) {
    poker::HoldemHand hand{};
    hand.hole[0] = hero_cards[0];
    hand.hole[1] = hero_cards[1];
    hand.board_count = static_cast<std::uint8_t>(board.size());
    for (std::size_t index = 0U; index < board.size(); ++index) {
        hand.board[index] = board[index];
    }
    return hand;
}

void print_specific_hand_exact_result(const CliOptions& options, const poker::EquityResult& result, double seconds) {
    const double states_per_second = static_cast<double>(result.evaluated_states) / seconds;

    std::cout << "Hero: " << format_cards(options.hero_cards) << '\n';
    std::cout << "Board: " << format_board(options.board) << '\n';
    std::cout << "Opponents: " << options.opponents << '\n';
    std::cout << "Method: Exact\n";
    std::cout << "States: " << format_number(result.evaluated_states) << "\n\n";

    std::cout.setf(std::ios::fixed);
    std::cout << std::setprecision(4);
    std::cout << "Win:    " << result.win_probability * 100.0 << "%\n";
    std::cout << "Tie:    " << result.tie_probability * 100.0 << "%\n";
    std::cout << "Loss:   " << result.loss_probability * 100.0 << "%\n";
    std::cout << "Equity: " << result.equity * 100.0 << "%\n";
    std::cout << "Time: " << std::setprecision(4) << seconds << " seconds\n";
    std::cout << "Speed: " << format_number(static_cast<std::uint64_t>(states_per_second)) << " states/sec\n";
}

void print_specific_hand_monte_carlo_result(const CliOptions& options,
                                            const poker::EquityResult& result,
                                            std::uint64_t seed,
                                            double seconds) {
    const double sims_per_second = static_cast<double>(result.simulations) / seconds;

    std::cout << "Hero: " << format_cards(options.hero_cards) << '\n';
    std::cout << "Board: " << format_board(options.board) << '\n';
    std::cout << "Opponents: " << options.opponents << '\n';
    std::cout << "Method: Monte Carlo\n";
    std::cout << "Simulations: " << format_number(result.simulations) << '\n';
    std::cout << "Seed: " << seed << "\n\n";

    std::cout.setf(std::ios::fixed);
    std::cout << std::setprecision(2);
    std::cout << "Win:   " << result.win_probability * 100.0 << "%\n";
    std::cout << "Tie:   " << result.tie_probability * 100.0 << "%\n";
    std::cout << "Loss:  " << result.loss_probability * 100.0 << "%\n";
    std::cout << "Equity:" << ' ' << result.equity * 100.0 << "%\n";
    std::cout << "Speed: " << format_number(static_cast<std::uint64_t>(sims_per_second)) << " simulations/sec\n";
    std::cout << "Elapsed: " << std::setprecision(6) << seconds << " seconds\n";
}

void print_range_exact_result(const CliOptions& options, const poker::EquityResult& result, double seconds) {
    const double states_per_second = static_cast<double>(result.evaluated_states) / seconds;

    std::cout << "Hero range: " << format_range_label(*options.hero_range_notation, *options.hero_range) << '\n';
    std::cout << "Villain range: " << format_range_label(*options.villain_range_notation, *options.villain_range) << '\n';
    std::cout << "Board: " << format_board(options.board) << '\n';
    std::cout << "Method: Exact\n";
    std::cout << "States: " << format_number(result.evaluated_states) << "\n\n";

    std::cout.setf(std::ios::fixed);
    std::cout << std::setprecision(4);
    std::cout << "Win:    " << result.win_probability * 100.0 << "%\n";
    std::cout << "Tie:    " << result.tie_probability * 100.0 << "%\n";
    std::cout << "Loss:   " << result.loss_probability * 100.0 << "%\n";
    std::cout << "Equity: " << result.equity * 100.0 << "%\n";
    std::cout << "Time: " << std::setprecision(4) << seconds << " seconds\n";
    std::cout << "Speed: " << format_number(static_cast<std::uint64_t>(states_per_second)) << " states/sec\n";
}

void print_range_monte_carlo_result(const CliOptions& options,
                                    const poker::EquityResult& result,
                                    std::uint64_t seed,
                                    double seconds) {
    const double sims_per_second = static_cast<double>(result.simulations) / seconds;

    std::cout << "Hero range: " << format_range_label(*options.hero_range_notation, *options.hero_range) << '\n';
    std::cout << "Villain range: " << format_range_label(*options.villain_range_notation, *options.villain_range) << '\n';
    std::cout << "Board: " << format_board(options.board) << '\n';
    std::cout << "Method: Monte Carlo\n";
    std::cout << "Simulations: " << format_number(result.simulations) << '\n';
    std::cout << "Seed: " << seed << "\n\n";

    std::cout.setf(std::ios::fixed);
    std::cout << std::setprecision(2);
    std::cout << "Win:   " << result.win_probability * 100.0 << "%\n";
    std::cout << "Tie:   " << result.tie_probability * 100.0 << "%\n";
    std::cout << "Loss:  " << result.loss_probability * 100.0 << "%\n";
    std::cout << "Equity:" << ' ' << result.equity * 100.0 << "%\n";
    std::cout << "Speed: " << format_number(static_cast<std::uint64_t>(sims_per_second)) << " simulations/sec\n";
    std::cout << "Elapsed: " << std::setprecision(6) << seconds << " seconds\n";
}

void print_pot_odds_result(const poker::PotOddsResult& result) {
    std::cout << "Pot before call: " << format_amount(result.pot_before_call) << '\n';
    std::cout << "Call amount:      " << format_amount(result.call_amount) << '\n';
    std::cout << "Final pot:       " << format_amount(result.final_pot) << '\n';
    std::cout << "Required equity: " << format_equity_percent(result.required_equity) << '\n';
    std::cout << "Pot odds:        " << format_pot_odds_ratio(result) << '\n';
}

void print_ev_result(const poker::PotOddsResult& result, double equity) {
    const double call_ev = poker::calculate_call_ev(equity, result.pot_before_call, result.call_amount);
    const double fold_ev = poker::calculate_fold_ev(result.pot_before_call, result.call_amount);

    std::cout << "Pot before call: " << format_amount(result.pot_before_call) << '\n';
    std::cout << "Call amount:      " << format_amount(result.call_amount) << '\n';
    std::cout << "Final pot:       " << format_amount(result.final_pot) << '\n';
    std::cout << "Required equity: " << format_equity_percent(result.required_equity) << '\n';
    std::cout << "Pot odds:        " << format_pot_odds_ratio(result) << '\n';
    std::cout << "Equity:          " << format_equity_percent(equity) << '\n';
    std::cout << "Call EV:         " << format_signed_money(call_ev) << '\n';
    std::cout << "Fold EV:         " << format_signed_money(fold_ev) << '\n';
    std::cout << "Decision:        " << (call_ev > 0.0 ? "CALL" : (call_ev < 0.0 ? "FOLD" : "INDIFFERENT")) << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc < 2 || is_help_token(argv[1])) {
            print_equity_usage(std::cout);
            print_decision_usage(std::cout);
            return 0;
        }

        const std::string_view command = argv[1];
        if ((command == "equity" || command == "pot-odds" || command == "ev") && argc >= 3 && is_help_token(argv[2])) {
            if (command == "equity") {
                print_equity_usage(std::cout);
            } else {
                print_decision_usage(std::cout);
            }
            return 0;
        }

        if (command == "pot-odds" || command == "ev") {
            const DecisionOptions options = parse_decision_arguments(argc, argv);
            const poker::PotOddsResult result = poker::calculate_pot_odds(options.pot_before_call, options.call_amount);
            if (options.mode == DecisionCliMode::pot_odds) {
                print_pot_odds_result(result);
            } else {
                print_ev_result(result, *options.equity);
            }
            return 0;
        }

        const CliOptions options = parse_equity_arguments(argc, argv);

        const auto start = std::chrono::steady_clock::now();
        if (options.mode == EquityCliMode::specific_hand_vs_specific_hand) {
            const poker::HoldemHand hand = make_holdem_hand(options.hero_cards, options.board);
            if (options.equity_options.method == poker::EquityMethod::exact) {
                const poker::EquityResult result = poker::calculate_equity(hand, options.opponents, options.equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                print_specific_hand_exact_result(options, result, elapsed.count());
            } else {
                const std::uint64_t seed = options.equity_options.seed.value_or(0U);
                const poker::EquityResult result = poker::calculate_equity(hand, options.opponents, options.equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                print_specific_hand_monte_carlo_result(options, result, seed, elapsed.count());
            }
        } else if (options.mode == EquityCliMode::specific_hand_vs_range) {
            const poker::HoldemHand hand = make_holdem_hand(options.hero_cards, options.board);
            if (options.equity_options.method == poker::EquityMethod::exact) {
                const poker::EquityResult result = poker::calculate_equity(hand, *options.villain_range, options.equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                std::cout << "Hero: " << format_cards(options.hero_cards) << '\n';
                std::cout << "Villain range: " << format_range_label(*options.villain_range_notation, *options.villain_range) << '\n';
                std::cout << "Board: " << format_board(options.board) << '\n';
                std::cout << "Method: Exact\n";
                std::cout << "States: " << format_number(result.evaluated_states) << "\n\n";
                std::cout.setf(std::ios::fixed);
                std::cout << std::setprecision(4);
                std::cout << "Win:    " << result.win_probability * 100.0 << "%\n";
                std::cout << "Tie:    " << result.tie_probability * 100.0 << "%\n";
                std::cout << "Loss:   " << result.loss_probability * 100.0 << "%\n";
                std::cout << "Equity: " << result.equity * 100.0 << "%\n";
                std::cout << "Time: " << std::setprecision(4) << elapsed.count() << " seconds\n";
                const double states_per_second = static_cast<double>(result.evaluated_states) / elapsed.count();
                std::cout << "Speed: " << format_number(static_cast<std::uint64_t>(states_per_second)) << " states/sec\n";
            } else {
                const std::uint64_t seed = options.equity_options.seed.value_or(0U);
                const poker::EquityResult result = poker::calculate_equity(hand, *options.villain_range, options.equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                std::cout << "Hero: " << format_cards(options.hero_cards) << '\n';
                std::cout << "Villain range: " << format_range_label(*options.villain_range_notation, *options.villain_range) << '\n';
                std::cout << "Board: " << format_board(options.board) << '\n';
                std::cout << "Method: Monte Carlo\n";
                std::cout << "Simulations: " << format_number(result.simulations) << '\n';
                std::cout << "Seed: " << seed << "\n\n";
                std::cout.setf(std::ios::fixed);
                std::cout << std::setprecision(2);
                std::cout << "Win:   " << result.win_probability * 100.0 << "%\n";
                std::cout << "Tie:   " << result.tie_probability * 100.0 << "%\n";
                std::cout << "Loss:  " << result.loss_probability * 100.0 << "%\n";
                std::cout << "Equity:" << ' ' << result.equity * 100.0 << "%\n";
                std::cout << "Speed: " << format_number(static_cast<std::uint64_t>(static_cast<double>(result.simulations) / elapsed.count())) << " simulations/sec\n";
                std::cout << "Elapsed: " << std::setprecision(6) << elapsed.count() << " seconds\n";
            }
        } else {
            if (options.equity_options.method == poker::EquityMethod::exact) {
                const poker::EquityResult result = poker::calculate_equity(*options.hero_range, *options.villain_range, options.board, options.equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                print_range_exact_result(options, result, elapsed.count());
            } else {
                const std::uint64_t seed = options.equity_options.seed.value_or(0U);
                const poker::EquityResult result = poker::calculate_equity(*options.hero_range, *options.villain_range, options.board, options.equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                print_range_monte_carlo_result(options, result, seed, elapsed.count());
            }
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
