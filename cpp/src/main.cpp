#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>

#include "poker/deck.hpp"
#include "poker/evaluator.hpp"

namespace {

constexpr std::uint64_t kDefaultIterations = 1'000'000ULL;

std::uint64_t parse_iterations(int argc, char* argv[]) {
    if (argc == 1) {
        return kDefaultIterations;
    }

    if (argc == 3 && std::string(argv[1]) == "--iterations") {
        return static_cast<std::uint64_t>(std::stoull(argv[2]));
    }

    if (argc == 2) {
        return static_cast<std::uint64_t>(std::stoull(argv[1]));
    }

    throw std::runtime_error("Usage: poker_benchmark [--iterations N|N]");
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const std::uint64_t iterations = parse_iterations(argc, argv);
        poker::Deck deck;
        std::mt19937_64 generator{0xC0FFEEULL};

        std::uint64_t checksum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
            deck.reset();
            deck.shuffle(generator);

            std::array<poker::Card, 7> cards{};
            for (std::size_t index = 0; index < cards.size(); ++index) {
                cards[index] = deck.draw_unchecked();
            }

            const poker::HandValue value = poker::evaluate(cards, cards.size());
            checksum += value.score;
        }

        const auto end = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed = end - start;
        const double evaluations_per_second = static_cast<double>(iterations) / elapsed.count();

        std::cout << "iterations=" << iterations << '\n';
        std::cout << "checksum=" << checksum << '\n';
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "elapsed_seconds=" << elapsed.count() << '\n';
        std::cout << "evaluations_per_second=" << evaluations_per_second << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
