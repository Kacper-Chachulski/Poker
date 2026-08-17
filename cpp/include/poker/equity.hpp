#pragma once

#include <cstddef>
#include <cstdint>

#include "poker/hand.hpp"

namespace poker {

struct EquityResult {
    double win_probability{0.0};
    double tie_probability{0.0};
    double loss_probability{0.0};
    double equity{0.0};
    std::uint64_t simulations{0};
    std::uint64_t evaluated_states{0};
};

EquityResult simulate_equity(const HoldemHand& hero,
                             std::size_t opponents,
                             std::size_t simulations,
                             std::uint64_t seed);

}  // namespace poker