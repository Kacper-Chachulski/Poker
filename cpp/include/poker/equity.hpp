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

EquityResult calculate_equity(const HoldemHand& hero,
                              const HandRange& villain_range,
                              const EquityOptions& options = {});

EquityResult calculate_equity(const HandRange& hero_range,
                              const HandRange& villain_range,
                              const std::vector<Card>& board,
                              const EquityOptions& options = {});

EquityResult simulate_equity(const HoldemHand& hero,
                             std::size_t opponents,
                             std::size_t simulations,
                             std::uint64_t seed);

}  // namespace poker