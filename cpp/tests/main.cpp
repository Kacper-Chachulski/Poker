#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

#include "poker/card.hpp"
#include "poker/deck.hpp"
#include "poker/evaluator.hpp"
#include "poker/hand.hpp"
#include "poker/equity.hpp"
#include "poker/range.hpp"

namespace {

int failures = 0;

void fail(const std::string& message) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void expect_eq(std::uint64_t actual, std::uint64_t expected, const std::string& message) {
    if (actual != expected) {
        fail(message + " expected=" + std::to_string(expected) + " actual=" + std::to_string(actual));
    }
}

void expect_close(double actual, double expected, double epsilon, const std::string& message) {
    if (std::fabs(actual - expected) > epsilon) {
        fail(message + " expected=" + std::to_string(expected) + " actual=" + std::to_string(actual));
    }
}

template <typename Fn>
void expect_no_throw(Fn&& fn, const std::string& message) {
    try {
        fn();
    } catch (const std::exception& error) {
        fail(message + ": " + error.what());
    }
}

template <typename Fn>
void expect_invalid_argument(Fn&& fn, const std::string& message) {
    try {
        fn();
        fail(message + ": expected invalid_argument");
    } catch (const std::invalid_argument&) {
    } catch (const std::exception& error) {
        fail(message + ": wrong exception: " + error.what());
    }
}

poker::Card card(std::string_view text) {
    const std::optional<poker::Card> parsed = poker::Card::from_string(text);
    if (!parsed) {
        throw std::runtime_error(std::string("invalid card literal: ") + std::string(text));
    }
    return *parsed;
}

poker::HoldemHand make_hand(std::string_view h1,
                            std::string_view h2,
                            std::initializer_list<std::string_view> board = {}) {
    poker::HoldemHand hand{};
    hand.hole[0] = card(h1);
    hand.hole[1] = card(h2);
    hand.board_count = static_cast<std::uint8_t>(board.size());
    std::size_t index = 0U;
    for (std::string_view text : board) {
        hand.board[index++] = card(text);
    }
    return hand;
}

void expect_category(const poker::HandValue value,
                      poker::HandCategory expected,
                      const std::string& message) {
    if (value.category() != expected) {
        fail(message);
    }
}

std::uint64_t binomial(std::uint64_t n, std::uint64_t k) {
    if (k > n) {
        return 0U;
    }
    if (k > n - k) {
        k = n - k;
    }

    std::uint64_t result = 1U;
    for (std::uint64_t index = 1U; index <= k; ++index) {
        result = (result * (n - k + index)) / index;
    }
    return result;
}

std::uint64_t theoretical_exact_states(std::size_t board_count, std::size_t opponents) {
    const std::size_t missing_board = 5U - board_count;
    std::uint64_t remaining = 52U - 2U - board_count;
    const std::uint64_t board_states = binomial(remaining, missing_board);
    remaining -= missing_board;

    std::uint64_t opponent_states = 1U;
    for (std::size_t opponent = 0; opponent < opponents; ++opponent) {
        opponent_states *= binomial(remaining, 2U);
        remaining -= 2U;
    }

    return board_states * opponent_states;
}

void test_cards() {
    std::array<bool, 52> seen{};
    for (std::uint8_t index = 0; index < 52; ++index) {
        const poker::Card current = poker::Card::from_index(index);
        expect_true(current.valid(), "card should be valid");
        expect_eq(current.index(), index, "card index should round-trip");
        expect_true(!seen[current.index()], "card indices should be unique");
        seen[current.index()] = true;
    }

    const poker::Card ace_spades = card("As");
    expect_true(ace_spades.to_string() == "As", "card string conversion should round-trip");
    expect_true(ace_spades == card("As"), "card equality should hold");
    expect_true(ace_spades != card("Ks"), "card inequality should hold");
    expect_true(!poker::Card::from_string("ZZ").has_value(), "invalid card should fail to parse");
}

void test_deck() {
    poker::Deck deck;
    expect_eq(deck.remaining(), 52U, "fresh deck should contain 52 cards");

    std::array<bool, 52> seen{};
    for (std::size_t index = 0; index < 52; ++index) {
        const poker::Card current = deck[index];
        expect_true(current.valid(), "deck card should be valid");
        expect_true(!seen[current.index()], "deck should contain unique cards");
        seen[current.index()] = true;
    }

    poker::Card drawn;
    expect_true(deck.draw(drawn), "draw should succeed on a full deck");
    expect_eq(deck.remaining(), 51U, "draw should reduce remaining cards");

    deck.reset();
    expect_eq(deck.remaining(), 52U, "reset should restore full deck");

    std::mt19937_64 generator{123456ULL};
    deck.shuffle(generator);
    expect_eq(deck.remaining(), 52U, "shuffle should preserve deck size");

    seen.fill(false);
    for (std::size_t index = 0; index < 52; ++index) {
        const poker::Card current = deck[index];
        expect_true(!seen[current.index()], "shuffled deck should still contain unique cards");
        seen[current.index()] = true;
    }
}

void test_holdem_representation() {
    const poker::HoldemHand hand = make_hand("As", "Ks", {"Qh", "7c", "2s"});
    expect_eq(hand.total_cards(), 5U, "holdem hand should count hole plus board cards");
    const std::array<poker::Card, 7> cards = hand.cards();
    expect_true(cards[0] == card("As") && cards[1] == card("Ks"), "holdem cards should preserve hole cards");
    expect_true(cards[2] == card("Qh") && cards[4] == card("2s"), "holdem cards should preserve board cards");
}

void test_evaluator() {
    expect_category(poker::evaluate(std::array<poker::Card, 7>{card("As"), card("Kd"), card("Qh"), card("9c"), card("7s"), card("4d"), card("2h")}, 7),
                    poker::HandCategory::high_card,
                    "high card");

    expect_category(poker::evaluate(std::array<poker::Card, 7>{card("As"), card("Ad"), card("Qh"), card("9c"), card("7s"), card("4d"), card("2h")}, 7),
                    poker::HandCategory::one_pair,
                    "pair");

    expect_category(poker::evaluate(std::array<poker::Card, 7>{card("As"), card("Ad"), card("Qh"), card("Qc"), card("7s"), card("4d"), card("2h")}, 7),
                    poker::HandCategory::two_pair,
                    "two pair");

    expect_category(poker::evaluate(std::array<poker::Card, 7>{card("As"), card("Ad"), card("Ah"), card("Qc"), card("7s"), card("4d"), card("2h")}, 7),
                    poker::HandCategory::three_of_a_kind,
                    "trips");

    expect_category(poker::evaluate(std::array<poker::Card, 7>{card("As"), card("Kd"), card("Qh"), card("Jc"), card("Ts"), card("4d"), card("2h")}, 7),
                    poker::HandCategory::straight,
                    "straight");

    expect_category(poker::evaluate(std::array<poker::Card, 7>{card("As"), card("5d"), card("4h"), card("3c"), card("2s"), card("Kd"), card("Qh")}, 7),
                    poker::HandCategory::straight,
                    "ace-low straight");

    expect_category(poker::evaluate(std::array<poker::Card, 7>{card("As"), card("Qs"), card("9s"), card("6s"), card("2s"), card("Kd"), card("Qh")}, 7),
                    poker::HandCategory::flush,
                    "flush");

    expect_category(poker::evaluate(std::array<poker::Card, 7>{card("As"), card("Ad"), card("Ah"), card("Ks"), card("Kd"), card("2c"), card("3d")}, 7),
                    poker::HandCategory::full_house,
                    "full house");

    expect_category(poker::evaluate(std::array<poker::Card, 7>{card("As"), card("Ad"), card("Ah"), card("Ac"), card("Kd"), card("2c"), card("3d")}, 7),
                    poker::HandCategory::four_of_a_kind,
                    "quads");

    expect_category(poker::evaluate(std::array<poker::Card, 7>{card("As"), card("Ks"), card("Qs"), card("Js"), card("Ts"), card("2c"), card("3d")}, 7),
                    poker::HandCategory::straight_flush,
                    "straight flush");

    const poker::HandValue pair_with_ace_kicker = poker::evaluate(std::array<poker::Card, 7>{card("9s"), card("9d"), card("Ah"), card("Qh"), card("7s"), card("4d"), card("2h")}, 7);
    const poker::HandValue pair_with_king_kicker = poker::evaluate(std::array<poker::Card, 7>{card("9s"), card("9d"), card("Kh"), card("Qh"), card("7s"), card("4d"), card("2h")}, 7);
    expect_true(pair_with_ace_kicker > pair_with_king_kicker, "tie breaker should prefer higher kicker");

    const poker::HandValue equal_hand_a = poker::evaluate(std::array<poker::Card, 7>{card("As"), card("Kd"), card("Qh"), card("9c"), card("7s"), card("4d"), card("2h")}, 7);
    const poker::HandValue equal_hand_b = poker::evaluate(std::array<poker::Card, 7>{card("Ah"), card("Kc"), card("Qs"), card("9d"), card("7h"), card("4s"), card("2c")}, 7);
    expect_true(equal_hand_a == equal_hand_b, "equivalent hands should compare equal");

    const poker::HandValue best_five_of_seven = poker::evaluate(std::array<poker::Card, 7>{card("As"), card("Ks"), card("Qs"), card("Js"), card("Ts"), card("9c"), card("2d")}, 7);
    expect_category(best_five_of_seven, poker::HandCategory::straight_flush, "best 5 of 7 should be selected");
}

void test_range_basics() {
    expect_eq(poker::HandRange::parse("AA").size(), 6U, "AA should expand to 6 combos");
    expect_eq(poker::HandRange::parse("AKs").size(), 4U, "AKs should expand to 4 combos");
    expect_eq(poker::HandRange::parse("AKo").size(), 12U, "AKo should expand to 12 combos");
    expect_eq(poker::HandRange::parse("AK").size(), 16U, "AK should expand to 16 combos");
    expect_eq(poker::HandRange::parse("22").size(), 6U, "22 should expand to 6 combos");
    expect_eq(poker::HandRange::parse("QJs").size(), 4U, "QJs should expand to 4 combos");

    const poker::HandRange qq_plus = poker::HandRange::parse("QQ+");
    expect_eq(qq_plus.size(), 18U, "QQ+ should expand to 18 combos");

    const poker::HandRange pairs_plus = poker::HandRange::parse("22+");
    expect_eq(pairs_plus.size(), 78U, "22+ should expand to 78 combos");

    const poker::HandRange suited_plus = poker::HandRange::parse("A5s+");
    expect_eq(suited_plus.size(), 36U, "A5s+ should expand to 36 combos");

    expect_eq(poker::HandRange::parse("QQ+, AKs").size(), 22U, "union should deduplicate and count correctly");
    expect_eq(poker::HandRange::parse("AKs, AKo").size(), 16U, "AKs and AKo should cover AK");
    expect_eq(poker::HandRange::parse("AKs, AK").size(), 16U, "AKs and AK should cover AK");
    expect_eq(poker::HandRange::parse("QQ, QQ+").size(), 18U, "overlapping pocket pair ranges should deduplicate");
    expect_eq(poker::HandRange::parse(" QQ+ , AKs , KQo ").size(), 34U, "whitespace should be ignored around union tokens");
}

void test_range_membership() {
    const poker::HandRange ak_suited = poker::HandRange::parse("AKs");
    expect_true(ak_suited.contains(poker::HandCombo(card("As"), card("Ks"))), "AsKs should belong to AKs");
    expect_true(ak_suited.contains(poker::HandCombo(card("Ks"), card("As"))), "canonical ordering should not matter");
    expect_true(!ak_suited.contains(poker::HandCombo(card("As"), card("Kd"))), "AsKd should not belong to AKs");

    const poker::HandRange ak_offsuit = poker::HandRange::parse("AKo");
    expect_true(ak_offsuit.contains(poker::HandCombo(card("Ah"), card("Kd"))), "AhKd should belong to AKo");

    const poker::HandRange ak_any = poker::HandRange::parse("AK");
    expect_true(ak_any.contains(poker::HandCombo(card("As"), card("Kh"))), "AsKh should belong to AK");

    const poker::HandRange aa = poker::HandRange::parse("AA");
    expect_true(aa.contains(poker::HandCombo(card("As"), card("Ah"))), "AsAh should belong to AA");
}

void test_handcombo_canonicalization() {
    const poker::HandCombo combo_a(card("As"), card("Ks"));
    const poker::HandCombo combo_b(card("Ks"), card("As"));
    expect_true(combo_a == combo_b, "AsKs and KsAs should be treated as identical");
}

void test_range_invalid_syntax() {
    const std::array<std::string_view, 12> invalid_inputs{
        "AX",
        "AQsX",
        "AKo+",
        "QQ-",
        "A2x",
        ",AKs",
        "AKs,",
        "ZZ",
        "1As",
        "2A",
        "AKx",
        "",
    };

    for (std::string_view notation : invalid_inputs) {
        expect_invalid_argument([&] {
            (void)poker::HandRange::parse(notation);
        }, std::string("invalid range notation should throw: ") + std::string(notation));
    }

    expect_invalid_argument([&] {
        (void)poker::HandRange::parse("AKs,,QQ");
    }, "empty token in the middle should throw");
}

[[maybe_unused]] poker::EquityOptions exact_options() {
    poker::EquityOptions options{};
    options.method = poker::EquityMethod::exact;
    return options;
}

[[maybe_unused]] poker::EquityOptions monte_carlo_options(std::uint64_t simulations, std::uint64_t seed) {
    poker::EquityOptions options{};
    options.method = poker::EquityMethod::monte_carlo;
    options.simulations = simulations;
    options.seed = seed;
    return options;
}

void test_equity_validation() {
    expect_no_throw([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks"), 0U, exact_options());
    }, "pre-flop should be valid");

    expect_no_throw([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c", "2s"}), 1U, exact_options());
    }, "flop should be valid");

    expect_no_throw([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c", "2s", "Td"}), 1U, exact_options());
    }, "turn should be valid");

    expect_no_throw([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c", "2s", "Td", "4h"}), 1U, exact_options());
    }, "river should be valid");

    poker::HoldemHand invalid_board_1 = make_hand("As", "Ks", {"Qh"});
    expect_invalid_argument([&] {
        (void)poker::calculate_equity(invalid_board_1, 1U, exact_options());
    }, "board size 1 should be rejected");

    poker::HoldemHand invalid_board_2 = make_hand("As", "Ks", {"Qh", "7c"});
    expect_invalid_argument([&] {
        (void)poker::calculate_equity(invalid_board_2, 1U, exact_options());
    }, "board size 2 should be rejected");

    expect_invalid_argument([&] {
        (void)poker::calculate_equity(make_hand("As", "As"), 1U, exact_options());
    }, "duplicate hero cards should be rejected");

    expect_invalid_argument([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks", {"As", "7c", "2s"}), 1U, exact_options());
    }, "duplicate hero and board cards should be rejected");

    expect_invalid_argument([&] {
        poker::EquityOptions options{};
        options.method = poker::EquityMethod::monte_carlo;
        options.simulations = 0U;
        (void)poker::calculate_equity(make_hand("As", "Ks"), 1U, options);
    }, "zero simulations should be rejected");
}

void test_equity_correctness() {
    const poker::EquityResult preflop = poker::calculate_equity(make_hand("As", "Ks"), 2U, monte_carlo_options(200U, 12345U));
    expect_true(preflop.win_probability >= 0.0 && preflop.win_probability <= 1.0, "win probability should be in range");
    expect_true(preflop.tie_probability >= 0.0 && preflop.tie_probability <= 1.0, "tie probability should be in range");
    expect_true(preflop.loss_probability >= 0.0 && preflop.loss_probability <= 1.0, "loss probability should be in range");
    expect_true(preflop.equity >= 0.0 && preflop.equity <= 1.0, "equity should be in range");
    expect_close(preflop.win_probability + preflop.tie_probability + preflop.loss_probability, 1.0, 1e-12,
                 "probabilities should sum to 1");
    expect_true(preflop.equity >= preflop.win_probability, "equity should be at least win probability");
    expect_true(preflop.equity <= preflop.win_probability + preflop.tie_probability,
                "equity should not exceed win + tie probability");

    const poker::EquityResult same_seed_a = poker::calculate_equity(make_hand("As", "Ks"), 2U, monte_carlo_options(100U, 424242U));
    const poker::EquityResult same_seed_b = poker::calculate_equity(make_hand("As", "Ks"), 2U, monte_carlo_options(100U, 424242U));
    expect_true(same_seed_a.win_probability == same_seed_b.win_probability &&
                same_seed_a.tie_probability == same_seed_b.tie_probability &&
                same_seed_a.loss_probability == same_seed_b.loss_probability &&
                same_seed_a.equity == same_seed_b.equity,
                "same seed should produce identical results");

    const poker::EquityResult different_seed_a = poker::calculate_equity(make_hand("As", "Ks"), 2U, monte_carlo_options(200U, 1U));
    const poker::EquityResult different_seed_b = poker::calculate_equity(make_hand("As", "Ks"), 2U, monte_carlo_options(200U, 2U));
    const bool any_difference = different_seed_a.win_probability != different_seed_b.win_probability ||
                                different_seed_a.tie_probability != different_seed_b.tie_probability ||
                                different_seed_a.loss_probability != different_seed_b.loss_probability ||
                                different_seed_a.equity != different_seed_b.equity;
    expect_true(any_difference, "different seeds should be able to produce different Monte Carlo results");
}

void test_equity_sanity() {
    const poker::EquityResult strong_hand = poker::calculate_equity(
        make_hand("As", "Ah", {"Ac", "Kd", "Kh", "Qh", "2s"}), 2U, monte_carlo_options(300U, 77U));
    expect_true(strong_hand.equity > 0.85, "strong made hand on the river should have high equity");

    const poker::EquityResult weak_hand = poker::calculate_equity(make_hand("7d", "2c"), 5U, monte_carlo_options(300U, 88U));
    expect_true(weak_hand.equity < 0.25, "very weak hand against many opponents should have low equity");

    const poker::EquityResult board_tie = poker::calculate_equity(
        make_hand("2d", "3c", {"Ah", "Kh", "Qh", "Jh", "Th"}), 2U, monte_carlo_options(50U, 99U));
    expect_close(board_tie.win_probability, 0.0, 1e-12, "board tie should have no wins");
    expect_close(board_tie.loss_probability, 0.0, 1e-12, "board tie should have no losses");
    expect_close(board_tie.tie_probability, 1.0, 1e-12, "board tie should have all ties");
    expect_close(board_tie.equity, 1.0 / 3.0, 1e-12, "board tie should split equity across all players");
}

void test_exact_validation() {
    expect_no_throw([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c", "2s"}), 1U, exact_options());
    }, "flop exact should be valid");

    expect_no_throw([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c", "2s", "Td"}), 1U, exact_options());
    }, "turn exact should be valid");

    expect_no_throw([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c", "2s", "Td", "4h"}), 1U, exact_options());
    }, "river exact should be valid");

    expect_invalid_argument([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks", {"Qh"}), 1U, exact_options());
    }, "exact solver should reject board size 1");

    expect_invalid_argument([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c"}), 1U, exact_options());
    }, "exact solver should reject board size 2");

    expect_invalid_argument([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks"), 6U, exact_options());
    }, "exact solver should reject six opponents");
}

void test_exact_correctness() {
    const poker::EquityResult river = poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c", "2s", "Td", "4h"}), 1U, exact_options());
    expect_eq(river.evaluated_states, theoretical_exact_states(5U, 1U), "river exact state count");
    expect_close(river.win_probability + river.tie_probability + river.loss_probability, 1.0, 1e-12,
                 "river exact probabilities should sum to 1");

    const poker::EquityResult turn = poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c", "2s", "Td"}), 1U, exact_options());
    expect_eq(turn.evaluated_states, theoretical_exact_states(4U, 1U), "turn exact state count");
    expect_close(turn.win_probability + turn.tie_probability + turn.loss_probability, 1.0, 1e-12,
                 "turn exact probabilities should sum to 1");

    const poker::EquityResult flop = poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c", "2s"}), 1U, exact_options());
    expect_eq(flop.evaluated_states, theoretical_exact_states(3U, 1U), "flop exact state count");
    expect_close(flop.win_probability + flop.tie_probability + flop.loss_probability, 1.0, 1e-12,
                 "flop exact probabilities should sum to 1");

    expect_true(river.equity >= 0.0 && river.equity <= 1.0, "river exact equity range");
    expect_true(turn.equity >= 0.0 && turn.equity <= 1.0, "turn exact equity range");
    expect_true(flop.equity >= 0.0 && flop.equity <= 1.0, "flop exact equity range");

    const poker::EquityResult exact_tie = poker::calculate_equity(make_hand("2d", "3c", {"Ah", "Kh", "Qh", "Jh", "Th"}), 1U, exact_options());
    expect_close(exact_tie.win_probability, 0.0, 1e-12, "exact tie should have no wins");
    expect_close(exact_tie.loss_probability, 0.0, 1e-12, "exact tie should have no losses");
    expect_close(exact_tie.tie_probability, 1.0, 1e-12, "exact tie should have all ties");
    expect_close(exact_tie.equity, 0.5, 1e-12, "exact tie should split equity evenly");

    const poker::EquityResult zero_opponents = poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c", "2s"}), 0U, exact_options());
    expect_close(zero_opponents.win_probability, 1.0, 1e-12, "zero opponents should always win");
    expect_close(zero_opponents.tie_probability, 0.0, 1e-12, "zero opponents should have no ties");
    expect_close(zero_opponents.loss_probability, 0.0, 1e-12, "zero opponents should have no losses");
    expect_close(zero_opponents.equity, 1.0, 1e-12, "zero opponents should have full equity");
}

void test_exact_monte_carlo_cross_validation() {
    const poker::HoldemHand river_hand = make_hand("As", "Ks", {"Qh", "7c", "2s", "Td", "4h"});
    const poker::EquityResult exact = poker::calculate_equity(river_hand, 1U, exact_options());
    const poker::EquityResult monte_carlo = poker::calculate_equity(river_hand, 1U, monte_carlo_options(50000U, 20240601U));
    expect_true(std::fabs(exact.equity - monte_carlo.equity) < 0.05,
                "Monte Carlo should converge toward the exact river result");
}

}  // namespace

int main() {
    try {
        test_cards();
        test_deck();
        test_holdem_representation();
        test_evaluator();
        test_range_basics();
        test_range_membership();
        test_handcombo_canonicalization();
        test_range_invalid_syntax();
        test_equity_validation();
        test_equity_correctness();
        test_equity_sanity();
        test_exact_validation();
        test_exact_correctness();
        test_exact_monte_carlo_cross_validation();
    } catch (const std::exception& error) {
        std::cerr << "Unhandled exception in tests: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All tests passed\n";
    return 0;
}