#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "poker/card.hpp"
#include "poker/deck.hpp"
#include "poker/evaluator.hpp"
#include "poker/hand.hpp"

namespace {

int failures = 0;

void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect_eq(std::uint64_t actual, std::uint64_t expected, const std::string& message) {
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL: " << message << " expected=" << expected << " actual=" << actual << '\n';
    }
}

void expect_category(poker::HandValue value, poker::HandCategory expected, const std::string& message) {
    if (value.category() != expected) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

poker::Card card(const char* text) {
    const std::optional<poker::Card> parsed = poker::Card::from_string(text);
    if (!parsed) {
        throw std::runtime_error(std::string("invalid card literal: ") + text);
    }
    return *parsed;
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
    expect_true(poker::Card::from_string("ZZ").has_value() == false, "invalid card should fail to parse");
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
    poker::HoldemHand hand{};
    hand.hole[0] = card("As");
    hand.hole[1] = card("Ks");
    hand.board_count = 3;
    hand.board[0] = card("Qh");
    hand.board[1] = card("7c");
    hand.board[2] = card("2s");

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

}  // namespace

int main() {
    try {
        test_cards();
        test_deck();
        test_holdem_representation();
        test_evaluator();
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