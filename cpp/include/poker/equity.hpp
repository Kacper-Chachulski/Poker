#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "poker/hand.hpp"
#include "poker/range.hpp"

namespace poker {

enum class EquityMethod {
    exact,
    monte_carlo,
};

struct EquityOptions {
    EquityMethod method{EquityMethod::exact};
    std::uint64_t simulations{0U};
    std::optional<std::uint64_t> seed{};
};

struct EquityResult {
    double win_probability{0.0};
    double tie_probability{0.0};
    double loss_probability{0.0};
    double equity{0.0};
    std::uint64_t simulations{0};
    std::uint64_t evaluated_states{0};
};

EquityResult calculate_equity(const HoldemHand& hero,
                              std::size_t opponents,
                              const EquityOptions& options = {});

std::uint64_t theoretical_exact_states(const HoldemHand& hero, std::size_t opponents);

EquityResult calculate_equity(const HoldemHand& hero,
                              const HandRange& villain_range,
                              const EquityOptions& options = {});

std::uint64_t theoretical_exact_states(const HoldemHand& hero, const HandRange& villain_range);

EquityResult calculate_equity(const HandRange& hero_range,
                              const HandRange& villain_range,
                              const std::vector<Card>& board,
                              const EquityOptions& options = {});

std::uint64_t theoretical_exact_states(const HandRange& hero_range,
                                       const HandRange& villain_range,
                                       const std::vector<Card>& board);

EquityResult simulate_equity(const HoldemHand& hero,
                             std::size_t opponents,
                             std::size_t simulations,
                             std::uint64_t seed);

}  // namespace poker