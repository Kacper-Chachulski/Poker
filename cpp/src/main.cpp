#include <cstddef>
#include <chrono>
#include <array>
#include <algorithm>
#include <cctype>
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
#include "poker/decision.hpp"
#include "poker/game_session.hpp"
#include "poker/ev.hpp"
#include "poker/hand.hpp"
#include "equity_common.hpp"

namespace {

enum class EquityCliMode {
    specific_hand_vs_specific_hand,
    specific_hand_vs_range,
    range_vs_range,
};

enum class DecisionCliMode {
    pot_odds,
    ev,
    decision,
    analyze,
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
    std::optional<poker::EquityMethod> requested_method{};
    bool simulations_set{false};
    poker::EquityOptions equity_options{};
};

struct DecisionOptions {
    DecisionCliMode mode{DecisionCliMode::pot_odds};
    double pot_before_call{0.0};
    double call_amount{0.0};
    std::optional<double> equity{};
};

struct DecisionCommandOptions {
    std::array<poker::Card, 2> hero_cards{};
    std::vector<poker::Card> board{};
    std::size_t opponents{0U};
    bool opponents_set{false};
    std::optional<double> hero_stack{};
    std::optional<poker::EquityMethod> requested_method{};
    bool simulations_set{false};
    double pot_before_call{0.0};
    bool pot_set{false};
    double call_amount{0.0};
    bool call_set{false};
    poker::EquityOptions equity_options{};
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

std::string format_method_name(poker::EquityMethod method) {
    return method == poker::EquityMethod::exact ? "Exact" : "Monte Carlo";
}

std::string format_betting_action(poker::BettingAction action);

std::string format_recommendation(poker::BettingAction action) {
    std::string text = format_betting_action(action);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return text;
}

std::string format_limit_reason(std::uint64_t theoretical_states) {
    return "Exact enumeration would require " + format_number(theoretical_states) +
           " states (> 10,000,000 limit)";
}

struct MethodSelection {
    poker::EquityMethod method{poker::EquityMethod::exact};
    std::optional<std::string> reason{};
};

poker::HoldemHand make_holdem_hand(const std::array<poker::Card, 2>& hero_cards, const std::vector<poker::Card>& board);

MethodSelection select_equity_method(std::optional<poker::EquityMethod> requested_method,
                                     std::uint64_t theoretical_states,
                                     bool simulations_set,
                                     poker::EquityOptions& options) {
    if (requested_method.has_value()) {
        options.method = *requested_method;
        if (*requested_method == poker::EquityMethod::exact && !poker::detail::exact_equity_allowed(theoretical_states)) {
            throw std::invalid_argument(poker::detail::exact_equity_limit_message(theoretical_states));
        }

        if (*requested_method == poker::EquityMethod::monte_carlo && !simulations_set && options.simulations == 0U) {
            throw std::invalid_argument("--simulations must be greater than 0 for Monte Carlo mode");
        }

        return {*requested_method, std::nullopt};
    }

    if (poker::detail::exact_equity_allowed(theoretical_states)) {
        options.method = poker::EquityMethod::exact;
        return {poker::EquityMethod::exact, std::nullopt};
    }

    options.method = poker::EquityMethod::monte_carlo;
    if (!simulations_set || options.simulations == 0U) {
        options.simulations = 100000U;
    }

    return {poker::EquityMethod::monte_carlo, format_limit_reason(theoretical_states)};
}

std::uint64_t theoretical_exact_states_for_options(const CliOptions& options) {
    if (options.mode == EquityCliMode::specific_hand_vs_specific_hand) {
        const poker::HoldemHand hand = make_holdem_hand(options.hero_cards, options.board);
        return poker::theoretical_exact_states(hand, options.opponents);
    }

    if (options.mode == EquityCliMode::specific_hand_vs_range) {
        const poker::HoldemHand hand = make_holdem_hand(options.hero_cards, options.board);
        return poker::theoretical_exact_states(hand, *options.villain_range);
    }

    return poker::theoretical_exact_states(*options.hero_range, *options.villain_range, options.board);
}

std::uint64_t theoretical_exact_states_for_options(const DecisionCommandOptions& options) {
    const poker::HoldemHand hand = make_holdem_hand(options.hero_cards, options.board);
    return poker::theoretical_exact_states(hand, options.opponents);
}

void print_equity_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  poker equity <hero card 1> <hero card 2> [--board cards...] [--method exact|montecarlo] [--simulations N] [--seed N]\n"
        << "  poker equity <hero card 1> <hero card 2> --villain-range \"RANGE\" [--board cards...] [--method exact|montecarlo] [--simulations N] [--seed N]\n"
        << "  poker equity --hero-range \"RANGE\" --villain-range \"RANGE\" [--board cards...] [--method exact|montecarlo] [--simulations N] [--seed N]\n\n"
        << "  If --method is omitted, exact is used up to 10,000,000 theoretical states and Monte Carlo is used above that limit.\n\n"
        << "Examples:\n"
        << "  poker equity As Ks --villain-range \"QQ+, AJs+, KQs\" --board Qh 7c 2s --method exact\n"
        << "  poker equity --hero-range \"QQ+, AKs\" --villain-range \"JJ+, AQs+\" --board Qh 7c 2s --method montecarlo --simulations 1000000 --seed 12345\n";
}

void print_decision_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  poker pot-odds --pot N --call N\n"
        << "  poker ev --pot N --call N --equity N\n"
        << "  poker decision <hero card 1> <hero card 2> --opponents N --pot N --call N [--board cards...] [--method exact|montecarlo] [--simulations N] [--seed N]\n\n"
        << "  If --method is omitted, exact is used up to 10,000,000 theoretical states and Monte Carlo is used above that limit.\n\n"
        << "Examples:\n"
        << "  poker pot-odds --pot 100 --call 50\n"
        << "  poker ev --pot 100 --call 50 --equity 0.40\n"
        << "  poker decision Ah Ts --board 4d 4h 7h Ks --opponents 1 --pot 5 --call 3\n";
}

void print_analyze_usage(std::ostream& out) {
    out << "Usage:\n"
        << "  poker analyze <hero card 1> <hero card 2> --opponents N --pot N --call N [--board cards...] [--method exact|montecarlo] [--simulations N] [--seed N]\n\n"
        << "Examples:\n"
        << "  poker analyze Ah Ts --board 4d 4h 7h Ks --opponents 1 --pot 5 --call 3\n"
        << "  poker analyze As Ks --opponents 5 --pot 100 --call 50 --method montecarlo --simulations 100000 --seed 12345\n";
}

std::string format_betting_action(poker::BettingAction action) {
    switch (action) {
        case poker::BettingAction::check:
            return "Check";
        case poker::BettingAction::bet:
            return "Bet";
        case poker::BettingAction::call:
            return "Call";
        case poker::BettingAction::raise:
            return "Raise";
        case poker::BettingAction::fold:
            return "Fold";
        case poker::BettingAction::all_in:
            return "All-in";
    }

    return "Unknown";
}

template <typename OptionsT>
void parse_board_values(OptionsT& options, std::size_t& index, int argc, char* argv[]) {
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
            options.simulations_set = true;
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
            options.requested_method = parse_method(argv[index + 1U]);
            options.equity_options.method = *options.requested_method;
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

DecisionCommandOptions parse_decision_command_arguments(int argc, char* argv[], std::string_view expected_command) {
    if (argc < 2) {
        throw std::invalid_argument("Missing command");
    }

    if (std::string_view(argv[1]) != expected_command) {
        throw std::invalid_argument(std::string("Only the '") + std::string(expected_command) + "' command is supported");
    }

    DecisionCommandOptions options{};
    std::vector<poker::Card> positional_cards{};

    std::size_t index = 2U;
    while (index < static_cast<std::size_t>(argc)) {
        const std::string_view token = argv[index];

        if (token == "--board") {
            parse_board_values(options, index, argc, argv);
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

        if (token == "--pot") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--pot requires a value");
            }
            options.pot_before_call = parse_double(argv[index + 1U], "pot");
            options.pot_set = true;
            index += 2U;
            continue;
        }

        if (token == "--call") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--call requires a value");
            }
            options.call_amount = parse_double(argv[index + 1U], "call");
            options.call_set = true;
            index += 2U;
            continue;
        }

        if (token == "--stack") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--stack requires a value");
            }
            options.hero_stack = parse_double(argv[index + 1U], "stack");
            index += 2U;
            continue;
        }

        if (token == "--simulations") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--simulations requires a value");
            }
            options.equity_options.simulations = parse_u64(argv[index + 1U], "simulations");
            options.simulations_set = true;
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
            options.requested_method = parse_method(argv[index + 1U]);
            options.equity_options.method = *options.requested_method;
            index += 2U;
            continue;
        }

        if (starts_with_option(token)) {
            throw std::invalid_argument(std::string("Unknown option: ") + std::string(token));
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

    if (!options.opponents_set) {
        throw std::invalid_argument("--opponents is required");
    }
    if (!options.pot_set) {
        throw std::invalid_argument("--pot is required");
    }
    if (!options.call_set) {
        throw std::invalid_argument("--call is required");
    }
    if (!options.hero_stack.has_value()) {
        const double base_stack = std::max(options.pot_before_call, options.call_amount);
        options.hero_stack = std::max(1000.0, base_stack + 1000.0);
    }
    if (positional_cards.size() != 2U) {
        throw std::invalid_argument("Decision requires exactly two hero cards");
    }

    options.hero_cards[0] = positional_cards[0];
    options.hero_cards[1] = positional_cards[1];
    return options;
}

DecisionCommandOptions parse_analyze_arguments(int argc, char* argv[]) {
    return parse_decision_command_arguments(argc, argv, "analyze");
}

poker::BettingState make_analysis_betting_state(const DecisionCommandOptions& options) {
    poker::BettingState betting{};
    betting.current_pot = options.pot_before_call;
    betting.call_amount = options.call_amount;
    betting.hero_stack = options.hero_stack.value_or(std::max(1000.0, std::max(options.pot_before_call, options.call_amount) + 1000.0));
    betting.check_allowed = options.call_amount == 0.0;
    if (betting.call_amount == 0.0) {
        betting.minimum_raise_amount = 1.0;
    }
    return betting;
}

void print_analysis_header(const DecisionCommandOptions& options,
                           const poker::EquityResult& equity,
                           const poker::PotOddsResult& pot_odds,
                           const std::optional<std::string>& reason) {
    std::cout << "Hero: " << format_cards(options.hero_cards) << '\n';
    std::cout << "Board: " << format_board(options.board) << '\n';
    std::cout << "Opponents: " << options.opponents << '\n';
    std::cout << "Method: " << format_method_name(options.equity_options.method) << '\n';
    if (reason.has_value()) {
        std::cout << "Reason: " << *reason << '\n';
    }
    std::cout << "Win:    " << format_equity_percent(equity.win_probability) << '\n';
    std::cout << "Tie:    " << format_equity_percent(equity.tie_probability) << '\n';
    std::cout << "Loss:   " << format_equity_percent(equity.loss_probability) << '\n';
    std::cout << "Equity: " << format_equity_percent(equity.equity) << "\n\n";
    std::cout << "Pot before call: " << format_amount(pot_odds.pot_before_call) << '\n';
    std::cout << "Call amount:      " << format_amount(pot_odds.call_amount) << '\n';
    std::cout << "Final pot:       " << format_amount(pot_odds.final_pot) << '\n';
    std::cout << "Required equity: " << format_equity_percent(pot_odds.required_equity) << '\n';
    std::cout << "Pot odds:        " << format_pot_odds_ratio(pot_odds) << "\n\n";
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

poker::BettingState make_decision_betting_state(const DecisionCommandOptions& options) {
    poker::BettingState betting{};
    betting.current_pot = options.pot_before_call;
    betting.call_amount = options.call_amount;
    if (options.call_amount == 0.0) {
        betting.hero_stack = options.hero_stack.value_or(std::max(1000.0, options.pot_before_call + 1000.0));
        betting.check_allowed = true;
        betting.minimum_raise_amount = 1.0;
    } else {
        betting.hero_stack = options.hero_stack.value_or(std::max(1000.0, std::max(options.pot_before_call, options.call_amount) + 1000.0));
        betting.check_allowed = false;
    }
    return betting;
}

void print_specific_hand_exact_result(const CliOptions& options,
                                      const poker::EquityResult& result,
                                      double seconds,
                                      const std::optional<std::string>& reason) {
    const double states_per_second = static_cast<double>(result.evaluated_states) / seconds;

    std::cout << "Hero: " << format_cards(options.hero_cards) << '\n';
    std::cout << "Board: " << format_board(options.board) << '\n';
    std::cout << "Opponents: " << options.opponents << '\n';
    std::cout << "Method: " << format_method_name(options.equity_options.method) << '\n';
    if (reason.has_value()) {
        std::cout << "Reason: " << *reason << '\n';
    }
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
                                            double seconds,
                                            const std::optional<std::string>& reason) {
    const double sims_per_second = static_cast<double>(result.simulations) / seconds;

    std::cout << "Hero: " << format_cards(options.hero_cards) << '\n';
    std::cout << "Board: " << format_board(options.board) << '\n';
    std::cout << "Opponents: " << options.opponents << '\n';
    std::cout << "Method: " << format_method_name(options.equity_options.method) << '\n';
    if (reason.has_value()) {
        std::cout << "Reason: " << *reason << '\n';
    }
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

void print_range_exact_result(const CliOptions& options,
                             const poker::EquityResult& result,
                             double seconds,
                             const std::optional<std::string>& reason) {
    const double states_per_second = static_cast<double>(result.evaluated_states) / seconds;

    std::cout << "Hero range: " << format_range_label(*options.hero_range_notation, *options.hero_range) << '\n';
    std::cout << "Villain range: " << format_range_label(*options.villain_range_notation, *options.villain_range) << '\n';
    std::cout << "Board: " << format_board(options.board) << '\n';
    std::cout << "Method: " << format_method_name(options.equity_options.method) << '\n';
    if (reason.has_value()) {
        std::cout << "Reason: " << *reason << '\n';
    }
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
                                    double seconds,
                                    const std::optional<std::string>& reason) {
    const double sims_per_second = static_cast<double>(result.simulations) / seconds;

    std::cout << "Hero range: " << format_range_label(*options.hero_range_notation, *options.hero_range) << '\n';
    std::cout << "Villain range: " << format_range_label(*options.villain_range_notation, *options.villain_range) << '\n';
    std::cout << "Board: " << format_board(options.board) << '\n';
    std::cout << "Method: " << format_method_name(options.equity_options.method) << '\n';
    if (reason.has_value()) {
        std::cout << "Reason: " << *reason << '\n';
    }
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

void print_decision_result(const DecisionCommandOptions& options,
                           const poker::EquityResult& equity,
                           const poker::PotOddsResult& pot_odds,
                           const poker::DecisionResult& decision,
                           const std::optional<std::string>& reason) {
    std::cout << "Hero: " << format_cards(options.hero_cards) << '\n';
    std::cout << "Board: " << format_board(options.board) << '\n';
    std::cout << "Opponents: " << options.opponents << '\n';
    std::cout << "Method: " << format_method_name(options.equity_options.method) << '\n';
    if (reason.has_value()) {
        std::cout << "Reason: " << *reason << '\n';
    }
    std::cout << "Equity: " << format_equity_percent(equity.equity) << "\n\n";

    std::cout << "Pot before call: " << format_amount(pot_odds.pot_before_call) << '\n';
    std::cout << "Call amount:      " << format_amount(pot_odds.call_amount) << '\n';
    std::cout << "Required equity: " << format_equity_percent(pot_odds.required_equity) << '\n';
    std::cout << "Pot odds:        " << format_pot_odds_ratio(pot_odds) << "\n\n";

    std::cout << "Actions:\n";
    for (const poker::ActionEvaluation& action : decision.actions) {
        std::cout << "  " << format_betting_action(action.action) << ": ";
        if (!action.supported || !action.ev.has_value()) {
            std::cout << "unsupported\n";
        } else {
            std::cout << "EV = " << format_signed_money(*action.ev) << '\n';
        }
    }

    std::cout << '\n';
    if (decision.best_action.has_value()) {
        std::cout << "Recommendation: " << format_recommendation(*decision.best_action) << '\n';
        if (decision.suggested_amount.has_value()) {
            std::cout << "Suggested amount: " << format_amount(*decision.suggested_amount) << '\n';
        }
        if (decision.rationale.has_value()) {
            std::cout << "Reason: " << *decision.rationale << '\n';
        }
    } else {
        std::cout << "Recommendation: UNAVAILABLE\n";
    }
}

void print_analyze_result(const DecisionCommandOptions& options,
                          const poker::EquityResult& equity,
                          const poker::PotOddsResult& pot_odds,
                          const poker::DecisionResult& decision,
                          const std::optional<std::string>& reason) {
    print_analysis_header(options, equity, pot_odds, reason);

    if (options.call_amount == 0.0) {
        if (decision.best_action.has_value()) {
            std::cout << "Recommendation: " << format_recommendation(*decision.best_action) << '\n';
            if (decision.suggested_amount.has_value()) {
                std::cout << "Suggested amount: " << format_amount(*decision.suggested_amount) << '\n';
            }
            if (decision.rationale.has_value()) {
                std::cout << "Reason: " << *decision.rationale << '\n';
            }
        } else {
            std::cout << "Recommendation: CHECK\n";
        }
        return;
    }

    const poker::ActionEvaluation call_action = poker::evaluate_action(poker::BettingState{options.pot_before_call,
                                                                                            options.call_amount,
                                                                                            options.call_amount,
                                                                                            std::nullopt,
                                                                                            false},
                                                                       equity.equity,
                                                                       poker::BettingAction::call);
    const poker::ActionEvaluation fold_action = poker::evaluate_action(poker::BettingState{options.pot_before_call,
                                                                                            options.call_amount,
                                                                                            options.call_amount,
                                                                                            std::nullopt,
                                                                                            false},
                                                                       equity.equity,
                                                                       poker::BettingAction::fold);

    std::cout << "Call EV:         " << (call_action.ev.has_value() ? format_signed_money(*call_action.ev) : std::string("unsupported")) << '\n';
    std::cout << "Fold EV:         " << (fold_action.ev.has_value() ? format_signed_money(*fold_action.ev) : std::string("unsupported")) << '\n';
    if (decision.best_action.has_value()) {
        std::cout << "Recommendation: " << format_recommendation(*decision.best_action) << '\n';
        if (decision.suggested_amount.has_value()) {
            std::cout << "Suggested amount: " << format_amount(*decision.suggested_amount) << '\n';
        }
        if (decision.rationale.has_value()) {
            std::cout << "Reason: " << *decision.rationale << '\n';
        }
    } else {
        std::cout << "Recommendation: UNAVAILABLE\n";
    }
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
        if ((command == "equity" || command == "pot-odds" || command == "ev" || command == "decision" || command == "analyze") && argc >= 3 && is_help_token(argv[2])) {
            if (command == "equity") {
                print_equity_usage(std::cout);
            } else if (command == "analyze") {
                print_analyze_usage(std::cout);
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

        if (command == "play") {
            poker::run_game_session(std::cin, std::cout);
            return 0;
        }

        if (command == "decision") {
            const DecisionCommandOptions options = parse_decision_command_arguments(argc, argv, "decision");
            const poker::HoldemHand hand = make_holdem_hand(options.hero_cards, options.board);
            const poker::PotOddsResult pot_odds = poker::calculate_pot_odds(options.pot_before_call, options.call_amount);
            poker::BettingState betting = make_decision_betting_state(options);

            const std::uint64_t theoretical_states = theoretical_exact_states_for_options(options);
            poker::EquityOptions equity_options = options.equity_options;
            const MethodSelection selection = select_equity_method(options.requested_method,
                                                                  theoretical_states,
                                                                  options.simulations_set,
                                                                  equity_options);
            betting.call_amount = options.call_amount;

            const auto start = std::chrono::steady_clock::now();
            const poker::EquityResult equity = poker::calculate_equity(hand, options.opponents, equity_options);
            const poker::DecisionResult decision = poker::evaluate_decision(betting, equity.equity);
            const auto end = std::chrono::steady_clock::now();
            const std::chrono::duration<double> elapsed = end - start;

            (void)elapsed;
            DecisionCommandOptions selected_options = options;
            selected_options.equity_options = equity_options;
            print_decision_result(selected_options, equity, pot_odds, decision, selection.reason);
            return 0;
        }

        if (command == "analyze") {
            const DecisionCommandOptions options = parse_analyze_arguments(argc, argv);
            const poker::HoldemHand hand = make_holdem_hand(options.hero_cards, options.board);
            const std::uint64_t theoretical_states = theoretical_exact_states_for_options(options);
            poker::EquityOptions equity_options = options.equity_options;
            const MethodSelection selection = select_equity_method(options.requested_method,
                                                                  theoretical_states,
                                                                  options.simulations_set,
                                                                  equity_options);

            const poker::EquityResult equity = poker::calculate_equity(hand, options.opponents, equity_options);
            const poker::PotOddsResult pot_odds = poker::calculate_pot_odds(options.pot_before_call, options.call_amount);
            const poker::BettingState betting = make_analysis_betting_state(options);
            const poker::DecisionResult decision = poker::evaluate_decision(betting, equity.equity);

            DecisionCommandOptions selected_options = options;
            selected_options.equity_options = equity_options;
            print_analyze_result(selected_options, equity, pot_odds, decision, selection.reason);
            return 0;
        }

        const CliOptions options = parse_equity_arguments(argc, argv);
        const std::uint64_t theoretical_states = theoretical_exact_states_for_options(options);
        poker::EquityOptions equity_options = options.equity_options;
        const MethodSelection selection = select_equity_method(options.requested_method,
                                                              theoretical_states,
                                                              options.simulations_set,
                                                              equity_options);

        const auto start = std::chrono::steady_clock::now();
        if (options.mode == EquityCliMode::specific_hand_vs_specific_hand) {
            const poker::HoldemHand hand = make_holdem_hand(options.hero_cards, options.board);
            if (selection.method == poker::EquityMethod::exact) {
                const poker::EquityResult result = poker::calculate_equity(hand, options.opponents, equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                CliOptions selected_options = options;
                selected_options.equity_options = equity_options;
                print_specific_hand_exact_result(selected_options, result, elapsed.count(), selection.reason);
            } else {
                const std::uint64_t seed = equity_options.seed.value_or(0U);
                const poker::EquityResult result = poker::calculate_equity(hand, options.opponents, equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                CliOptions selected_options = options;
                selected_options.equity_options = equity_options;
                print_specific_hand_monte_carlo_result(selected_options, result, seed, elapsed.count(), selection.reason);
            }
        } else if (options.mode == EquityCliMode::specific_hand_vs_range) {
            const poker::HoldemHand hand = make_holdem_hand(options.hero_cards, options.board);
            if (selection.method == poker::EquityMethod::exact) {
                const poker::EquityResult result = poker::calculate_equity(hand, *options.villain_range, equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                std::cout << "Hero: " << format_cards(options.hero_cards) << '\n';
                std::cout << "Villain range: " << format_range_label(*options.villain_range_notation, *options.villain_range) << '\n';
                std::cout << "Board: " << format_board(options.board) << '\n';
                std::cout << "Method: " << format_method_name(equity_options.method) << '\n';
                if (selection.reason.has_value()) {
                    std::cout << "Reason: " << *selection.reason << '\n';
                }
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
                const std::uint64_t seed = equity_options.seed.value_or(0U);
                const poker::EquityResult result = poker::calculate_equity(hand, *options.villain_range, equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                std::cout << "Hero: " << format_cards(options.hero_cards) << '\n';
                std::cout << "Villain range: " << format_range_label(*options.villain_range_notation, *options.villain_range) << '\n';
                std::cout << "Board: " << format_board(options.board) << '\n';
                std::cout << "Method: " << format_method_name(equity_options.method) << '\n';
                if (selection.reason.has_value()) {
                    std::cout << "Reason: " << *selection.reason << '\n';
                }
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
            if (selection.method == poker::EquityMethod::exact) {
                const poker::EquityResult result = poker::calculate_equity(*options.hero_range, *options.villain_range, options.board, equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                CliOptions selected_options = options;
                selected_options.equity_options = equity_options;
                print_range_exact_result(selected_options, result, elapsed.count(), selection.reason);
            } else {
                const std::uint64_t seed = equity_options.seed.value_or(0U);
                const poker::EquityResult result = poker::calculate_equity(*options.hero_range, *options.villain_range, options.board, equity_options);
                const auto end = std::chrono::steady_clock::now();
                const std::chrono::duration<double> elapsed = end - start;
                CliOptions selected_options = options;
                selected_options.equity_options = equity_options;
                print_range_monte_carlo_result(selected_options, result, seed, elapsed.count(), selection.reason);
            }
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
