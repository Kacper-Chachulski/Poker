#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace poker {

enum class BettingAction : std::uint8_t {
    check,
    bet,
    call,
    raise,
    fold,
    all_in,
};

struct BettingState {
    double current_pot{0.0};
    double call_amount{0.0};
    double hero_stack{0.0};
    std::optional<double> minimum_raise_amount{};
    bool check_allowed{false};
};

void validate_betting_state(const BettingState& state);

std::vector<BettingAction> get_legal_actions(const BettingState& state);

}  // namespace poker