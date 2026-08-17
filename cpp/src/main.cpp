#include <cstddef>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

#include "poker/card.hpp"
#include "poker/equity.hpp"
#include "poker/exact_equity.hpp"
#include "poker/hand.hpp"

namespace {

enum class Method {
    monte_carlo,
    exact,
};

struct CliOptions {
    poker::HoldemHand hero;
    std::size_t opponents{0U};
    std::size_t simulations{0U};
    std::optional<std::uint64_t> seed;
    Method method{Method::monte_carlo};
};

bool starts_with_option(std::string_view token) {
    return token.rfind("--", 0U) == 0U;
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

Method parse_method(std::string_view text) {
    if (text == "exact") {
        return Method::exact;
    }
    if (text == "montecarlo") {
        return Method::monte_carlo;
    }
    throw std::invalid_argument(std::string("Invalid method: ") + std::string(text));
}

CliOptions parse_equity_arguments(int argc, char* argv[]) {
    if (argc < 4) {
        throw std::invalid_argument(
            "Usage: poker equity <hero card 1> <hero card 2> [--board cards...] --opponents N [--method exact|montecarlo] [--simulations N] [--seed N]");
    }

    if (std::string_view(argv[1]) != "equity") {
        throw std::invalid_argument("Only the 'equity' command is supported");
    }

    CliOptions options{};
    options.hero.hole[0] = parse_card(argv[2]);
    options.hero.hole[1] = parse_card(argv[3]);

    std::size_t index = 4U;
    while (index < static_cast<std::size_t>(argc)) {
        const std::string_view token = argv[index];

        if (token == "--board") {
            ++index;
            while (index < static_cast<std::size_t>(argc) && !starts_with_option(argv[index])) {
                if (options.hero.board_count >= 5U) {
                    throw std::invalid_argument("Board cannot contain more than 5 cards");
                }
                options.hero.board[options.hero.board_count++] = parse_card(argv[index]);
                ++index;
            }
            continue;
        }

        if (token == "--opponents") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--opponents requires a value");
            }
            options.opponents = static_cast<std::size_t>(parse_u64(argv[index + 1U], "opponents"));
            index += 2U;
            continue;
        }

        if (token == "--simulations") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--simulations requires a value");
            }
            options.simulations = static_cast<std::size_t>(parse_u64(argv[index + 1U], "simulations"));
            index += 2U;
            continue;
        }

        if (token == "--seed") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--seed requires a value");
            }
            options.seed = parse_u64(argv[index + 1U], "seed");
            index += 2U;
            continue;
        }

        if (token == "--method") {
            if (index + 1U >= static_cast<std::size_t>(argc)) {
                throw std::invalid_argument("--method requires a value");
            }
            options.method = parse_method(argv[index + 1U]);
            index += 2U;
            continue;
        }

        if (starts_with_option(token)) {
            throw std::invalid_argument(std::string("Unknown option: ") + std::string(token));
        }

        if (options.hero.board_count >= 5U) {
            throw std::invalid_argument("Board cannot contain more than 5 cards");
        }
        options.hero.board[options.hero.board_count++] = parse_card(token);
        ++index;
    }

    if (options.opponents == 0U || options.opponents > 5U) {
        throw std::invalid_argument("--opponents must be between 1 and 5");
    }

    if (options.hero.board_count != 0U && options.hero.board_count != 3U &&
        options.hero.board_count != 4U && options.hero.board_count != 5U) {
        throw std::invalid_argument("Board must contain 0, 3, 4, or 5 cards");
    }

    if (options.method == Method::monte_carlo && options.simulations == 0U) {
        throw std::invalid_argument("--simulations must be greater than 0 for Monte Carlo mode");
    }

    return options;
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

std::uint64_t choose_seed(const std::optional<std::uint64_t>& provided_seed) {
    if (provided_seed.has_value()) {
        return *provided_seed;
    }

    std::random_device device;
    const std::uint64_t high = static_cast<std::uint64_t>(device()) << 32U;
    const std::uint64_t low = static_cast<std::uint64_t>(device());
    return high ^ low;
}

void print_monte_carlo_result(const CliOptions& options, const poker::EquityResult& result, std::uint64_t seed, double seconds) {
    const double sims_per_second = static_cast<double>(result.simulations) / seconds;

    std::cout << "Hero: " << format_cards(options.hero) << '\n';
    std::cout << "Board: " << format_board(options.hero) << '\n';
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

void print_exact_result(const CliOptions& options, const poker::EquityResult& result, double seconds) {
    const double states_per_second = static_cast<double>(result.evaluated_states) / seconds;

    std::cout << "Hero: " << format_cards(options.hero) << '\n';
    std::cout << "Board: " << format_board(options.hero) << '\n';
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

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const CliOptions options = parse_equity_arguments(argc, argv);
        const std::uint64_t seed = choose_seed(options.seed);

        const auto start = std::chrono::steady_clock::now();
        if (options.method == Method::exact) {
            const poker::EquityResult result = poker::solve_exact_equity(options.hero, options.opponents);
            const auto end = std::chrono::steady_clock::now();
            const std::chrono::duration<double> elapsed = end - start;
            print_exact_result(options, result, elapsed.count());
        } else {
            const poker::EquityResult result = poker::simulate_equity(options.hero, options.opponents, options.simulations, seed);
            const auto end = std::chrono::steady_clock::now();
            const std::chrono::duration<double> elapsed = end - start;
            print_monte_carlo_result(options, result, seed, elapsed.count());
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
