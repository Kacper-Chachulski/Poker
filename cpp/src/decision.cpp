#include "poker/decision.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

#include "equity_common.hpp"
#include "poker/ev.hpp"

namespace poker {

namespace {

bool contains_action(const std::vector<BettingAction>& actions, BettingAction action) {
    return std::find(actions.begin(), actions.end(), action) != actions.end();
}

ActionEvaluation make_unsupported_evaluation(BettingAction action, bool legal) {
    ActionEvaluation evaluation{};
    evaluation.action = action;
    evaluation.legal = legal;
    evaluation.supported = false;
    return evaluation;
}

ActionEvaluation make_supported_evaluation(BettingAction action, double ev, double invested_amount, bool legal) {
    ActionEvaluation evaluation{};
    evaluation.action = action;
    evaluation.legal = legal;
    evaluation.supported = true;
    evaluation.ev = ev;
    evaluation.invested_amount = invested_amount;
    return evaluation;
}

}  // namespace

ActionEvaluation evaluate_action(const BettingState& betting, double equity, BettingAction action) {
    validate_betting_state(betting);

    if (!std::isfinite(equity) || equity < 0.0 || equity > 1.0) {
        detail::throw_invalid("Equity must be between 0 and 1");
    }

    const std::vector<BettingAction> legal_actions = get_legal_actions(betting);
    const bool legal = contains_action(legal_actions, action);
    if (!legal) {
        return make_unsupported_evaluation(action, false);
    }

    switch (action) {
        case BettingAction::fold:
            return make_supported_evaluation(action, 0.0, 0.0, true);
        case BettingAction::call:
            return make_supported_evaluation(action, calculate_call_ev(equity, betting), betting.call_amount, true);
        case BettingAction::check:
        case BettingAction::bet:
        case BettingAction::raise:
        case BettingAction::all_in:
            return make_unsupported_evaluation(action, true);
    }

    detail::throw_invalid("Unknown betting action");
    return {};
}

ActionEvaluation evaluate_action(const GameState& state, double equity, BettingAction action) {
    validate_game_state(state);
    return evaluate_action(state.betting, equity, action);
}

DecisionResult evaluate_decision(const BettingState& betting, double equity) {
    validate_betting_state(betting);

    if (!std::isfinite(equity) || equity < 0.0 || equity > 1.0) {
        detail::throw_invalid("Equity must be between 0 and 1");
    }

    DecisionResult result{};
    const std::vector<BettingAction> legal_actions = get_legal_actions(betting);
    result.actions.reserve(legal_actions.size());

    for (BettingAction action : legal_actions) {
        result.actions.push_back(evaluate_action(betting, equity, action));
    }

    for (const ActionEvaluation& evaluation : result.actions) {
        if (!evaluation.supported || !evaluation.ev.has_value()) {
            continue;
        }

        if (!result.best_ev.has_value() || *evaluation.ev > *result.best_ev) {
            result.best_ev = evaluation.ev;
            result.best_action = evaluation.action;
        }
    }

    return result;
}

DecisionResult evaluate_decision(const GameState& state, double equity) {
    validate_game_state(state);
    return evaluate_decision(state.betting, equity);
}

}  // namespace poker