#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "poker/card.hpp"
#include "poker/game_state.hpp"
#include "poker/hand.hpp"
#include "poker/equity.hpp"

namespace {

struct Scenario {
    const char* name;
    poker::HoldemHand hero;
    std::size_t opponents;
};

struct MixedScenario {
    const char* name;
    poker::GameState state;
};

poker::Card parse_card(std::string_view text) {
    const std::optional<poker::Card> parsed = poker::Card::from_string(text);
    if (!parsed) {
        throw std::invalid_argument(std::string("Invalid card notation: ") + std::string(text));
    }
    return *parsed;
}

poker::HoldemHand make_hand(std::string_view h1,
                            std::string_view h2,
                            std::initializer_list<std::string_view> board = {}) {
    poker::HoldemHand hand{};
    hand.hole[0] = parse_card(h1);
    hand.hole[1] = parse_card(h2);
    hand.board_count = static_cast<std::uint8_t>(board.size());
    std::size_t index = 0U;
    for (std::string_view card_text : board) {
        hand.board[index++] = parse_card(card_text);
    }
    return hand;
}

std::uint64_t parse_simulations(int argc, char* argv[]) {
    if (argc == 1) {
        return 1U;
    }

    if (argc == 3 && std::string_view(argv[1]) == "--runs") {
        return static_cast<std::uint64_t>(std::stoull(argv[2]));
    }

    if (argc == 2) {
        return static_cast<std::uint64_t>(std::stoull(argv[1]));
    }

    throw std::runtime_error("Usage: poker_exact_benchmark [--runs N|N]");
}

std::uint64_t binomial(std::uint64_t n, std::uint64_t k) {
    if (k > n) {
        return 0U;
    }
    if (k > n - k) {
        k = n - k;
    }

    std::uint64_t result = 1U;
    for (std::uint64_t index = 1U; index <= k; ++index) {
        result = (result * (n - k + index)) / index;
    }
    return result;
}

std::uint64_t theoretical_states(std::size_t board_count, std::size_t opponents) {
    const std::size_t missing_board = 5U - board_count;
    std::uint64_t remaining = 52U - 2U - board_count;
    const std::uint64_t board_states = binomial(remaining, missing_board);
    remaining -= missing_board;

    std::uint64_t opponent_states = 1U;
    for (std::size_t opponent = 0; opponent < opponents; ++opponent) {
        opponent_states *= binomial(remaining, 2U);
        remaining -= 2U;
    }

    return board_states * opponent_states;
}

std::string format_number(std::uint64_t value) {
    std::string text = std::to_string(value);
    for (std::ptrdiff_t index = static_cast<std::ptrdiff_t>(text.size()) - 3; index > 0; index -= 3) {
        text.insert(static_cast<std::size_t>(index), ",");
    }
    return text;
}

std::string format_cards(const poker::HoldemHand& hand) {
    return hand.hole[0].to_string() + std::string(" ") + hand.hole[1].to_string();
}

std::string format_board(const poker::HoldemHand& hand) {
    if (hand.board_count == 0U) {
        return "(none)";
    }

    std::string text;
    for (std::size_t index = 0; index < hand.board_count; ++index) {
        if (index > 0U) {
            text += ' ';
        }
        text += hand.board[index].to_string();
    }
    return text;
}

poker::GameState make_mixed_state() {
    poker::GameState state{};
    state.street = poker::Street::river;
    state.hero = make_hand("2c", "3d", {"As", "Ks", "Qs", "Js", "Ts"});
    state.betting.current_pot = 100.0;
    state.betting.call_amount = 50.0;
    state.betting.hero_stack = 1000.0;
    state.opponents = {
        poker::Opponent{poker::HandCombo(parse_card("4c"), parse_card("5d"))},
        poker::Opponent{poker::HandRange::parse("AA")},
        poker::Opponent{poker::RandomOpponent{}},
    };
    state.player_count = 4U;
    return state;
}

void run_scenario(const Scenario& scenario) {
    const std::uint64_t theoretical = theoretical_states(scenario.hero.board_count, scenario.opponents);
    const auto start = std::chrono::steady_clock::now();
    const poker::EquityOptions options{};
    const poker::EquityResult result = poker::calculate_equity(scenario.hero, scenario.opponents, options);
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    const double states_per_second = static_cast<double>(result.evaluated_states) / elapsed.count();

    std::cout << scenario.name << '\n';
    std::cout << "  Hero: " << format_cards(scenario.hero) << '\n';
    std::cout << "  Board: " << format_board(scenario.hero) << '\n';
    std::cout << "  Opponents: " << scenario.opponents << '\n';
    std::cout << "  Theoretical states: " << format_number(theoretical) << '\n';
    std::cout << "  Evaluated states: " << format_number(result.evaluated_states) << '\n';
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Win:   " << result.win_probability * 100.0 << "%\n";
    std::cout << "  Tie:   " << result.tie_probability * 100.0 << "%\n";
    std::cout << "  Loss:  " << result.loss_probability * 100.0 << "%\n";
    std::cout << "  Equity:" << ' ' << result.equity * 100.0 << "%\n";
    std::cout << "  Time:  " << std::setprecision(6) << elapsed.count() << " seconds\n";
    std::cout << "  Speed: " << format_number(static_cast<std::uint64_t>(states_per_second)) << " states/sec\n\n";
}

void run_mixed_scenario(const MixedScenario& scenario) {
    const std::uint64_t theoretical = 2460U;
    const auto start = std::chrono::steady_clock::now();
    const poker::EquityOptions options{};
    const poker::EquityResult result = poker::calculate_equity(scenario.state, options);
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    const double states_per_second = static_cast<double>(result.evaluated_states) / elapsed.count();

    std::cout << scenario.name << '\n';
    std::cout << "  Hero: " << format_cards(scenario.state.hero) << '\n';
    std::cout << "  Board: " << format_board(scenario.state.hero) << '\n';
    std::cout << "  Opponents: specific + range + random" << '\n';
    std::cout << "  Theoretical states: " << format_number(theoretical) << '\n';
    std::cout << "  Evaluated states: " << format_number(result.evaluated_states) << '\n';
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Win:   " << result.win_probability * 100.0 << "%\n";
    std::cout << "  Tie:   " << result.tie_probability * 100.0 << "%\n";
    std::cout << "  Loss:  " << result.loss_probability * 100.0 << "%\n";
    std::cout << "  Equity:" << ' ' << result.equity * 100.0 << "%\n";
    std::cout << "  Time:  " << std::setprecision(6) << elapsed.count() << " seconds\n";
    std::cout << "  Speed: " << format_number(static_cast<std::uint64_t>(states_per_second)) << " states/sec\n\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        (void)parse_simulations(argc, argv);

        const std::array<Scenario, 4> scenarios{ {
            {"River, 1 opponent", make_hand("As", "Ks", {"Qh", "7c", "2s", "Td", "4h"}), 1U},
            {"River, 2 opponents", make_hand("As", "Ks", {"Qh", "7c", "2s", "Td", "4h"}), 2U},
            {"Turn, 1 opponent", make_hand("As", "Ks", {"Qh", "7c", "2s", "Td"}), 1U},
            {"Flop, 1 opponent", make_hand("As", "Ks", {"Qh", "7c", "2s"}), 1U},
        } };

        std::cout << "Exact equity benchmark\n\n";
        for (const Scenario& scenario : scenarios) {
            run_scenario(scenario);
        }

        run_mixed_scenario({"Mixed, specific + range + random", make_mixed_state()});

        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
