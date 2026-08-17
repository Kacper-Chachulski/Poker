#pragma once

#include <cstddef>
#include <cstdint>

#include "poker/equity.hpp"

namespace poker {

EquityResult solve_exact_equity(const HoldemHand& hero, std::size_t opponents);

}  // namespace poker