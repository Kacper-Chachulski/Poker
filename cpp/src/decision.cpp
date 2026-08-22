#include "poker/decision.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

#include "equity_common.hpp"
#include "poker/ev.hpp"

namespace poker {

namespace {

constexpr double kStrongEquityThreshold = 0.60;
constexpr double kModerateEquityThreshold = 0.40;
constexpr double kOpeningBetFractionOfPot = 0.66;
constexpr double kRaiseTotalFractionOfCall = 2.50;

struct HeuristicRecommendation {
    std::optional<BettingAction> action{};
    std::optional<double> amount{};
    std::optional<std::string> rationale{};
};

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

double clamp_amount(double amount, double minimum, double maximum) {
    if (maximum < minimum) {
        return maximum;
    }
    return std::max(minimum, std::min(amount, maximum));
}

HeuristicRecommendation make_heuristic_recommendation(const BettingState& betting, double equity, const std::vector<BettingAction>& legal_actions) {
    HeuristicRecommendation recommendation{};

    const bool can_check = contains_action(legal_actions, BettingAction::check);
    const bool can_bet = contains_action(legal_actions, BettingAction::bet);
    const bool can_call = contains_action(legal_actions, BettingAction::call);
    const bool can_raise = contains_action(legal_actions, BettingAction::raise);
    const bool can_fold = contains_action(legal_actions, BettingAction::fold);
    const bool can_all_in = contains_action(legal_actions, BettingAction::all_in);

    if (betting.call_amount == 0.0) {
        if (equity >= kStrongEquityThreshold && can_bet) {
            const double target_bet = clamp_amount(betting.current_pot * kOpeningBetFractionOfPot,
                                                   *betting.minimum_raise_amount,
                                                   betting.hero_stack);
            recommendation.action = (target_bet >= betting.hero_stack && can_all_in) ? BettingAction::all_in : BettingAction::bet;
            recommendation.amount = target_bet;
            recommendation.rationale = "Strong equity";
            return recommendation;
        }

        if (can_check) {
            recommendation.action = BettingAction::check;
            recommendation.rationale = equity >= kModerateEquityThreshold ? "Moderate equity" : "Weak equity";
            return recommendation;
        }

        if (can_all_in) {
            recommendation.action = BettingAction::all_in;
            recommendation.amount = betting.hero_stack;
            recommendation.rationale = "Strong equity";
        }

        return recommendation;
    }

    if (equity >= kStrongEquityThreshold && can_raise) {
        const double target_raise = clamp_amount(betting.call_amount * kRaiseTotalFractionOfCall,
                                                 betting.call_amount + *betting.minimum_raise_amount,
                                                 betting.hero_stack);
        recommendation.action = (target_raise >= betting.hero_stack && can_all_in) ? BettingAction::all_in : BettingAction::raise;
        recommendation.amount = target_raise;
        recommendation.rationale = "Strong equity";
        return recommendation;
    }

    if (equity >= kModerateEquityThreshold && can_call) {
        recommendation.action = BettingAction::call;
        recommendation.rationale = "Moderate equity";
        return recommendation;
    }

    if (can_fold) {
        recommendation.action = BettingAction::fold;
        recommendation.rationale = "Weak equity";
        return recommendation;
    }

    if (can_all_in) {
        recommendation.action = BettingAction::all_in;
        recommendation.amount = betting.hero_stack;
        recommendation.rationale = "Strong equity";
    }

    return recommendation;
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

    const HeuristicRecommendation recommendation = make_heuristic_recommendation(betting, equity, legal_actions);
    if (recommendation.action.has_value()) {
        result.best_action = recommendation.action;
        result.heuristic_recommendation = true;
        result.suggested_amount = recommendation.amount;
        result.rationale = recommendation.rationale;

        const auto selected = std::find_if(result.actions.begin(), result.actions.end(), [&](const ActionEvaluation& evaluation) {
            return evaluation.action == *recommendation.action;
        });
        if (selected != result.actions.end() && selected->supported && selected->ev.has_value()) {
            result.best_ev = selected->ev;
        } else {
            result.best_ev.reset();
        }
    }

    return result;
}

DecisionResult evaluate_decision(const GameState& state, double equity) {
    validate_game_state(state);
    return evaluate_decision(state.betting, equity);
}

}  // namespace poker