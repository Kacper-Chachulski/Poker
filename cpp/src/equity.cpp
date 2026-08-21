#include "poker/equity.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <random>
#include <vector>

#include "poker/evaluator.hpp"
#include "equity_common.hpp"
#include "poker/exact_equity.hpp"
#include "poker/range.hpp"

namespace poker {

namespace {

std::array<Card, detail::kDeckSize> make_full_deck() noexcept {
    std::array<Card, detail::kDeckSize> deck{};
    for (std::uint8_t index = 0; index < detail::kDeckSize; ++index) {
        deck[index] = Card::from_index(index);
    }
    return deck;
}

std::size_t collect_known_cards(const HoldemHand& hero, std::array<Card, detail::kMaxKnownCards>& known_cards) {
    std::size_t count = 0U;
    known_cards[count++] = hero.hole[0];
    known_cards[count++] = hero.hole[1];
    for (std::size_t index = 0; index < hero.board_count; ++index) {
        known_cards[count++] = hero.board[index];
    }

    for (std::size_t left = 0; left < count; ++left) {
        for (std::size_t right = left + 1U; right < count; ++right) {
            if (known_cards[right].index() > known_cards[left].index()) {
                std::swap(known_cards[left], known_cards[right]);
            }
        }
    }

    return count;
}

Card draw_card(std::array<Card, detail::kDeckSize>& deck, std::size_t& remaining, std::mt19937_64& rng) {
    std::uniform_int_distribution<std::size_t> distribution(0U, remaining - 1U);
    const std::size_t index = distribution(rng);
    const Card drawn = deck[index];
    deck[index] = deck[remaining - 1U];
    --remaining;
    return drawn;
}

struct RangeCounts {
    std::uint64_t wins{0U};
    std::uint64_t ties{0U};
    std::uint64_t losses{0U};
    long double equity_sum{0.0L};
    std::uint64_t states{0U};
};

bool combo_uses_known_card(const HandCombo& combo,
                           const std::array<Card, detail::kMaxKnownCards>& known_cards,
                           std::size_t known_count);

void remove_card(std::array<Card, detail::kDeckSize>& deck, std::size_t& remaining, Card card);

std::size_t collect_known_cards(const std::vector<Card>& board, std::array<Card, detail::kMaxKnownCards>& known_cards) {
    if (!detail::is_valid_board_size(board.size())) {
        detail::throw_invalid("Board must contain 0, 3, 4, or 5 cards");
    }

    std::size_t count = 0U;
    for (Card card : board) {
        if (!card.valid()) {
            detail::throw_invalid("Board contains an invalid card");
        }
        known_cards[count++] = card;
    }

    for (std::size_t left = 0U; left < count; ++left) {
        for (std::size_t right = left + 1U; right < count; ++right) {
            if (known_cards[left] == known_cards[right]) {
                detail::throw_invalid("Duplicate cards are not allowed");
            }
        }
    }

    return count;
}

void append_combo_cards(const HandCombo& combo,
                        std::array<Card, detail::kMaxKnownCards>& known_cards,
                        std::size_t& known_count) {
    const std::array<Card, 2> cards = combo.cards();
    known_cards[known_count++] = cards[0];
    known_cards[known_count++] = cards[1];
}

std::vector<HandCombo> filter_legal_combos(const HandRange& range,
                                          const std::array<Card, detail::kMaxKnownCards>& known_cards,
                                          std::size_t known_count) {
    std::vector<HandCombo> legal_combos{};
    legal_combos.reserve(range.size());
    for (const HandCombo& combo : range) {
        if (!combo_uses_known_card(combo, known_cards, known_count)) {
            legal_combos.push_back(combo);
        }
    }

    return legal_combos;
}

struct RangeMatchup {
    HandCombo hero;
    std::vector<HandCombo> villains;
};

std::vector<RangeMatchup> build_range_matchups(const HandRange& hero_range,
                                               const HandRange& villain_range,
                                               const std::vector<Card>& board) {
    std::array<Card, detail::kMaxKnownCards> known_cards{};
    const std::size_t board_count = collect_known_cards(board, known_cards);

    const std::vector<HandCombo> legal_heroes = filter_legal_combos(hero_range, known_cards, board_count);
    if (legal_heroes.empty()) {
        detail::throw_invalid("Hero range has no legal combos after card removal");
    }

    std::vector<RangeMatchup> matchups{};
    matchups.reserve(legal_heroes.size());

    for (const HandCombo& hero_combo : legal_heroes) {
        std::array<Card, detail::kMaxKnownCards> hero_known_cards = known_cards;
        std::size_t hero_known_count = board_count;
        append_combo_cards(hero_combo, hero_known_cards, hero_known_count);

        std::vector<HandCombo> legal_villains = filter_legal_combos(villain_range, hero_known_cards, hero_known_count);
        if (!legal_villains.empty()) {
            matchups.push_back(RangeMatchup{hero_combo, std::move(legal_villains)});
        }
    }

    if (matchups.empty()) {
        detail::throw_invalid("No legal hero-villain combinations remain after card removal");
    }

    return matchups;
}

void evaluate_heads_up_leaf(const std::array<Card, 2>& hero_hole,
                            const std::array<Card, 2>& villain_hole,
                            const std::array<Card, 5>& board,
                            RangeCounts& counts) {
    std::array<Card, 7> hero_cards{};
    std::array<Card, 7> villain_cards{};

    hero_cards[0] = hero_hole[0];
    hero_cards[1] = hero_hole[1];
    villain_cards[0] = villain_hole[0];
    villain_cards[1] = villain_hole[1];

    for (std::size_t index = 0U; index < 5U; ++index) {
        hero_cards[2U + index] = board[index];
        villain_cards[2U + index] = board[index];
    }

    const HandValue hero_value = evaluate(hero_cards, hero_cards.size());
    const HandValue villain_value = evaluate(villain_cards, villain_cards.size());

    if (hero_value > villain_value) {
        ++counts.wins;
        counts.equity_sum += 1.0L;
    } else if (villain_value > hero_value) {
        ++counts.losses;
    } else {
        ++counts.ties;
        counts.equity_sum += 0.5L;
    }

    ++counts.states;
}

void enumerate_board_runouts(std::array<Card, detail::kDeckSize>& deck,
                             std::size_t deck_count,
                             std::size_t board_known,
                             std::size_t board_filled,
                             std::size_t missing_board,
                             std::size_t start_index,
                             const std::array<Card, 2>& hero_hole,
                             const std::array<Card, 2>& villain_hole,
                             std::array<Card, 5>& board,
                             RangeCounts& counts) {
    if (missing_board == 0U) {
        evaluate_heads_up_leaf(hero_hole, villain_hole, board, counts);
        return;
    }

    for (std::size_t index = start_index; index + missing_board <= deck_count; ++index) {
        const Card chosen = deck[index];
        std::swap(deck[index], deck[deck_count - 1U]);
        --deck_count;

        board[board_known + board_filled] = chosen;
        enumerate_board_runouts(deck,
                                deck_count,
                                board_known,
                                board_filled + 1U,
                                missing_board - 1U,
                                index,
                                hero_hole,
                                villain_hole,
                                board,
                                counts);

        ++deck_count;
        std::swap(deck[index], deck[deck_count - 1U]);
    }
}

EquityResult solve_range_vs_range_exact_equity(const HandRange& hero_range,
                                               const HandRange& villain_range,
                                               const std::vector<Card>& board) {
    const std::vector<RangeMatchup> matchups = build_range_matchups(hero_range, villain_range, board);

    std::array<Card, detail::kDeckSize> base_deck = make_full_deck();
    std::size_t base_count = detail::kDeckSize;

    std::array<Card, 5U> board_cards{};
    for (std::size_t index = 0U; index < board.size(); ++index) {
        board_cards[index] = board[index];
        remove_card(base_deck, base_count, board[index]);
    }

    std::uint64_t evaluated_states = 0U;
    std::uint64_t hero_count = 0U;
    long double win_probability_sum = 0.0L;
    long double tie_probability_sum = 0.0L;
    long double loss_probability_sum = 0.0L;
    long double equity_sum = 0.0L;

    const std::size_t missing_board = 5U - board.size();

    for (const RangeMatchup& matchup : matchups) {
        std::array<Card, detail::kDeckSize> hero_deck = base_deck;
        std::size_t hero_deck_count = base_count;
        const std::array<Card, 2> hero_hole = matchup.hero.cards();
        remove_card(hero_deck, hero_deck_count, hero_hole[0]);
        remove_card(hero_deck, hero_deck_count, hero_hole[1]);

        RangeCounts hero_counts{};
        for (const HandCombo& villain_combo : matchup.villains) {
            std::array<Card, detail::kDeckSize> deck = hero_deck;
            std::size_t deck_count = hero_deck_count;
            const std::array<Card, 2> villain_hole = villain_combo.cards();
            remove_card(deck, deck_count, villain_hole[0]);
            remove_card(deck, deck_count, villain_hole[1]);

            if (missing_board == 0U) {
                evaluate_heads_up_leaf(hero_hole, villain_hole, board_cards, hero_counts);
            } else {
                enumerate_board_runouts(deck,
                                        deck_count,
                                        board.size(),
                                        0U,
                                        missing_board,
                                        0U,
                                        hero_hole,
                                        villain_hole,
                                        board_cards,
                                        hero_counts);
            }
        }

        if (hero_counts.states == 0U) {
            continue;
        }

        win_probability_sum += static_cast<long double>(hero_counts.wins) / static_cast<long double>(hero_counts.states);
        tie_probability_sum += static_cast<long double>(hero_counts.ties) / static_cast<long double>(hero_counts.states);
        loss_probability_sum += static_cast<long double>(hero_counts.losses) / static_cast<long double>(hero_counts.states);
        equity_sum += hero_counts.equity_sum / static_cast<long double>(hero_counts.states);
        evaluated_states += hero_counts.states;
        ++hero_count;
    }

    if (hero_count == 0U) {
        detail::throw_invalid("No legal hero-villain combinations remain after card removal");
    }

    EquityResult result{};
    result.win_probability = static_cast<double>(win_probability_sum / static_cast<long double>(hero_count));
    result.tie_probability = static_cast<double>(tie_probability_sum / static_cast<long double>(hero_count));
    result.loss_probability = static_cast<double>(loss_probability_sum / static_cast<long double>(hero_count));
    result.equity = static_cast<double>(equity_sum / static_cast<long double>(hero_count));
    result.evaluated_states = evaluated_states;
    return result;
}

EquityResult simulate_range_vs_range_equity(const HandRange& hero_range,
                                            const HandRange& villain_range,
                                            const std::vector<Card>& board,
                                            std::size_t simulations,
                                            std::uint64_t seed) {
    detail::validate_simulations(simulations);

    const std::vector<RangeMatchup> matchups = build_range_matchups(hero_range, villain_range, board);

    std::array<Card, detail::kDeckSize> base_deck = make_full_deck();
    std::size_t base_count = detail::kDeckSize;
    for (Card card : board) {
        remove_card(base_deck, base_count, card);
    }

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::size_t> hero_distribution(0U, matchups.size() - 1U);

    std::uint64_t win_count = 0U;
    std::uint64_t tie_count = 0U;
    std::uint64_t loss_count = 0U;
    long double equity_sum = 0.0L;

    std::array<Card, 5U> board_cards{};
    for (std::size_t index = 0U; index < board.size(); ++index) {
        board_cards[index] = board[index];
    }

    std::array<Card, 7U> hero_cards{};
    std::array<Card, 7U> villain_cards{};

    for (std::size_t simulation = 0U; simulation < simulations; ++simulation) {
        const RangeMatchup& matchup = matchups[hero_distribution(rng)];
        std::uniform_int_distribution<std::size_t> villain_distribution(0U, matchup.villains.size() - 1U);
        const HandCombo& villain_combo = matchup.villains[villain_distribution(rng)];

        std::array<Card, detail::kDeckSize> deck = base_deck;
        std::size_t remaining = base_count;

        const std::array<Card, 2> hero_hole = matchup.hero.cards();
        const std::array<Card, 2> villain_hole = villain_combo.cards();
        remove_card(deck, remaining, hero_hole[0]);
        remove_card(deck, remaining, hero_hole[1]);
        remove_card(deck, remaining, villain_hole[0]);
        remove_card(deck, remaining, villain_hole[1]);

        for (std::size_t index = 0U; index < board.size(); ++index) {
            board_cards[index] = board[index];
        }
        for (std::size_t index = board.size(); index < 5U; ++index) {
            board_cards[index] = draw_card(deck, remaining, rng);
        }

        hero_cards[0] = hero_hole[0];
        hero_cards[1] = hero_hole[1];
        villain_cards[0] = villain_hole[0];
        villain_cards[1] = villain_hole[1];
        for (std::size_t index = 0U; index < 5U; ++index) {
            hero_cards[2U + index] = board_cards[index];
            villain_cards[2U + index] = board_cards[index];
        }

        const HandValue hero_value = evaluate(hero_cards, hero_cards.size());
        const HandValue villain_value = evaluate(villain_cards, villain_cards.size());

        if (hero_value > villain_value) {
            ++win_count;
            equity_sum += 1.0L;
        } else if (villain_value > hero_value) {
            ++loss_count;
        } else {
            ++tie_count;
            equity_sum += 0.5L;
        }
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

bool combo_uses_known_card(const HandCombo& combo,
                           const std::array<Card, detail::kMaxKnownCards>& known_cards,
                           std::size_t known_count) {
    const std::array<Card, 2> cards = combo.cards();
    for (std::size_t index = 0U; index < known_count; ++index) {
        if (cards[0] == known_cards[index] || cards[1] == known_cards[index]) {
            return true;
        }
    }

    return false;
}

std::vector<HandCombo> filter_legal_villain_combos(const HoldemHand& hero, const HandRange& villain_range) {
    std::array<Card, detail::kMaxKnownCards> known_cards{};
    const std::size_t known_count = collect_known_cards(hero, known_cards);

    std::vector<HandCombo> legal_combos{};
    legal_combos.reserve(villain_range.size());
    for (const HandCombo& combo : villain_range) {
        if (!combo_uses_known_card(combo, known_cards, known_count)) {
            legal_combos.push_back(combo);
        }
    }

    if (legal_combos.empty()) {
        detail::throw_invalid("Villain range has no legal combos after card removal");
    }

    return legal_combos;
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

void evaluate_range_leaf(const HoldemHand& hero,
                         const HandCombo& villain_combo,
                         const std::array<Card, 5>& board,
                         RangeCounts& counts) {
    std::array<Card, 7> hero_cards{};
    std::array<Card, 7> villain_cards{};

    hero_cards[0] = hero.hole[0];
    hero_cards[1] = hero.hole[1];
    villain_cards[0] = villain_combo.cards()[0];
    villain_cards[1] = villain_combo.cards()[1];

    for (std::size_t index = 0U; index < 5U; ++index) {
        hero_cards[2U + index] = board[index];
        villain_cards[2U + index] = board[index];
    }

    const HandValue hero_value = evaluate(hero_cards, hero_cards.size());
    const HandValue villain_value = evaluate(villain_cards, villain_cards.size());

    if (hero_value > villain_value) {
        ++counts.wins;
        counts.equity_sum += 1.0L;
    } else if (villain_value > hero_value) {
        ++counts.losses;
    } else {
        ++counts.ties;
        counts.equity_sum += 0.5L;
    }

    ++counts.states;
}

void enumerate_range_board(std::array<Card, detail::kDeckSize>& deck,
                           std::size_t deck_count,
                           std::size_t board_known,
                           std::size_t board_filled,
                           std::size_t missing_board,
                           std::size_t start_index,
                           const HoldemHand& hero,
                           const HandCombo& villain_combo,
                           std::array<Card, 5>& board,
                           RangeCounts& counts) {
    if (missing_board == 0U) {
        evaluate_range_leaf(hero, villain_combo, board, counts);
        return;
    }

    for (std::size_t index = start_index; index + missing_board <= deck_count; ++index) {
        const Card chosen = deck[index];
        std::swap(deck[index], deck[deck_count - 1U]);
        --deck_count;

        board[board_known + board_filled] = chosen;
        enumerate_range_board(deck,
                              deck_count,
                              board_known,
                              board_filled + 1U,
                              missing_board - 1U,
                              index,
                              hero,
                              villain_combo,
                              board,
                              counts);

        ++deck_count;
        std::swap(deck[index], deck[deck_count - 1U]);
    }
}

EquityResult solve_range_exact_equity(const HoldemHand& hero, const HandRange& villain_range) {
    detail::validate_hand(hero);

    const std::vector<HandCombo> legal_combos = filter_legal_villain_combos(hero, villain_range);

    std::array<Card, detail::kDeckSize> base_deck = make_full_deck();
    std::size_t base_count = detail::kDeckSize;
    remove_card(base_deck, base_count, hero.hole[0]);
    remove_card(base_deck, base_count, hero.hole[1]);

    std::array<Card, 5U> board{};
    for (std::size_t index = 0U; index < hero.board_count; ++index) {
        board[index] = hero.board[index];
        remove_card(base_deck, base_count, hero.board[index]);
    }

    RangeCounts counts{};
    const std::size_t missing_board = 5U - hero.board_count;

    for (const HandCombo& villain_combo : legal_combos) {
        std::array<Card, detail::kDeckSize> deck = base_deck;
        std::size_t deck_count = base_count;
        const std::array<Card, 2> villain_cards = villain_combo.cards();
        remove_card(deck, deck_count, villain_cards[0]);
        remove_card(deck, deck_count, villain_cards[1]);

        if (missing_board == 0U) {
            evaluate_range_leaf(hero, villain_combo, board, counts);
        } else {
            enumerate_range_board(deck,
                                  deck_count,
                                  hero.board_count,
                                  0U,
                                  missing_board,
                                  0U,
                                  hero,
                                  villain_combo,
                                  board,
                                  counts);
        }
    }

    EquityResult result{};
    result.win_probability = static_cast<double>(counts.wins) / static_cast<double>(counts.states);
    result.tie_probability = static_cast<double>(counts.ties) / static_cast<double>(counts.states);
    result.loss_probability = static_cast<double>(counts.losses) / static_cast<double>(counts.states);
    result.equity = static_cast<double>(counts.equity_sum / static_cast<long double>(counts.states));
    result.evaluated_states = counts.states;
    return result;
}

EquityResult simulate_range_equity(const HoldemHand& hero,
                                   const HandRange& villain_range,
                                   std::size_t simulations,
                                   std::uint64_t seed) {
    detail::validate_hand(hero);
    detail::validate_simulations(simulations);

    const std::vector<HandCombo> legal_combos = filter_legal_villain_combos(hero, villain_range);

    std::array<Card, detail::kDeckSize> base_deck = make_full_deck();
    std::size_t base_count = detail::kDeckSize;
    remove_card(base_deck, base_count, hero.hole[0]);
    remove_card(base_deck, base_count, hero.hole[1]);
    for (std::size_t index = 0U; index < hero.board_count; ++index) {
        remove_card(base_deck, base_count, hero.board[index]);
    }

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::size_t> distribution(0U, legal_combos.size() - 1U);

    std::uint64_t win_count = 0U;
    std::uint64_t tie_count = 0U;
    std::uint64_t loss_count = 0U;
    long double equity_sum = 0.0L;

    std::array<Card, 5U> board{};
    std::array<Card, 7U> hero_cards{};
    std::array<Card, 7U> villain_cards{};

    for (std::size_t simulation = 0U; simulation < simulations; ++simulation) {
        std::array<Card, detail::kDeckSize> deck = base_deck;
        std::size_t remaining = base_count;

        const HandCombo& villain_combo = legal_combos[distribution(rng)];
        const std::array<Card, 2> villain = villain_combo.cards();
        remove_card(deck, remaining, villain[0]);
        remove_card(deck, remaining, villain[1]);

        for (std::size_t index = 0U; index < hero.board_count; ++index) {
            board[index] = hero.board[index];
        }
        for (std::size_t index = hero.board_count; index < 5U; ++index) {
            board[index] = draw_card(deck, remaining, rng);
        }

        hero_cards[0] = hero.hole[0];
        hero_cards[1] = hero.hole[1];
        villain_cards[0] = villain[0];
        villain_cards[1] = villain[1];
        for (std::size_t index = 0U; index < 5U; ++index) {
            hero_cards[2U + index] = board[index];
            villain_cards[2U + index] = board[index];
        }

        const HandValue hero_value = evaluate(hero_cards, hero_cards.size());
        const HandValue villain_value = evaluate(villain_cards, villain_cards.size());

        if (hero_value > villain_value) {
            ++win_count;
            equity_sum += 1.0L;
        } else if (villain_value > hero_value) {
            ++loss_count;
        } else {
            ++tie_count;
            equity_sum += 0.5L;
        }
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

}  // namespace

EquityResult calculate_equity(const HoldemHand& hero, std::size_t opponents, const EquityOptions& options) {
    detail::validate_equity_request(hero, opponents);

    switch (options.method) {
        case EquityMethod::exact:
            return solve_exact_equity(hero, opponents);
        case EquityMethod::monte_carlo:
            detail::validate_simulations(options.simulations);
            return simulate_equity(hero,
                                   opponents,
                                   static_cast<std::size_t>(options.simulations),
                                   options.seed.value_or(0U));
    }

    detail::throw_invalid("Unknown equity calculation method");
    return {};
}

EquityResult calculate_equity(const HoldemHand& hero,
                              const HandRange& villain_range,
                              const EquityOptions& options) {
    detail::validate_hand(hero);

    switch (options.method) {
        case EquityMethod::exact:
            return solve_range_exact_equity(hero, villain_range);
        case EquityMethod::monte_carlo:
            detail::validate_simulations(options.simulations);
            return simulate_range_equity(hero,
                                         villain_range,
                                         static_cast<std::size_t>(options.simulations),
                                         options.seed.value_or(0U));
    }

    detail::throw_invalid("Unknown equity calculation method");
    return {};
}

EquityResult calculate_equity(const HandRange& hero_range,
                              const HandRange& villain_range,
                              const std::vector<Card>& board,
                              const EquityOptions& options) {
    if (hero_range.empty()) {
        detail::throw_invalid("Hero range must not be empty");
    }

    if (villain_range.empty()) {
        detail::throw_invalid("Villain range must not be empty");
    }

    switch (options.method) {
        case EquityMethod::exact:
            return solve_range_vs_range_exact_equity(hero_range, villain_range, board);
        case EquityMethod::monte_carlo:
            detail::validate_simulations(options.simulations);
            return simulate_range_vs_range_equity(hero_range,
                                                  villain_range,
                                                  board,
                                                  static_cast<std::size_t>(options.simulations),
                                                  options.seed.value_or(0U));
    }

    detail::throw_invalid("Unknown equity calculation method");
    return {};
}

EquityResult simulate_equity(const HoldemHand& hero,
                             std::size_t opponents,
                             std::size_t simulations,
                             std::uint64_t seed) {
    detail::validate_equity_request(hero, opponents);
    detail::validate_simulations(simulations);

    std::array<Card, detail::kMaxKnownCards> known_cards{};
    const std::size_t known_count = collect_known_cards(hero, known_cards);
    const std::array<Card, detail::kDeckSize> full_deck = make_full_deck();
    std::mt19937_64 rng(seed);

    std::uint64_t win_count = 0U;
    std::uint64_t tie_count = 0U;
    std::uint64_t loss_count = 0U;
    double equity_sum = 0.0;

    std::array<Card, 5U> board{};
    std::array<Card, 7U> hero_cards{};
    std::array<Card, 7U> opponent_cards{};

    for (std::size_t simulation = 0; simulation < simulations; ++simulation) {
        std::array<Card, detail::kDeckSize> deck{};
        std::size_t remaining = 0U;

        for (std::size_t index = 0U; index < detail::kDeckSize; ++index) {
            const Card card = full_deck[index];

            bool is_known = false;
            for (std::size_t known_index = 0U; known_index < known_count; ++known_index) {
                if (card == known_cards[known_index]) {
                    is_known = true;
                    break;
                }
            }

            if (!is_known) {
                deck[remaining++] = card;
            }
        }

        for (std::size_t index = 0; index < hero.board_count; ++index) {
            board[index] = hero.board[index];
        }
        for (std::size_t index = hero.board_count; index < 5U; ++index) {
            board[index] = draw_card(deck, remaining, rng);
        }

        hero_cards[0] = hero.hole[0];
        hero_cards[1] = hero.hole[1];
        for (std::size_t index = 0; index < 5U; ++index) {
            hero_cards[2U + index] = board[index];
        }

        const HandValue hero_value = evaluate(hero_cards, hero_cards.size());

        HandValue best_value = hero_value;
        std::size_t winner_count = 1U;
        bool hero_is_best = true;

        for (std::size_t opponent = 0; opponent < opponents; ++opponent) {
            opponent_cards[0] = draw_card(deck, remaining, rng);
            opponent_cards[1] = draw_card(deck, remaining, rng);
            for (std::size_t index = 0; index < 5U; ++index) {
                opponent_cards[2U + index] = board[index];
            }

            const HandValue opponent_value = evaluate(opponent_cards, opponent_cards.size());
            if (opponent_value > best_value) {
                best_value = opponent_value;
                winner_count = 1U;
                hero_is_best = false;
            } else if (opponent_value == best_value) {
                ++winner_count;
            }
        }

        if (hero_is_best) {
            if (winner_count == 1U) {
                ++win_count;
                equity_sum += 1.0;
            } else {
                ++tie_count;
                equity_sum += 1.0 / static_cast<double>(winner_count);
            }
        } else {
            ++loss_count;
        }
    }

    EquityResult result{};
    result.simulations = simulations;
    result.win_probability = static_cast<double>(win_count) / static_cast<double>(simulations);
    result.tie_probability = static_cast<double>(tie_count) / static_cast<double>(simulations);
    result.loss_probability = static_cast<double>(loss_count) / static_cast<double>(simulations);
    result.equity = equity_sum / static_cast<double>(simulations);
    result.evaluated_states = simulations;
    return result;
}

}  // namespace poker