#pragma once

#include <cstddef>
#include <optional>

namespace poker {

struct PotOddsResult {
    double pot_before_call{0.0};
    double call_amount{0.0};
    double final_pot{0.0};
    double required_equity{0.0};
    std::optional<double> pot_odds_ratio{};
};

PotOddsResult calculate_pot_odds(double pot_before_call, double call_amount);

double calculate_break_even_equity(double pot_before_call, double call_amount);

double calculate_call_ev(double equity, double pot_before_call, double call_amount);

double calculate_fold_ev(double pot_before_call, double call_amount);

}  // namespace poker