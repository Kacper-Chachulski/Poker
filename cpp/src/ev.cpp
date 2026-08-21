#include "poker/ev.hpp"

#include <cmath>
#include <stdexcept>

namespace poker {

namespace {

void validate_money_value(double value, const char* label) {
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(label);
    }
}

void validate_equity_value(double equity) {
    if (!std::isfinite(equity) || equity < 0.0 || equity > 1.0) {
        throw std::invalid_argument("Equity must be between 0 and 1");
    }
}

}  // namespace

PotOddsResult calculate_pot_odds(double pot_before_call, double call_amount) {
    validate_money_value(pot_before_call, "Pot before call must be non-negative and finite");
    validate_money_value(call_amount, "Call amount must be non-negative and finite");

    PotOddsResult result{};
    result.pot_before_call = pot_before_call;
    result.call_amount = call_amount;
    result.final_pot = pot_before_call + call_amount;
    result.required_equity = (result.final_pot == 0.0) ? 0.0 : call_amount / result.final_pot;
    if (call_amount > 0.0) {
        result.pot_odds_ratio = pot_before_call / call_amount;
    }

    return result;
}

double calculate_break_even_equity(double pot_before_call, double call_amount) {
    return calculate_pot_odds(pot_before_call, call_amount).required_equity;
}

double calculate_call_ev(double equity, double pot_before_call, double call_amount) {
    validate_equity_value(equity);
    validate_money_value(pot_before_call, "Pot before call must be non-negative and finite");
    validate_money_value(call_amount, "Call amount must be non-negative and finite");

    return equity * pot_before_call - (1.0 - equity) * call_amount;
}

double calculate_fold_ev(double pot_before_call, double call_amount) {
    validate_money_value(pot_before_call, "Pot before call must be non-negative and finite");
    validate_money_value(call_amount, "Call amount must be non-negative and finite");

    return 0.0;
}

}  // namespace poker