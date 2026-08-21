#include "poker/betting.hpp"

#include <cmath>
#include <stdexcept>

#include "equity_common.hpp"

namespace poker {

namespace {

bool is_finite_non_negative(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

void append_action(std::vector<BettingAction>& actions, BettingAction action) {
    actions.push_back(action);
}

}  // namespace

void validate_betting_state(const BettingState& state) {
    if (!is_finite_non_negative(state.current_pot)) {
        detail::throw_invalid("Current pot must be non-negative and finite");
    }

    if (!is_finite_non_negative(state.call_amount)) {
        detail::throw_invalid("Call amount must be non-negative and finite");
    }

    if (!is_finite_non_negative(state.hero_stack)) {
        detail::throw_invalid("Hero stack must be non-negative and finite");
    }

    if (state.call_amount > state.hero_stack) {
        detail::throw_invalid("Call amount cannot exceed Hero's remaining stack");
    }

    if (state.check_allowed && state.call_amount > 0.0) {
        detail::throw_invalid("Check cannot be allowed when a call amount is required");
    }

    if (!state.check_allowed && state.call_amount == 0.0 && state.hero_stack > 0.0) {
        detail::throw_invalid("Check must be allowed when no bet is faced");
    }

    if (state.minimum_raise_amount.has_value()) {
        if (!is_finite_non_negative(*state.minimum_raise_amount) || *state.minimum_raise_amount <= 0.0) {
            detail::throw_invalid("Minimum raise amount must be positive and finite when provided");
        }
    }

    if (state.call_amount == 0.0 && state.hero_stack > 0.0 && !state.minimum_raise_amount.has_value()) {
        detail::throw_invalid("A betting amount is required when no bet is faced and chips remain");
    }
}

std::vector<BettingAction> get_legal_actions(const BettingState& state) {
    validate_betting_state(state);

    std::vector<BettingAction> actions{};
    actions.reserve(4U);

    if (state.call_amount == 0.0) {
        if (state.check_allowed) {
            append_action(actions, BettingAction::check);
        }

        if (state.hero_stack > 0.0) {
            if (state.minimum_raise_amount.has_value() && state.hero_stack >= *state.minimum_raise_amount) {
                append_action(actions, BettingAction::bet);
            }

            append_action(actions, BettingAction::all_in);
        }

        return actions;
    }

    append_action(actions, BettingAction::fold);

    if (state.hero_stack >= state.call_amount) {
        append_action(actions, BettingAction::call);
    }

    if (state.minimum_raise_amount.has_value() &&
        state.hero_stack > state.call_amount + *state.minimum_raise_amount) {
        append_action(actions, BettingAction::raise);
    }

    if (state.hero_stack > state.call_amount) {
        append_action(actions, BettingAction::all_in);
    }

    return actions;
}

}  // namespace poker