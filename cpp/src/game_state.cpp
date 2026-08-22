#include "poker/game_state.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <random>
#include <utility>
#include <vector>

#include "equity_common.hpp"
#include "poker/evaluator.hpp"
#include "poker/equity.hpp"

namespace poker {

std::uint64_t theoretical_exact_states(const GameState& state);

namespace {

constexpr std::size_t kMaxMixedKnownCards = 17U;

struct ShowdownAccumulator {
    std::uint64_t wins{0U};
    std::uint64_t ties{0U};
    std::uint64_t losses{0U};
    long double equity_sum{0.0L};
    std::uint64_t states{0U};
};

struct LeafOutcome {
    bool win{false};
    bool tie{false};
    bool loss{false};
    long double equity_share{0.0L};
};

std::size_t board_count_for_street(Street street) {
    switch (street) {
        case Street::preflop: return 0U;
        case Street::flop: return 3U;
        case Street::turn: return 4U;
        case Street::river: return 5U;
    }

    detail::throw_invalid("Invalid street");
    return 0U;
}

std::optional<std::size_t> next_eligible_seat(const std::vector<PlayerStatus>& seats,
                                             std::size_t from_seat,
                                             bool include_from) {
    const std::size_t player_count = seats.size();
    if (player_count == 0U) {
        return std::nullopt;
    }

    for (std::size_t offset = include_from ? 0U : 1U; offset <= player_count; ++offset) {
        const std::size_t seat = (from_seat + offset) % player_count;
        if (seats[seat] == PlayerStatus::active) {
            return seat;
        }
    }

    return std::nullopt;
}

std::array<Card, detail::kDeckSize> make_full_deck() {
    std::array<Card, detail::kDeckSize> deck{};
    for (std::uint8_t index = 0U; index < detail::kDeckSize; ++index) {
        deck[index] = Card::from_index(index);
    }
    return deck;
}

void remove_card(std::array<Card, detail::kDeckSize>& deck, std::size_t& remaining, Card card) {
    for (std::size_t index = 0U; index < remaining; ++index) {
        if (deck[index] == card) {
            deck[index] = deck[remaining - 1U];
            --remaining;
            return;
        }
    }

    detail::throw_invalid("Known card not found in deck");
}

bool combo_uses_known_card(const HandCombo& combo,
                           const std::array<Card, kMaxMixedKnownCards>& known_cards,
                           std::size_t known_count) {
    const std::array<Card, 2> cards = combo.cards();
    for (std::size_t index = 0U; index < known_count; ++index) {
        if (cards[0] == known_cards[index] || cards[1] == known_cards[index]) {
            return true;
        }
    }

    return false;
}

void append_combo_cards(const HandCombo& combo,
                        std::array<Card, kMaxMixedKnownCards>& known_cards,
                        std::size_t& known_count) {
    const std::array<Card, 2> cards = combo.cards();
    known_cards[known_count++] = cards[0];
    known_cards[known_count++] = cards[1];
}

std::vector<Card> collect_known_cards(const GameState& state) {
    std::vector<Card> known_cards{};
    known_cards.reserve(kMaxMixedKnownCards);

    known_cards.push_back(state.hero.hole[0]);
    known_cards.push_back(state.hero.hole[1]);
    for (std::size_t index = 0U; index < state.hero.board_count; ++index) {
        known_cards.push_back(state.hero.board[index]);
    }

    for (const Opponent& opponent : state.opponents) {
        if (const HandCombo* combo = std::get_if<HandCombo>(&opponent)) {
            const std::array<Card, 2> cards = combo->cards();
            known_cards.push_back(cards[0]);
            known_cards.push_back(cards[1]);
        }
    }

    return known_cards;
}

void validate_known_cards(const std::vector<Card>& cards) {
    for (std::size_t left = 0U; left < cards.size(); ++left) {
        for (std::size_t right = left + 1U; right < cards.size(); ++right) {
            if (cards[left] == cards[right]) {
                detail::throw_invalid("Duplicate cards are not allowed");
            }
        }
    }
}

std::size_t random_opponent_count(const GameState& state) {
    std::size_t count = 0U;
    for (const Opponent& opponent : state.opponents) {
        if (std::holds_alternative<RandomOpponent>(opponent)) {
            ++count;
        }
    }
    return count;
}

std::size_t known_opponent_count(const GameState& state) {
    std::size_t count = 0U;
    for (const Opponent& opponent : state.opponents) {
        if (!std::holds_alternative<RandomOpponent>(opponent)) {
            ++count;
        }
    }
    return count;
}

HandRange make_singleton_range(const HandCombo& combo) {
    return HandRange::from_combo(combo);
}

std::array<Card, kMaxMixedKnownCards> collect_initial_known_cards(const GameState& state, std::size_t& known_count) {
    std::array<Card, kMaxMixedKnownCards> known_cards{};
    known_count = 0U;

    known_cards[known_count++] = state.hero.hole[0];
    known_cards[known_count++] = state.hero.hole[1];
    for (std::size_t index = 0U; index < state.hero.board_count; ++index) {
        known_cards[known_count++] = state.hero.board[index];
    }

    for (const Opponent& opponent : state.opponents) {
        if (const HandCombo* combo = std::get_if<HandCombo>(&opponent)) {
            append_combo_cards(*combo, known_cards, known_count);
        }
    }

    return known_cards;
}

std::vector<std::array<Card, 2>> collect_fixed_villains(const GameState& state) {
    std::vector<std::array<Card, 2>> villains{};
    villains.reserve(state.opponents.size());

    for (const Opponent& opponent : state.opponents) {
        if (const HandCombo* combo = std::get_if<HandCombo>(&opponent)) {
            villains.push_back(combo->cards());
        }
    }

    return villains;
}

std::vector<const Opponent*> collect_flexible_opponents(const GameState& state) {
    std::vector<const Opponent*> flexible_opponents{};
    flexible_opponents.reserve(state.opponents.size());
    for (const Opponent& opponent : state.opponents) {
        if (!std::holds_alternative<HandCombo>(opponent)) {
            flexible_opponents.push_back(&opponent);
        }
    }

    return flexible_opponents;
}

std::vector<HandCombo> legal_combos_for_opponent(const Opponent& opponent,
                                                 const std::array<Card, detail::kDeckSize>& deck,
                                                 std::size_t remaining,
                                                 const std::array<Card, kMaxMixedKnownCards>& known_cards,
                                                 std::size_t known_count);

std::uint64_t count_mixed_exact_states(const HoldemHand& hero,
                                      const std::vector<const Opponent*>& flexible_opponents,
                                      std::size_t seat_index,
                                      const std::array<Card, detail::kDeckSize>& deck,
                                      std::size_t remaining,
                                      const std::array<Card, kMaxMixedKnownCards>& known_cards,
                                      std::size_t known_count) {
    if (seat_index == flexible_opponents.size()) {
        return detail::binomial(remaining, 5U - hero.board_count);
    }

    const Opponent& opponent = *flexible_opponents[seat_index];
    const std::vector<HandCombo> candidates = legal_combos_for_opponent(opponent, deck, remaining, known_cards, known_count);
    if (candidates.empty()) {
        detail::throw_invalid("No legal opponent combinations remain after card removal");
    }

    std::uint64_t total_states = 0U;
    for (const HandCombo& candidate : candidates) {
        const std::array<Card, 2> cards = candidate.cards();
        std::array<Card, detail::kDeckSize> next_deck = deck;
        std::size_t next_remaining = remaining;
        remove_card(next_deck, next_remaining, cards[0]);
        remove_card(next_deck, next_remaining, cards[1]);

        std::array<Card, kMaxMixedKnownCards> next_known_cards = known_cards;
        std::size_t next_known_count = known_count;
        append_combo_cards(candidate, next_known_cards, next_known_count);

        total_states = detail::saturating_add_exact_states(total_states,
                                                           count_mixed_exact_states(hero,
                                                                                   flexible_opponents,
                                                                                   seat_index + 1U,
                                                                                   next_deck,
                                                                                   next_remaining,
                                                                                   next_known_cards,
                                                                                   next_known_count));
        if (total_states >= detail::kExactStatesOverLimit) {
            return detail::kExactStatesOverLimit;
        }
    }

    return total_states;
}

std::vector<HandCombo> legal_combos_for_opponent(const Opponent& opponent,
                                                 const std::array<Card, detail::kDeckSize>& deck,
                                                 std::size_t remaining,
                                                 const std::array<Card, kMaxMixedKnownCards>& known_cards,
                                                 std::size_t known_count) {
    std::vector<HandCombo> candidates{};

    if (const HandRange* range = std::get_if<HandRange>(&opponent)) {
        candidates.reserve(range->size());
        for (const HandCombo& combo : *range) {
            if (!combo_uses_known_card(combo, known_cards, known_count)) {
                candidates.push_back(combo);
            }
        }
        return candidates;
    }

    if (std::holds_alternative<RandomOpponent>(opponent)) {
        const std::size_t pair_count = remaining * (remaining - 1U) / 2U;
        candidates.reserve(pair_count);
        for (std::size_t first = 0U; first + 1U < remaining; ++first) {
            for (std::size_t second = first + 1U; second < remaining; ++second) {
                candidates.emplace_back(deck[first], deck[second]);
            }
        }
        return candidates;
    }

    return candidates;
}

LeafOutcome evaluate_showdown_leaf(const HoldemHand& hero,
                                  const std::vector<std::array<Card, 2>>& villains,
                                  const std::array<Card, 5U>& board) {
    std::array<Card, 7U> hero_cards{};
    hero_cards[0] = hero.hole[0];
    hero_cards[1] = hero.hole[1];
    for (std::size_t index = 0U; index < 5U; ++index) {
        hero_cards[2U + index] = board[index];
    }

    const HandValue hero_value = evaluate(hero_cards, hero_cards.size());
    HandValue best_value = hero_value;
    std::size_t winner_count = 1U;
    bool hero_is_best = true;

    std::array<Card, 7U> villain_cards{};
    for (const std::array<Card, 2>& villain_hole : villains) {
        villain_cards[0] = villain_hole[0];
        villain_cards[1] = villain_hole[1];
        for (std::size_t index = 0U; index < 5U; ++index) {
            villain_cards[2U + index] = board[index];
        }

        const HandValue villain_value = evaluate(villain_cards, villain_cards.size());
        if (villain_value > best_value) {
            best_value = villain_value;
            winner_count = 1U;
            hero_is_best = false;
        } else if (villain_value == best_value) {
            ++winner_count;
        }
    }

    LeafOutcome outcome{};
    if (hero_is_best) {
        if (winner_count == 1U) {
            outcome.win = true;
            outcome.equity_share = 1.0L;
        } else {
            outcome.tie = true;
            outcome.equity_share = 1.0L / static_cast<long double>(winner_count);
        }
    } else {
        outcome.loss = true;
    }

    return outcome;
}

void enumerate_board_runouts(std::array<Card, detail::kDeckSize>& deck,
                             std::size_t remaining,
                             std::size_t board_known,
                             std::size_t board_filled,
                             std::size_t missing_board,
                             std::size_t start_index,
                             const HoldemHand& hero,
                             const std::vector<std::array<Card, 2>>& villains,
                             std::array<Card, 5U>& board,
                             ShowdownAccumulator& accumulator) {
    if (missing_board == 0U) {
        const LeafOutcome outcome = evaluate_showdown_leaf(hero, villains, board);
        accumulator.wins += outcome.win ? 1U : 0U;
        accumulator.ties += outcome.tie ? 1U : 0U;
        accumulator.losses += outcome.loss ? 1U : 0U;
        accumulator.equity_sum += outcome.equity_share;
        ++accumulator.states;
        return;
    }

    for (std::size_t index = start_index; index + missing_board <= remaining; ++index) {
        const Card chosen = deck[index];
        std::swap(deck[index], deck[remaining - 1U]);
        --remaining;

        board[board_known + board_filled] = chosen;
        enumerate_board_runouts(deck,
                                remaining,
                                board_known,
                                board_filled + 1U,
                                missing_board - 1U,
                                index,
                                hero,
                                villains,
                                board,
                                accumulator);

        ++remaining;
        std::swap(deck[index], deck[remaining - 1U]);
    }
}

EquityResult finalize_showdown_accumulator(const ShowdownAccumulator& accumulator) {
    EquityResult result{};
    result.win_probability = static_cast<double>(accumulator.wins) / static_cast<double>(accumulator.states);
    result.tie_probability = static_cast<double>(accumulator.ties) / static_cast<double>(accumulator.states);
    result.loss_probability = static_cast<double>(accumulator.losses) / static_cast<double>(accumulator.states);
    result.equity = static_cast<double>(accumulator.equity_sum / static_cast<long double>(accumulator.states));
    result.evaluated_states = accumulator.states;
    return result;
}

EquityResult solve_board_exact_equity(const HoldemHand& hero,
                                      const std::vector<std::array<Card, 2>>& villains,
                                      std::array<Card, detail::kDeckSize> deck,
                                      std::size_t remaining,
                                      std::array<Card, 5U> board) {
    ShowdownAccumulator accumulator{};
    const std::size_t missing_board = 5U - hero.board_count;

    if (missing_board == 0U) {
        const LeafOutcome outcome = evaluate_showdown_leaf(hero, villains, board);
        accumulator.wins = outcome.win ? 1U : 0U;
        accumulator.ties = outcome.tie ? 1U : 0U;
        accumulator.losses = outcome.loss ? 1U : 0U;
        accumulator.equity_sum = outcome.equity_share;
        accumulator.states = 1U;
    } else {
        enumerate_board_runouts(deck,
                                remaining,
                                hero.board_count,
                                0U,
                                missing_board,
                                0U,
                                hero,
                                villains,
                                board,
                                accumulator);
    }

    return finalize_showdown_accumulator(accumulator);
}

std::optional<EquityResult> solve_mixed_exact_recursive(const HoldemHand& hero,
                                                        const std::vector<const Opponent*>& flexible_opponents,
                                                        std::size_t seat_index,
                                                        const std::array<Card, detail::kDeckSize>& deck,
                                                        std::size_t remaining,
                                                        const std::array<Card, kMaxMixedKnownCards>& known_cards,
                                                        std::size_t known_count,
                                                        const std::array<Card, 5U>& board,
                                                        const std::vector<std::array<Card, 2>>& villains) {
    if (seat_index == flexible_opponents.size()) {
        return solve_board_exact_equity(hero, villains, deck, remaining, board);
    }

    const Opponent& opponent = *flexible_opponents[seat_index];
    const std::vector<HandCombo> candidates = legal_combos_for_opponent(opponent, deck, remaining, known_cards, known_count);
    if (candidates.empty()) {
        return std::nullopt;
    }

    long double win_states = 0.0L;
    long double tie_states = 0.0L;
    long double loss_states = 0.0L;
    long double equity_states = 0.0L;
    std::uint64_t evaluated_states = 0U;
    std::size_t valid_children = 0U;

    for (const HandCombo& candidate : candidates) {
        const std::array<Card, 2> cards = candidate.cards();
        std::array<Card, detail::kDeckSize> next_deck = deck;
        std::size_t next_remaining = remaining;
        remove_card(next_deck, next_remaining, cards[0]);
        remove_card(next_deck, next_remaining, cards[1]);

        std::array<Card, kMaxMixedKnownCards> next_known_cards = known_cards;
        std::size_t next_known_count = known_count;
        append_combo_cards(candidate, next_known_cards, next_known_count);

        std::vector<std::array<Card, 2>> next_villains = villains;
        next_villains.push_back(cards);

        const std::optional<EquityResult> child = solve_mixed_exact_recursive(hero,
                                                                              flexible_opponents,
                                                                              seat_index + 1U,
                                                                              next_deck,
                                                                              next_remaining,
                                                                              next_known_cards,
                                                                              next_known_count,
                                                                              board,
                                                                              next_villains);
        if (child) {
            const long double child_states = static_cast<long double>(child->evaluated_states);
            win_states += static_cast<long double>(child->win_probability) * child_states;
            tie_states += static_cast<long double>(child->tie_probability) * child_states;
            loss_states += static_cast<long double>(child->loss_probability) * child_states;
            equity_states += static_cast<long double>(child->equity) * child_states;
            evaluated_states += child->evaluated_states;
            ++valid_children;
        }
    }

    if (valid_children == 0U) {
        return std::nullopt;
    }

    EquityResult result{};
    result.win_probability = static_cast<double>(win_states / static_cast<long double>(evaluated_states));
    result.tie_probability = static_cast<double>(tie_states / static_cast<long double>(evaluated_states));
    result.loss_probability = static_cast<double>(loss_states / static_cast<long double>(evaluated_states));
    result.equity = static_cast<double>(equity_states / static_cast<long double>(evaluated_states));
    result.evaluated_states = evaluated_states;
    return result;
}

bool has_legal_completion(const std::vector<const Opponent*>& flexible_opponents,
                          std::size_t seat_index,
                          const std::array<Card, detail::kDeckSize>& deck,
                          std::size_t remaining,
                          const std::array<Card, kMaxMixedKnownCards>& known_cards,
                          std::size_t known_count) {
    if (seat_index == flexible_opponents.size()) {
        return true;
    }

    const Opponent& opponent = *flexible_opponents[seat_index];
    const std::vector<HandCombo> candidates = legal_combos_for_opponent(opponent, deck, remaining, known_cards, known_count);
    if (candidates.empty()) {
        return false;
    }

    for (const HandCombo& candidate : candidates) {
        const std::array<Card, 2> cards = candidate.cards();
        std::array<Card, detail::kDeckSize> next_deck = deck;
        std::size_t next_remaining = remaining;
        remove_card(next_deck, next_remaining, cards[0]);
        remove_card(next_deck, next_remaining, cards[1]);

        std::array<Card, kMaxMixedKnownCards> next_known_cards = known_cards;
        std::size_t next_known_count = known_count;
        append_combo_cards(candidate, next_known_cards, next_known_count);

        if (has_legal_completion(flexible_opponents,
                                 seat_index + 1U,
                                 next_deck,
                                 next_remaining,
                                 next_known_cards,
                                 next_known_count)) {
            return true;
        }
    }

    return false;
}

EquityResult simulate_mixed_equity(const GameState& state,
                                   const std::vector<const Opponent*>& flexible_opponents,
                                   const std::array<Card, detail::kDeckSize>& base_deck,
                                   std::size_t base_remaining,
                                   const std::array<Card, 5U>& base_board,
                                   const std::array<Card, kMaxMixedKnownCards>& base_known_cards,
                                   std::size_t base_known_count,
                                   const std::vector<std::array<Card, 2>>& base_villains,
                                   std::uint64_t simulations,
                                   std::uint64_t seed) {
    detail::validate_simulations(simulations);

    if (!has_legal_completion(flexible_opponents, 0U, base_deck, base_remaining, base_known_cards, base_known_count)) {
        detail::throw_invalid("No legal opponent combinations remain after card removal");
    }

    std::mt19937_64 rng(seed);
    std::uint64_t win_count = 0U;
    std::uint64_t tie_count = 0U;
    std::uint64_t loss_count = 0U;
    long double equity_sum = 0.0L;

    std::array<Card, 5U> board = base_board;
    std::vector<std::array<Card, 2>> villains = base_villains;
    villains.reserve(base_villains.size() + flexible_opponents.size());

    for (std::uint64_t simulation = 0U; simulation < simulations; ++simulation) {
        std::array<Card, detail::kDeckSize> deck = base_deck;
        std::size_t remaining = base_remaining;
        std::array<Card, kMaxMixedKnownCards> known_cards = base_known_cards;
        std::size_t known_count = base_known_count;
        villains = base_villains;
        board = base_board;

        bool rejected = false;
        for (const Opponent* opponent : flexible_opponents) {
            if (std::holds_alternative<RandomOpponent>(*opponent)) {
                if (remaining < 2U) {
                    rejected = true;
                    break;
                }

                std::uniform_int_distribution<std::size_t> first_distribution(0U, remaining - 1U);
                const std::size_t first_index = first_distribution(rng);
                const Card first = deck[first_index];
                std::swap(deck[first_index], deck[remaining - 1U]);
                --remaining;

                std::uniform_int_distribution<std::size_t> second_distribution(0U, remaining - 1U);
                const std::size_t second_index = second_distribution(rng);
                const Card second = deck[second_index];
                std::swap(deck[second_index], deck[remaining - 1U]);
                --remaining;

                known_cards[known_count++] = first;
                known_cards[known_count++] = second;
                villains.push_back(std::array<Card, 2>{first, second});
                continue;
            }

            const std::vector<HandCombo> candidates = legal_combos_for_opponent(*opponent, deck, remaining, known_cards, known_count);
            if (candidates.empty()) {
                rejected = true;
                break;
            }

            std::uniform_int_distribution<std::size_t> distribution(0U, candidates.size() - 1U);
            const HandCombo chosen = candidates[distribution(rng)];
            const std::array<Card, 2> cards = chosen.cards();
            remove_card(deck, remaining, cards[0]);
            remove_card(deck, remaining, cards[1]);
            known_cards[known_count++] = cards[0];
            known_cards[known_count++] = cards[1];
            villains.push_back(cards);
        }

        if (rejected) {
            continue;
        }

        for (std::size_t index = state.hero.board_count; index < 5U; ++index) {
            std::uniform_int_distribution<std::size_t> distribution(0U, remaining - 1U);
            const std::size_t chosen_index = distribution(rng);
            board[index] = deck[chosen_index];
            std::swap(deck[chosen_index], deck[remaining - 1U]);
            --remaining;
        }

        const LeafOutcome outcome = evaluate_showdown_leaf(state.hero, villains, board);
        win_count += outcome.win ? 1U : 0U;
        tie_count += outcome.tie ? 1U : 0U;
        loss_count += outcome.loss ? 1U : 0U;
        equity_sum += outcome.equity_share;
    }

    EquityResult result{};
    result.simulations = simulations;
    result.win_probability = static_cast<double>(win_count) / static_cast<double>(simulations);
    result.tie_probability = static_cast<double>(tie_count) / static_cast<double>(simulations);
    result.loss_probability = static_cast<double>(loss_count) / static_cast<double>(simulations);
    result.equity = static_cast<double>(equity_sum / static_cast<long double>(simulations));
    result.evaluated_states = simulations;
    return result;
}

EquityResult solve_mixed_game_state(const GameState& state, const EquityOptions& options) {
    std::size_t known_count = 0U;
    const std::array<Card, kMaxMixedKnownCards> known_cards = collect_initial_known_cards(state, known_count);

    std::array<Card, detail::kDeckSize> deck = make_full_deck();
    std::size_t remaining = detail::kDeckSize;
    for (std::size_t index = 0U; index < known_count; ++index) {
        remove_card(deck, remaining, known_cards[index]);
    }

    std::array<Card, 5U> board{};
    for (std::size_t index = 0U; index < state.hero.board_count; ++index) {
        board[index] = state.hero.board[index];
    }

    const std::vector<const Opponent*> flexible_opponents = collect_flexible_opponents(state);
    const std::vector<std::array<Card, 2>> fixed_villains = collect_fixed_villains(state);
    for (const Opponent* opponent : flexible_opponents) {
        if (std::holds_alternative<HandRange>(*opponent) && legal_combos_for_opponent(*opponent, deck, remaining, known_cards, known_count).empty()) {
            detail::throw_invalid("Opponent range must not be empty after card removal");
        }
    }

    switch (options.method) {
        case EquityMethod::exact: {
            const std::uint64_t theoretical_states = theoretical_exact_states(state);
            if (!detail::exact_equity_allowed(theoretical_states)) {
                detail::throw_exact_equity_limit(theoretical_states);
            }

            const std::optional<EquityResult> result = solve_mixed_exact_recursive(state.hero,
                                                                                   flexible_opponents,
                                                                                   0U,
                                                                                   deck,
                                                                                   remaining,
                                                                                   known_cards,
                                                                                   known_count,
                                                                                   board,
                                                                                fixed_villains);
            if (!result) {
                detail::throw_invalid("No legal opponent combinations remain after card removal");
            }
            return *result;
        }
        case EquityMethod::monte_carlo:
            return simulate_mixed_equity(state,
                                         flexible_opponents,
                                         deck,
                                         remaining,
                                         board,
                                         known_cards,
                                         known_count,
                                         fixed_villains,
                                         options.simulations,
                                         options.seed.value_or(0U));
    }

    detail::throw_invalid("Unknown equity calculation method");
    return {};
}

}  // namespace

void ActionOrderState::reset(Street new_street) {
    street = new_street;
}

void ActionOrderState::set_status(std::size_t seat, PlayerStatus new_status) {
    if (seat >= seats.size()) {
        detail::throw_invalid("Seat out of range");
    }
    seats[seat] = new_status;
}

PlayerStatus ActionOrderState::status(std::size_t seat) const {
    if (seat >= seats.size()) {
        detail::throw_invalid("Seat out of range");
    }
    return seats[seat];
}

bool ActionOrderState::is_active(std::size_t seat) const {
    return status(seat) == PlayerStatus::active;
}

std::size_t ActionOrderState::active_player_count() const {
    std::size_t count = 0U;
    for (PlayerStatus seat_status : seats) {
        if (seat_status == PlayerStatus::active) {
            ++count;
        }
    }
    return count;
}

bool ActionOrderState::hand_over() const {
    return active_player_count() <= 1U;
}

std::optional<std::size_t> ActionOrderState::first_to_act() const {
    if (hand_over() || seats.empty()) {
        return std::nullopt;
    }

    const std::size_t player_count = seats.size();
    std::size_t start = 0U;
    if (street == Street::preflop) {
        if (player_count == 2U) {
            start = button_seat;
        } else {
            start = (button_seat + 3U) % player_count;
        }
    } else {
        start = (button_seat + 1U) % player_count;
    }

    return next_eligible_seat(seats, start, true);
}

std::optional<std::size_t> ActionOrderState::next_to_act(std::size_t from_seat) const {
    if (hand_over() || seats.empty()) {
        return std::nullopt;
    }
    if (from_seat >= seats.size()) {
        detail::throw_invalid("Seat out of range");
    }
    return next_eligible_seat(seats, from_seat, false);
}

std::uint64_t theoretical_exact_states(const GameState& state) {
    validate_game_state(state);

    const std::size_t random_count = random_opponent_count(state);
    const std::size_t known_count = known_opponent_count(state);

    if (known_count == 0U) {
        return detail::theoretical_exact_states(state.hero.board_count, random_count);
    }

    if (state.opponents.size() == 1U) {
        const Opponent& opponent = state.opponents.front();
        if (std::holds_alternative<HandCombo>(opponent)) {
            return detail::theoretical_board_runout_states(state.hero.board_count);
        }

        if (const HandRange* range = std::get_if<HandRange>(&opponent)) {
            return theoretical_exact_states(state.hero, *range);
        }

        return detail::theoretical_exact_states(state.hero.board_count, 1U);
    }

    std::size_t known_count_local = 0U;
    const std::array<Card, kMaxMixedKnownCards> known_cards_local = collect_initial_known_cards(state, known_count_local);

    std::array<Card, detail::kDeckSize> deck = make_full_deck();
    std::size_t remaining = detail::kDeckSize;
    for (std::size_t index = 0U; index < known_count_local; ++index) {
        remove_card(deck, remaining, known_cards_local[index]);
    }

    const std::vector<const Opponent*> flexible_opponents = collect_flexible_opponents(state);
    return count_mixed_exact_states(state.hero,
                                    flexible_opponents,
                                    0U,
                                    deck,
                                    remaining,
                                    known_cards_local,
                                    known_count_local);
}

std::size_t board_card_count(Street street) {
    return board_count_for_street(street);
}

Street street_from_board_count(std::size_t board_count) {
    switch (board_count) {
        case 0U: return Street::preflop;
        case 3U: return Street::flop;
        case 4U: return Street::turn;
        case 5U: return Street::river;
        default: detail::throw_invalid("Board must contain 0, 3, 4, or 5 cards");
    }

    return Street::preflop;
}

void validate_game_state(const GameState& state) {
    detail::validate_hand(state.hero);
    validate_betting_state(state.betting);

    if (state.street != street_from_board_count(state.hero.board_count)) {
        detail::throw_invalid("Street and board card count do not match");
    }

    if (state.player_count < 2U) {
        detail::throw_invalid("Player count must be at least 2");
    }

    if (state.player_count > 6U) {
        detail::throw_invalid("Player count cannot exceed 6");
    }

    if (state.opponents.size() != state.player_count - 1U) {
        detail::throw_invalid("Player count must match the number of opponents plus hero");
    }

    for (const Opponent& opponent : state.opponents) {
        if (const HandCombo* combo = std::get_if<HandCombo>(&opponent)) {
            if (!combo->valid()) {
                detail::throw_invalid("Opponent hand must contain exactly 2 valid cards");
            }
        } else if (const HandRange* range = std::get_if<HandRange>(&opponent)) {
            if (range->empty()) {
                detail::throw_invalid("Opponent range must not be empty");
            }
        }
    }

    validate_known_cards(collect_known_cards(state));
}

void validate_action_order_state(const ActionOrderState& state) {
    if (state.player_count < 2U) {
        detail::throw_invalid("Player count must be at least 2");
    }

    if (state.player_count > 6U) {
        detail::throw_invalid("Player count cannot exceed 6");
    }

    if (state.seats.size() != state.player_count) {
        detail::throw_invalid("Seat count must match player count");
    }

    if (state.button_seat >= state.player_count) {
        detail::throw_invalid("Button seat must be in range");
    }

    if (state.hand_over()) {
        return;
    }

    if (!state.first_to_act().has_value()) {
        detail::throw_invalid("A non-terminal hand must have a first actor");
    }
}

EquityResult calculate_equity(const GameState& state, const EquityOptions& options) {
    validate_game_state(state);

    const std::size_t random_count = random_opponent_count(state);
    const std::size_t known_count = known_opponent_count(state);

    if (known_count == 0U) {
        return calculate_equity(state.hero, random_count, options);
    }

    if (state.opponents.size() == 1U) {
        const Opponent& opponent = state.opponents.front();
        if (const HandCombo* combo = std::get_if<HandCombo>(&opponent)) {
            return calculate_equity(state.hero, make_singleton_range(*combo), options);
        }

        if (const HandRange* range = std::get_if<HandRange>(&opponent)) {
            return calculate_equity(state.hero, *range, options);
        }

        return calculate_equity(state.hero, 1U, options);
    }

    return solve_mixed_game_state(state, options);
}

}  // namespace poker