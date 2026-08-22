#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "poker/betting.hpp"
#include "poker/game_state.hpp"

namespace poker {

struct ActionEvaluation {
    BettingAction action{BettingAction::fold};
    bool legal{false};
    bool supported{false};
    std::optional<double> ev{};
    std::optional<double> invested_amount{};
};

struct DecisionResult {
    std::vector<ActionEvaluation> actions{};
    std::optional<BettingAction> best_action{};
    std::optional<double> best_ev{};
    std::optional<double> suggested_amount{};
    std::optional<std::string> rationale{};
    bool heuristic_recommendation{false};
};

ActionEvaluation evaluate_action(const BettingState& betting, double equity, BettingAction action);
ActionEvaluation evaluate_action(const GameState& state, double equity, BettingAction action);

DecisionResult evaluate_decision(const BettingState& betting, double equity);
DecisionResult evaluate_decision(const GameState& state, double equity);
inline DecisionResult evaluate_decision(const GameState& state, const EquityResult& equity) {
    return evaluate_decision(state, equity.equity);
}

}  // namespace poker