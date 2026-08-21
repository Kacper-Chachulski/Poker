#include <cstddef>
#include <array>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <string>

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

constexpr std::uint64_t kDefaultSimulations = 100'000ULL;
constexpr std::uint64_t kBaseSeed = 12345ULL;

poker::Card parse_card(const char* text) {
    const std::optional<poker::Card> parsed = poker::Card::from_string(text);
    if (!parsed) {
        throw std::runtime_error(std::string("invalid card literal: ") + text);
    }
    return *parsed;
}

poker::HoldemHand make_hand(const char* h1,
                            const char* h2,
                            std::initializer_list<const char*> board) {
    poker::HoldemHand hand{};
    hand.hole[0] = parse_card(h1);
    hand.hole[1] = parse_card(h2);
    hand.board_count = static_cast<std::uint8_t>(board.size());
    std::size_t index = 0U;
    for (const char* card_text : board) {
        hand.board[index++] = parse_card(card_text);
    }
    return hand;
}

std::uint64_t parse_simulations(int argc, char* argv[]) {
    if (argc == 1) {
        return kDefaultSimulations;
    }

    if (argc == 3 && std::string(argv[1]) == "--simulations") {
        return static_cast<std::uint64_t>(std::stoull(argv[2]));
    }

    if (argc == 2) {
        return static_cast<std::uint64_t>(std::stoull(argv[1]));
    }

    throw std::runtime_error("Usage: poker_equity_benchmark [--simulations N|N]");
}

std::string format_number(std::uint64_t value) {
    std::string text = std::to_string(value);
    for (std::ptrdiff_t index = static_cast<std::ptrdiff_t>(text.size()) - 3; index > 0; index -= 3) {
        text.insert(static_cast<std::size_t>(index), ",");
    }
    return text;
}

std::string format_cards(const poker::HoldemHand& hand) {
    std::string text;
    text += hand.hole[0].to_string();
    text += ' ';
    text += hand.hole[1].to_string();
    return text;
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

void run_mixed_scenario(const MixedScenario& scenario, std::uint64_t simulations, std::uint64_t seed) {
    const auto start = std::chrono::steady_clock::now();
    poker::EquityOptions options{};
    options.method = poker::EquityMethod::monte_carlo;
    options.simulations = simulations;
    options.seed = seed;
    const poker::EquityResult result = poker::calculate_equity(scenario.state, options);
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    const double sims_per_second = static_cast<double>(simulations) / elapsed.count();

    std::cout << scenario.name << '\n';
    std::cout << "  Hero: " << format_cards(scenario.state.hero) << '\n';
    std::cout << "  Board: " << format_board(scenario.state.hero) << '\n';
    std::cout << "  Opponents: specific + range + random" << '\n';
    std::cout << "  Simulations: " << format_number(simulations) << '\n';
    std::cout << "  Seed: " << seed << '\n';
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Win:   " << result.win_probability * 100.0 << "%\n";
    std::cout << "  Tie:   " << result.tie_probability * 100.0 << "%\n";
    std::cout << "  Loss:  " << result.loss_probability * 100.0 << "%\n";
    std::cout << "  Equity:" << ' ' << result.equity * 100.0 << "%\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "  Speed: " << sims_per_second << " simulations/sec\n";
    std::cout << "  Elapsed: " << std::fixed << std::setprecision(6) << elapsed.count() << " seconds\n\n";
}

void run_scenario(const Scenario& scenario, std::uint64_t simulations, std::uint64_t seed) {
    const auto start = std::chrono::steady_clock::now();
    poker::EquityOptions options{};
    options.method = poker::EquityMethod::monte_carlo;
    options.simulations = simulations;
    options.seed = seed;
    const poker::EquityResult result = poker::calculate_equity(scenario.hero, scenario.opponents, options);
    const auto end = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end - start;
    const double sims_per_second = static_cast<double>(simulations) / elapsed.count();

    std::cout << scenario.name << '\n';
    std::cout << "  Hero: " << format_cards(scenario.hero) << '\n';
    std::cout << "  Board: " << format_board(scenario.hero) << '\n';
    std::cout << "  Opponents: " << scenario.opponents << '\n';
    std::cout << "  Simulations: " << format_number(simulations) << '\n';
    std::cout << "  Seed: " << seed << '\n';
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Win:   " << result.win_probability * 100.0 << "%\n";
    std::cout << "  Tie:   " << result.tie_probability * 100.0 << "%\n";
    std::cout << "  Loss:  " << result.loss_probability * 100.0 << "%\n";
    std::cout << "  Equity:" << ' ' << result.equity * 100.0 << "%\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "  Speed: " << sims_per_second << " simulations/sec\n";
    std::cout << "  Elapsed: " << std::fixed << std::setprecision(6) << elapsed.count() << " seconds\n\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const std::uint64_t simulations = parse_simulations(argc, argv);
        const std::array<Scenario, 6> scenarios{ {
            {"Pre-flop, 1 opponent", make_hand("As", "Ks", {}), 1U},
            {"Pre-flop, 2 opponents", make_hand("As", "Ks", {}), 2U},
            {"Pre-flop, 5 opponents", make_hand("As", "Ks", {}), 5U},
            {"Flop, 2 opponents", make_hand("As", "Ks", {"Qh", "7c", "2s"}), 2U},
            {"Turn, 2 opponents", make_hand("As", "Ks", {"Qh", "7c", "2s", "Td"}), 2U},
            {"River, 2 opponents", make_hand("As", "Ks", {"Qh", "7c", "2s", "Td", "4h"}), 2U},
        } };

        std::cout << "Equity benchmark\n";
        std::cout << "Simulations per case: " << format_number(simulations) << "\n\n";
        for (std::size_t index = 0; index < scenarios.size(); ++index) {
            run_scenario(scenarios[index], simulations, kBaseSeed + index);
        }

        run_mixed_scenario({"Mixed, specific + range + random", make_mixed_state()}, simulations, kBaseSeed + 100U);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}