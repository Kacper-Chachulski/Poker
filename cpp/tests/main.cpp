#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "poker/card.hpp"
#include "poker/deck.hpp"
#include "poker/game_state.hpp"
#include "poker/ev.hpp"
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

template <typename Fn>
bool run_test(const char* name, Fn&& fn) {
    try {
        fn();
        return true;
    } catch (const std::exception& error) {
        std::cerr << "Unhandled exception in " << name << ": " << error.what() << '\n';
        return false;
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

std::vector<poker::Card> make_board(std::initializer_list<std::string_view> board = {}) {
    std::vector<poker::Card> cards;
    cards.reserve(board.size());
    for (std::string_view text : board) {
        cards.push_back(card(text));
    }
    return cards;
}

bool contains_action(const std::vector<poker::BettingAction>& actions, poker::BettingAction action) {
    return std::find(actions.begin(), actions.end(), action) != actions.end();
}

void expect_actions(const std::vector<poker::BettingAction>& actions,
                    std::initializer_list<poker::BettingAction> expected,
                    const std::string& message) {
    if (actions.size() != expected.size()) {
        fail(message + " expected size=" + std::to_string(expected.size()) + " actual=" + std::to_string(actions.size()));
        return;
    }

    for (poker::BettingAction action : expected) {
        if (!contains_action(actions, action)) {
            fail(message + ": missing expected betting action");
            return;
        }
    }
}

poker::Opponent make_random_opponent() {
    return poker::RandomOpponent{};
}

poker::Opponent make_specific_opponent(std::string_view first, std::string_view second) {
    return poker::HandCombo(card(first), card(second));
}

poker::Opponent make_range_opponent(std::string_view notation) {
    return poker::HandRange::parse(notation);
}

poker::GameState make_game_state(poker::Street street,
                                 std::string_view hero_first,
                                 std::string_view hero_second,
                                 std::initializer_list<std::string_view> board,
                                 std::initializer_list<poker::Opponent> opponents,
                                 double pot,
                                 double call_amount,
                                 double effective_stack,
                                 std::optional<double> minimum_raise_amount = std::nullopt,
                                 bool check_allowed = false) {
    poker::GameState state{};
    state.street = street;
    state.hero = make_hand(hero_first, hero_second, board);
    state.betting.current_pot = pot;
    state.betting.call_amount = call_amount;
    state.betting.hero_stack = effective_stack;
    state.betting.minimum_raise_amount = minimum_raise_amount;
    state.betting.check_allowed = check_allowed;
    state.opponents = opponents;
    state.player_count = 1U + opponents.size();
    return state;
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

std::size_t count_legal_villain_combos(const poker::HoldemHand& hero, const poker::HandRange& villain_range) {
    std::array<poker::Card, 7> known_cards{};
    std::size_t known_count = 0U;
    known_cards[known_count++] = hero.hole[0];
    known_cards[known_count++] = hero.hole[1];
    for (std::size_t index = 0U; index < hero.board_count; ++index) {
        known_cards[known_count++] = hero.board[index];
    }

    std::size_t legal_count = 0U;
    for (const poker::HandCombo& combo : villain_range) {
        const std::array<poker::Card, 2> cards = combo.cards();
        bool blocked = false;
        for (std::size_t index = 0U; index < known_count; ++index) {
            if (cards[0] == known_cards[index] || cards[1] == known_cards[index]) {
                blocked = true;
                break;
            }
        }

        if (!blocked) {
            ++legal_count;
        }
    }

    return legal_count;
}

std::uint64_t theoretical_range_exact_states(std::size_t board_count, std::size_t legal_combo_count) {
    return legal_combo_count * binomial(48U - board_count, 5U - board_count);
}

std::size_t count_legal_combos_after_board(const poker::HandRange& range, const std::vector<poker::Card>& board) {
    std::size_t legal_count = 0U;
    for (const poker::HandCombo& combo : range) {
        const std::array<poker::Card, 2> cards = combo.cards();
        bool blocked = false;
        for (poker::Card board_card : board) {
            if (cards[0] == board_card || cards[1] == board_card) {
                blocked = true;
                break;
            }
        }

        if (!blocked) {
            ++legal_count;
        }
    }

    return legal_count;
}

std::size_t count_legal_pairs_after_board(const poker::HandRange& hero_range,
                                          const poker::HandRange& villain_range,
                                          const std::vector<poker::Card>& board) {
    std::size_t pair_count = 0U;
    for (const poker::HandCombo& hero_combo : hero_range) {
        const std::array<poker::Card, 2> hero_cards = hero_combo.cards();
        bool hero_blocked = false;
        for (poker::Card board_card : board) {
            if (hero_cards[0] == board_card || hero_cards[1] == board_card) {
                hero_blocked = true;
                break;
            }
        }
        if (hero_blocked) {
            continue;
        }

        for (const poker::HandCombo& villain_combo : villain_range) {
            const std::array<poker::Card, 2> villain_cards = villain_combo.cards();
            bool blocked = false;
            for (poker::Card board_card : board) {
                if (villain_cards[0] == board_card || villain_cards[1] == board_card ||
                    hero_cards[0] == board_card || hero_cards[1] == board_card) {
                    blocked = true;
                    break;
                }
            }

            if (blocked) {
                continue;
            }

            if (hero_cards[0] == villain_cards[0] || hero_cards[0] == villain_cards[1] ||
                hero_cards[1] == villain_cards[0] || hero_cards[1] == villain_cards[1]) {
                continue;
            }

            ++pair_count;
        }
    }

    return pair_count;
}

struct CliRunResult {
    int exit_code{0};
    std::string output{};
};

CliRunResult run_cli_command(const std::string& command) {
    const std::string output_path = "cli_test_output.txt";
    const std::string full_command = command + " > " + output_path + " 2>&1";
    const int exit_code = std::system(full_command.c_str());

    std::ifstream file(output_path, std::ios::binary);
    const std::string output((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::remove(output_path.c_str());

    return {exit_code, output};
}

void expect_cli_success(const std::string& command, std::initializer_list<std::string_view> snippets) {
    const CliRunResult result = run_cli_command(command);
    expect_eq(static_cast<std::uint64_t>(result.exit_code), 0U, std::string("CLI command should succeed: ") + command);
    for (std::string_view snippet : snippets) {
        expect_true(result.output.find(snippet) != std::string::npos,
                    std::string("CLI output should contain '") + std::string(snippet) + "': " + command);
    }
}

void expect_cli_failure(const std::string& command, std::initializer_list<std::string_view> snippets) {
    const CliRunResult result = run_cli_command(command);
    expect_true(result.exit_code != 0, std::string("CLI command should fail: ") + command);
    for (std::string_view snippet : snippets) {
        expect_true(result.output.find(snippet) != std::string::npos,
                    std::string("CLI error output should contain '") + std::string(snippet) + "': " + command);
    }
}

[[maybe_unused]] poker::EquityOptions exact_options();

[[maybe_unused]] poker::EquityOptions monte_carlo_options(std::uint64_t simulations, std::uint64_t seed);

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

void test_betting_validation_and_legal_actions() {
    const poker::BettingState no_bet{100.0, 0.0, 500.0, 10.0, true};
    expect_no_throw([&] { poker::validate_betting_state(no_bet); }, "no-bet state should be valid");
    expect_actions(poker::get_legal_actions(no_bet), {poker::BettingAction::check, poker::BettingAction::bet, poker::BettingAction::all_in}, "no-bet legal actions");

    const poker::BettingState facing_bet{100.0, 50.0, 500.0, 50.0, false};
    expect_no_throw([&] { poker::validate_betting_state(facing_bet); }, "facing-bet state should be valid");
    expect_actions(poker::get_legal_actions(facing_bet), {poker::BettingAction::fold, poker::BettingAction::call, poker::BettingAction::raise, poker::BettingAction::all_in}, "facing-bet legal actions");

    const poker::BettingState exact_call{100.0, 50.0, 50.0, std::nullopt, false};
    expect_no_throw([&] { poker::validate_betting_state(exact_call); }, "exact-call state should be valid");
    expect_actions(poker::get_legal_actions(exact_call), {poker::BettingAction::fold, poker::BettingAction::call}, "exact-call legal actions");

    const poker::BettingState call_but_no_raise{100.0, 50.0, 60.0, std::nullopt, false};
    expect_no_throw([&] { poker::validate_betting_state(call_but_no_raise); }, "call-but-no-raise state should be valid");
    expect_actions(poker::get_legal_actions(call_but_no_raise), {poker::BettingAction::fold, poker::BettingAction::call, poker::BettingAction::all_in}, "call-but-no-raise legal actions");

    const poker::BettingState all_in_only{100.0, 0.0, 6.0, 10.0, true};
    expect_no_throw([&] { poker::validate_betting_state(all_in_only); }, "all-in-only no-bet state should be valid");
    expect_actions(poker::get_legal_actions(all_in_only), {poker::BettingAction::check, poker::BettingAction::all_in}, "all-in-only legal actions");

    expect_invalid_argument([&] {
        (void)poker::validate_betting_state(poker::BettingState{100.0, 0.0, -1.0, 10.0, true});
    }, "negative stack should be rejected");

    expect_invalid_argument([&] {
        (void)poker::validate_betting_state(poker::BettingState{100.0, -1.0, 500.0, 10.0, true});
    }, "negative call should be rejected");

    expect_invalid_argument([&] {
        (void)poker::validate_betting_state(poker::BettingState{100.0, 50.0, 40.0, 10.0, false});
    }, "call amount greater than stack should be rejected");

    expect_invalid_argument([&] {
        (void)poker::validate_betting_state(poker::BettingState{100.0, 50.0, 500.0, 0.0, false});
    }, "minimum raise amount must be positive");

    expect_invalid_argument([&] {
        (void)poker::validate_betting_state(poker::BettingState{100.0, 50.0, 500.0, 10.0, true});
    }, "check cannot be allowed when facing a bet");

    expect_invalid_argument([&] {
        (void)poker::validate_betting_state(poker::BettingState{100.0, 0.0, 500.0, std::nullopt, true});
    }, "no-bet state with chips should require a betting amount");
}

void test_game_state_validation() {
    const poker::GameState preflop = make_game_state(poker::Street::preflop, "As", "Ks", {}, {make_random_opponent()}, 100.0, 50.0, 1000.0);
    const poker::GameState flop = make_game_state(poker::Street::flop, "As", "Ks", {"Qh", "7c", "2s"}, {make_random_opponent()}, 100.0, 50.0, 1000.0);
    const poker::GameState turn = make_game_state(poker::Street::turn, "As", "Ks", {"Qh", "7c", "2s", "Td"}, {make_random_opponent()}, 100.0, 50.0, 1000.0);
    const poker::GameState river = make_game_state(poker::Street::river, "As", "Ks", {"Qh", "7c", "2s", "Td", "4h"}, {make_random_opponent()}, 100.0, 50.0, 1000.0);

    expect_no_throw([&] { poker::validate_game_state(preflop); }, "preflop state should be valid");
    expect_no_throw([&] { poker::validate_game_state(flop); }, "flop state should be valid");
    expect_no_throw([&] { poker::validate_game_state(turn); }, "turn state should be valid");
    expect_no_throw([&] { poker::validate_game_state(river); }, "river state should be valid");

    expect_invalid_argument([&] {
        (void)poker::validate_game_state(make_game_state(poker::Street::preflop, "As", "Ks", {"Qh", "7c", "2s"}, {make_random_opponent()}, 100.0, 50.0, 1000.0));
    }, "preflop must reject a board");

    expect_invalid_argument([&] {
        (void)poker::validate_game_state(make_game_state(poker::Street::flop, "As", "Ks", {}, {make_random_opponent()}, 100.0, 50.0, 1000.0));
    }, "flop must require three board cards");

    expect_invalid_argument([&] {
        (void)poker::validate_game_state(make_game_state(poker::Street::turn, "As", "Ks", {"Qh", "7c", "2s"}, {make_random_opponent()}, 100.0, 50.0, 1000.0));
    }, "turn must require four board cards");

    expect_invalid_argument([&] {
        (void)poker::validate_game_state(make_game_state(poker::Street::river, "As", "Ks", {"Qh", "7c", "2s", "Td"}, {make_random_opponent()}, 100.0, 50.0, 1000.0));
    }, "river must require five board cards");

    expect_invalid_argument([&] {
        (void)poker::validate_game_state(make_game_state(poker::Street::flop, "As", "As", {"Qh", "7c", "2s"}, {make_random_opponent()}, 100.0, 50.0, 1000.0));
    }, "duplicate hero cards should be rejected");

    expect_invalid_argument([&] {
        (void)poker::validate_game_state(make_game_state(poker::Street::flop, "As", "Ks", {"As", "7c", "2s"}, {make_random_opponent()}, 100.0, 50.0, 1000.0));
    }, "hero and board overlap should be rejected");

    expect_invalid_argument([&] {
        (void)poker::validate_game_state(make_game_state(poker::Street::flop, "As", "Ks", {"Qh", "7c", "2s"}, {make_specific_opponent("As", "Qd")}, 100.0, 50.0, 1000.0));
    }, "hero and opponent overlap should be rejected");

    expect_invalid_argument([&] {
        (void)poker::validate_game_state(make_game_state(poker::Street::flop, "As", "Ks", {"Qh", "7c", "2s"}, {make_specific_opponent("Qd", "Jd")}, -1.0, 50.0, 1000.0));
    }, "negative pot should be rejected");

    expect_invalid_argument([&] {
        (void)poker::validate_game_state(make_game_state(poker::Street::flop, "As", "Ks", {"Qh", "7c", "2s"}, {make_specific_opponent("Qd", "Jd")}, 100.0, -1.0, 1000.0));
    }, "negative call should be rejected");

    expect_invalid_argument([&] {
        (void)poker::validate_game_state(make_game_state(poker::Street::flop, "As", "Ks", {"Qh", "7c", "2s"}, {make_specific_opponent("Qd", "Jd")}, 100.0, 50.0, 40.0));
    }, "call amount greater than stack should be rejected in GameState");

    expect_invalid_argument([&] {
        (void)poker::validate_game_state(make_game_state(poker::Street::flop, "As", "Ks", {"Qh", "7c", "2s"}, {make_specific_opponent("Qd", "Jd")}, 100.0, 50.0, -1.0));
    }, "negative stack should be rejected");
}

void test_game_state_equity_dispatch() {
    const poker::GameState specific_state = make_game_state(poker::Street::flop,
                                                            "As",
                                                            "Ks",
                                                            {"Qh", "7c", "2s"},
                                                            {make_specific_opponent("Qd", "Qc")},
                                                            100.0,
                                                            50.0,
                                                            1000.0);
    const poker::GameState range_state = make_game_state(poker::Street::flop,
                                                         "As",
                                                         "Ks",
                                                         {"Qh", "7c", "2s"},
                                                         {make_range_opponent("QQ+, AJs+, KQs")},
                                                         100.0,
                                                         50.0,
                                                         1000.0);
    const poker::GameState random_state = make_game_state(poker::Street::flop,
                                                          "As",
                                                          "Ks",
                                                          {"Qh", "7c", "2s"},
                                                          {make_random_opponent()},
                                                          100.0,
                                                          50.0,
                                                          1000.0);

    const poker::HoldemHand hero = make_hand("As", "Ks", {"Qh", "7c", "2s"});
    const poker::HandCombo specific_combo(card("Qd"), card("Qc"));
    const poker::HandRange singleton_range = poker::HandRange::from_combo(specific_combo);

    const poker::EquityResult direct_specific = poker::calculate_equity(hero, singleton_range, exact_options());
    const poker::EquityResult state_specific = poker::calculate_equity(specific_state, exact_options());
    expect_close(state_specific.equity, direct_specific.equity, 1e-12, "specific opponent state should match direct singleton range equity");

    const poker::HandRange villain_range = poker::HandRange::parse("QQ+, AJs+, KQs");
    const poker::EquityResult direct_range = poker::calculate_equity(hero, villain_range, exact_options());
    const poker::EquityResult state_range = poker::calculate_equity(range_state, exact_options());
    expect_close(state_range.equity, direct_range.equity, 1e-12, "range opponent state should match direct range equity");

    const poker::EquityResult direct_random = poker::calculate_equity(hero, 1U, exact_options());
    const poker::EquityResult state_random = poker::calculate_equity(random_state, exact_options());
    expect_close(state_random.equity, direct_random.equity, 1e-12, "random opponent state should match direct random equity");
}

void test_range_equity_smoke_and_card_removal() {
    const poker::HoldemHand hero = make_hand("As", "Ks", {"Qd", "Jc", "Th", "9s", "2c"});
    const poker::HandRange villain_range = poker::HandRange::parse("AK");

    const poker::EquityResult result = poker::calculate_equity(hero, villain_range, exact_options());
    expect_eq(result.evaluated_states, 9U, "AK should shrink to 9 legal combos after removing hero cards");
    expect_close(result.win_probability, 0.0, 1e-12, "hero should not win against tied AK combos on the river");
    expect_close(result.tie_probability, 1.0, 1e-12, "all legal AK combos should tie on this board");
    expect_close(result.loss_probability, 0.0, 1e-12, "hero should not lose against tied AK combos on the river");
    expect_close(result.equity, 0.5, 1e-12, "tied heads-up river result should split equity evenly");
}

void test_range_exact_river_equity() {
    const poker::HoldemHand hero = make_hand("As", "Ks", {"Qd", "Jc", "Th", "9s", "2c"});
    const poker::HandRange villain_range = poker::HandRange::parse("AA, KK, QQ, AK");

    const std::size_t legal_combo_count = count_legal_villain_combos(hero, villain_range);
    const poker::EquityResult result = poker::calculate_equity(hero, villain_range, exact_options());

    expect_eq(legal_combo_count, 18U, "river range should shrink to 18 legal combos");
    expect_eq(result.evaluated_states, theoretical_range_exact_states(hero.board_count, legal_combo_count),
              "river exact range state count should match legal combos times one showdown each");
    expect_close(result.win_probability, 9.0 / 18.0, 1e-12, "river win probability should be exact");
    expect_close(result.tie_probability, 9.0 / 18.0, 1e-12, "river tie probability should be exact");
    expect_close(result.loss_probability, 0.0, 1e-12, "river loss probability should be exact");
    expect_close(result.equity, 13.5 / 18.0, 1e-12, "river equity should average wins and half-ties");
}

void test_range_exact_flop_and_turn_equity() {
    const poker::HandRange villain_range = poker::HandRange::parse("AA, KK, QQ, AK");

    const poker::HoldemHand flop = make_hand("As", "Ks", {"Qs", "Js", "Ts"});
    const std::size_t flop_legal_count = count_legal_villain_combos(flop, villain_range);
    const poker::EquityResult flop_result = poker::calculate_equity(flop, villain_range, exact_options());
    expect_eq(flop_legal_count, 18U, "flop should leave 18 legal villain combos");
    expect_eq(flop_result.evaluated_states, theoretical_range_exact_states(flop.board_count, flop_legal_count),
              "flop exact state count should include every legal combo and board runout");
    expect_close(flop_result.win_probability, 1.0, 1e-12, "royal flush on the flop should always win");
    expect_close(flop_result.tie_probability, 0.0, 1e-12, "royal flush on the flop should never tie");
    expect_close(flop_result.loss_probability, 0.0, 1e-12, "royal flush on the flop should never lose");
    expect_close(flop_result.equity, 1.0, 1e-12, "royal flush on the flop should have full equity");

    const poker::HoldemHand turn = make_hand("As", "Ks", {"Qs", "Js", "Ts", "2d"});
    const std::size_t turn_legal_count = count_legal_villain_combos(turn, villain_range);
    const poker::EquityResult turn_result = poker::calculate_equity(turn, villain_range, exact_options());
    expect_eq(turn_legal_count, 18U, "turn should leave 18 legal villain combos");
    expect_eq(turn_result.evaluated_states, theoretical_range_exact_states(turn.board_count, turn_legal_count),
              "turn exact state count should include every legal combo and river card");
    expect_close(turn_result.win_probability, 1.0, 1e-12, "royal flush on the turn should always win");
    expect_close(turn_result.tie_probability, 0.0, 1e-12, "royal flush on the turn should never tie");
    expect_close(turn_result.loss_probability, 0.0, 1e-12, "royal flush on the turn should never lose");
    expect_close(turn_result.equity, 1.0, 1e-12, "royal flush on the turn should have full equity");
}

void test_range_monte_carlo_cross_validation() {
    const poker::HoldemHand hero = make_hand("As", "Ks", {"Qd", "Jc", "Th", "9s", "2c"});
    const poker::HandRange villain_range = poker::HandRange::parse("AA, KK, QQ, AK");

    poker::EquityOptions exact = exact_options();
    poker::EquityOptions monte = monte_carlo_options(50000U, 20240818U);

    const poker::EquityResult exact_result = poker::calculate_equity(hero, villain_range, exact);
    const poker::EquityResult monte_result = poker::calculate_equity(hero, villain_range, monte);

    expect_true(std::fabs(exact_result.equity - monte_result.equity) < 0.02,
                "Monte Carlo range equity should converge toward the exact river result");
}

void test_range_monte_carlo_determinism() {
    const poker::HoldemHand hero = make_hand("As", "Ks", {"Qd", "Jc", "Th", "9s", "2c"});
    const poker::HandRange villain_range = poker::HandRange::parse("AA, KK, QQ, AK");

    const poker::EquityResult first = poker::calculate_equity(hero, villain_range, monte_carlo_options(2000U, 424242U));
    const poker::EquityResult second = poker::calculate_equity(hero, villain_range, monte_carlo_options(2000U, 424242U));

    expect_true(first.win_probability == second.win_probability &&
                first.tie_probability == second.tie_probability &&
                first.loss_probability == second.loss_probability &&
                first.equity == second.equity,
                "same seed should produce identical range Monte Carlo results");
}

void test_range_empty_after_card_removal() {
    const poker::HoldemHand hero = make_hand("As", "Ah", {"Ad", "Ac", "Qh"});
    const poker::HandRange villain_range = poker::HandRange::parse("AK");

    expect_invalid_argument([&] {
        (void)poker::calculate_equity(hero, villain_range, exact_options());
    }, "range should throw when every combo is removed by known cards");
}

void test_range_vs_range_river_manual() {
    const poker::HandRange hero_range = poker::HandRange::parse("AA");
    const poker::HandRange villain_range = poker::HandRange::parse("KK");
    const std::vector<poker::Card> board = make_board({"Ah", "Kd", "Qs", "Jc", "Td"});

    const poker::EquityResult result = poker::calculate_equity(hero_range, villain_range, board, exact_options());
    expect_eq(result.evaluated_states, 9U, "AA vs KK river should evaluate the remaining legal concrete pairs");
    expect_close(result.win_probability, 0.0, 1e-12, "board straight should force a tie");
    expect_close(result.tie_probability, 1.0, 1e-12, "board straight should force a tie");
    expect_close(result.loss_probability, 0.0, 1e-12, "board straight should force a tie");
    expect_close(result.equity, 0.5, 1e-12, "board straight should split equity");
}

void test_range_vs_range_aa_kk_exact() {
    const poker::HandRange hero_range = poker::HandRange::parse("AA");
    const poker::HandRange villain_range = poker::HandRange::parse("KK");
    const std::vector<poker::Card> board = make_board({"Qh", "Jd", "Tc", "8s", "7d"});

    const poker::EquityResult result = poker::calculate_equity(hero_range, villain_range, board, exact_options());
    expect_eq(result.evaluated_states, 36U, "AA vs KK river should evaluate all legal concrete pairs");
    expect_close(result.win_probability, 1.0, 1e-12, "AA should beat KK on this river board");
    expect_close(result.tie_probability, 0.0, 1e-12, "AA vs KK should not tie on this board");
    expect_close(result.loss_probability, 0.0, 1e-12, "AA vs KK should not lose on this board");
    expect_close(result.equity, 1.0, 1e-12, "AA should have full equity against KK here");
}

void test_range_vs_range_ak_overlap_removal() {
    const poker::HandRange hero_range = poker::HandRange::parse("AK");
    const poker::HandRange villain_range = poker::HandRange::parse("AK");
    const std::vector<poker::Card> board = make_board({"Qh", "Jd", "9c", "7s", "2d"});

    const std::size_t legal_hero_combos = count_legal_combos_after_board(hero_range, board);
    const std::size_t legal_pairs = count_legal_pairs_after_board(hero_range, villain_range, board);
    const poker::EquityResult result = poker::calculate_equity(hero_range, villain_range, board, exact_options());

    expect_eq(legal_hero_combos, 16U, "AK should leave 16 legal hero combos on this board");
    expect_eq(legal_pairs, 144U, "AK vs AK should only evaluate non-overlapping ordered pairs");
    expect_eq(result.evaluated_states, 144U, "evaluated states should match the legal ordered pair count on the river");
}

void test_range_vs_range_flop_turn_exact() {
    const poker::HandRange hero_range = poker::HandRange::parse("AA");
    const poker::HandRange villain_range = poker::HandRange::parse("KK");

    const poker::EquityResult flop = poker::calculate_equity(hero_range, villain_range, make_board({"Qh", "Jd", "Tc"}), exact_options());
    expect_eq(flop.evaluated_states, 35640U, "flop exact range-vs-range state count should match legal pairs and board runouts");
    expect_close(flop.win_probability + flop.tie_probability + flop.loss_probability, 1.0, 1e-12,
                 "flop exact probabilities should sum to 1");

    const poker::EquityResult turn = poker::calculate_equity(hero_range, villain_range, make_board({"Qh", "Jd", "Tc", "9s"}), exact_options());
    expect_eq(turn.evaluated_states, 1584U, "turn exact range-vs-range state count should match legal pairs and river runouts");
    expect_close(turn.win_probability + turn.tie_probability + turn.loss_probability, 1.0, 1e-12,
                 "turn exact probabilities should sum to 1");
}

void test_range_vs_range_monte_carlo_cross_validation() {
    const poker::HandRange hero_range = poker::HandRange::parse("AA, KK");
    const poker::HandRange villain_range = poker::HandRange::parse("QQ, JJ");
    const std::vector<poker::Card> board = make_board({"Qh", "Jd", "Tc", "9s", "2d"});

    const poker::EquityResult exact = poker::calculate_equity(hero_range, villain_range, board, exact_options());
    const poker::EquityResult monte = poker::calculate_equity(hero_range, villain_range, board, monte_carlo_options(20000U, 20260818U));

    expect_true(std::fabs(exact.equity - monte.equity) < 0.03,
                "range-vs-range Monte Carlo should converge toward exact equity");
}

void test_range_vs_range_monte_carlo_determinism() {
    const poker::HandRange hero_range = poker::HandRange::parse("AA, KK");
    const poker::HandRange villain_range = poker::HandRange::parse("QQ, JJ");
    const std::vector<poker::Card> board = make_board({"Qh", "Jd", "Tc", "9s", "2d"});

    const poker::EquityResult first = poker::calculate_equity(hero_range, villain_range, board, monte_carlo_options(5000U, 424242U));
    const poker::EquityResult second = poker::calculate_equity(hero_range, villain_range, board, monte_carlo_options(5000U, 424242U));

    expect_true(first.win_probability == second.win_probability &&
                first.tie_probability == second.tie_probability &&
                first.loss_probability == second.loss_probability &&
                first.equity == second.equity,
                "same seed should produce identical range-vs-range Monte Carlo results");
}

void test_range_vs_range_validation() {
    const poker::HandRange hero_range = poker::HandRange::parse("AK");
    const poker::HandRange villain_range = poker::HandRange::parse("AK");

    expect_invalid_argument([&] {
        (void)poker::calculate_equity(poker::HandRange{}, villain_range, make_board(), exact_options());
    }, "empty hero range should be rejected");

    expect_invalid_argument([&] {
        (void)poker::calculate_equity(hero_range, poker::HandRange{}, make_board(), exact_options());
    }, "empty villain range should be rejected");

    expect_invalid_argument([&] {
        (void)poker::calculate_equity(poker::HandRange::parse("AA"), villain_range, make_board({"As", "Ah", "Ad", "Ac"}), exact_options());
    }, "fully blocked hero range should be rejected");

    expect_invalid_argument([&] {
        (void)poker::calculate_equity(poker::HandRange::parse("AA"), villain_range, make_board({"Ks", "Kh", "Kd", "Kc"}), exact_options());
    }, "fully blocked villain range should be rejected");
}

void test_ev_calculations() {
    const poker::PotOddsResult odds_100_50 = poker::calculate_pot_odds(100.0, 50.0);
    expect_close(odds_100_50.pot_before_call, 100.0, 1e-12, "pot should round-trip");
    expect_close(odds_100_50.call_amount, 50.0, 1e-12, "call amount should round-trip");
    expect_close(odds_100_50.final_pot, 150.0, 1e-12, "final pot should add the call");
    expect_close(odds_100_50.required_equity, 1.0 / 3.0, 1e-12, "required equity should be call / (pot + call)");
    expect_true(odds_100_50.pot_odds_ratio.has_value(), "pot odds ratio should be present for non-zero calls");
    expect_close(*odds_100_50.pot_odds_ratio, 2.0, 1e-12, "pot odds ratio should be pot / call");

    const poker::PotOddsResult odds_100_100 = poker::calculate_pot_odds(100.0, 100.0);
    expect_close(odds_100_100.required_equity, 0.5, 1e-12, "break-even equity should be 50% when pot equals call");
    expect_close(*odds_100_100.pot_odds_ratio, 1.0, 1e-12, "pot odds ratio should be 1:1 when pot equals call");

    const poker::PotOddsResult odds_200_50 = poker::calculate_pot_odds(200.0, 50.0);
    expect_close(odds_200_50.final_pot, 250.0, 1e-12, "final pot should be 250");
    expect_close(odds_200_50.required_equity, 0.2, 1e-12, "required equity should be 20%");
    expect_close(*odds_200_50.pot_odds_ratio, 4.0, 1e-12, "pot odds ratio should be 4:1");

    expect_close(poker::calculate_break_even_equity(100.0, 50.0), 1.0 / 3.0, 1e-12, "break-even equity should match pot odds");
    expect_close(poker::calculate_call_ev(1.0 / 3.0, 100.0, 50.0), 0.0, 1e-12, "break-even equity should produce zero call EV");
    expect_close(poker::calculate_call_ev(0.40, 100.0, 50.0), 10.0, 1e-12, "40% equity against 100/50 should be +10");
    expect_close(poker::calculate_call_ev(0.30, 100.0, 50.0), -5.0, 1e-12, "30% equity against 100/50 should be -5");
    expect_close(poker::calculate_fold_ev(100.0, 50.0), 0.0, 1e-12, "fold EV should be zero");

    const poker::PotOddsResult zero_call = poker::calculate_pot_odds(100.0, 0.0);
    expect_close(zero_call.final_pot, 100.0, 1e-12, "zero call should leave the pot unchanged");
    expect_close(zero_call.required_equity, 0.0, 1e-12, "zero call should have zero break-even equity");
    expect_true(!zero_call.pot_odds_ratio.has_value(), "zero call should not report a finite pot odds ratio");
    expect_close(poker::calculate_call_ev(0.40, 100.0, 0.0), 40.0, 1e-12, "zero call should preserve the full equity value");

    expect_invalid_argument([&] {
        (void)poker::calculate_pot_odds(-1.0, 50.0);
    }, "negative pot should be rejected");

    expect_invalid_argument([&] {
        (void)poker::calculate_pot_odds(100.0, -1.0);
    }, "negative call should be rejected");

    expect_invalid_argument([&] {
        (void)poker::calculate_call_ev(-0.01, 100.0, 50.0);
    }, "negative equity should be rejected");

    expect_invalid_argument([&] {
        (void)poker::calculate_call_ev(1.01, 100.0, 50.0);
    }, "equity above 1 should be rejected");

    expect_invalid_argument([&] {
        (void)poker::calculate_call_ev(std::numeric_limits<double>::quiet_NaN(), 100.0, 50.0);
    }, "NaN equity should be rejected");

    expect_invalid_argument([&] {
        (void)poker::calculate_pot_odds(std::numeric_limits<double>::infinity(), 50.0);
    }, "infinite pot should be rejected");
}

void test_cli_help_and_usage() {
    expect_cli_success("poker.exe equity --help", {"Usage:", "--hero-range", "--villain-range"});
    expect_cli_success("poker.exe pot-odds --help", {"Usage:", "pot-odds", "--pot", "--call"});
    expect_cli_success("poker.exe ev --help", {"Usage:", "poker ev", "--equity"});
}

void test_cli_specific_hand_vs_range() {
    expect_cli_success(
        "poker.exe equity As Ks --villain-range \"QQ+, AJs+, KQs\" --board Qh 7c 2s --method exact",
        {"Hero: As Ks", "Villain range:", "Method: Exact", "States:"});
}

void test_cli_range_vs_range() {
    expect_cli_success(
        "poker.exe equity --hero-range \"QQ+, AKs\" --villain-range \"JJ+, AQs+\" --board Qh 7c 2s --method montecarlo --simulations 1000 --seed 12345",
        {"Hero range:", "Villain range:", "Method: Monte Carlo", "Seed: 12345"});
}

void test_cli_validation_failures() {
    expect_cli_failure(
        "poker.exe equity As Ks --villain-range \"AKo+\" --board Qh 7c 2s --method exact",
        {"Offsuit plus notation is not supported"});

    expect_cli_failure(
        "poker.exe equity As Ks --villain-range \"\" --board Qh 7c 2s --method exact",
        {"Range notation is empty"});

    expect_cli_failure(
        "poker.exe equity As Ks --villain-range \"AK\" --board As 7c 2s --method exact",
        {"Duplicate cards"});

    expect_cli_failure(
        "poker.exe equity As Ks --villain-range \"AK\" --board Qh --method exact",
        {"Board must contain 0, 3, 4, or 5 cards"});

    expect_cli_failure(
        "poker.exe equity As Ks --hero-range \"AA\" --villain-range \"KK\"",
        {"Positional hero cards cannot be combined with range arguments"});

    expect_cli_failure(
        "poker.exe equity --hero-range \"AA\"",
        {"--hero-range requires --villain-range"});

    expect_cli_failure(
        "poker.exe equity --villain-range \"AK\"",
        {"Specific-hand vs range requires exactly two hero cards"});

    expect_cli_failure(
        "poker.exe equity As Ks --method bogus",
        {"Invalid method"});

    expect_cli_failure(
        "poker.exe equity As Ks --method montecarlo --simulations 0",
        {"--simulations must be greater than 0"});

    expect_cli_failure(
        "poker.exe pot-odds --pot -1 --call 50",
        {"Pot before call must be non-negative and finite"});

    expect_cli_failure(
        "poker.exe ev --pot 100 --call 50 --equity 1.5",
        {"Equity must be between 0 and 1"});
}

void test_cli_pot_odds_and_ev() {
    expect_cli_success(
        "poker.exe pot-odds --pot 100 --call 50",
        {"Pot before call: 100", "Call amount:      50", "Final pot:       150", "Required equity: 33.33%", "Pot odds:        2:1"});

    expect_cli_success(
        "poker.exe ev --pot 100 --call 50 --equity 0.40",
        {"Equity:          40.00%", "Call EV:         +10.00", "Fold EV:         +0.00", "Decision:        CALL"});

    expect_cli_success(
        "poker.exe ev --pot 100 --call 50 --equity 0.30",
        {"Call EV:         -5.00", "Decision:        FOLD"});
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
    if (!run_test("test_cards", test_cards) ||
        !run_test("test_deck", test_deck) ||
        !run_test("test_holdem_representation", test_holdem_representation) ||
        !run_test("test_evaluator", test_evaluator) ||
        !run_test("test_range_basics", test_range_basics) ||
        !run_test("test_range_membership", test_range_membership) ||
        !run_test("test_handcombo_canonicalization", test_handcombo_canonicalization) ||
        !run_test("test_range_invalid_syntax", test_range_invalid_syntax) ||
        !run_test("test_betting_validation_and_legal_actions", test_betting_validation_and_legal_actions) ||
        !run_test("test_game_state_validation", test_game_state_validation) ||
        !run_test("test_game_state_equity_dispatch", test_game_state_equity_dispatch) ||
        !run_test("test_ev_calculations", test_ev_calculations) ||
        !run_test("test_range_equity_smoke_and_card_removal", test_range_equity_smoke_and_card_removal) ||
        !run_test("test_range_vs_range_river_manual", test_range_vs_range_river_manual) ||
        !run_test("test_range_vs_range_aa_kk_exact", test_range_vs_range_aa_kk_exact) ||
        !run_test("test_range_vs_range_ak_overlap_removal", test_range_vs_range_ak_overlap_removal) ||
        !run_test("test_range_vs_range_flop_turn_exact", test_range_vs_range_flop_turn_exact) ||
        !run_test("test_range_vs_range_monte_carlo_cross_validation", test_range_vs_range_monte_carlo_cross_validation) ||
        !run_test("test_range_vs_range_monte_carlo_determinism", test_range_vs_range_monte_carlo_determinism) ||
        !run_test("test_range_vs_range_validation", test_range_vs_range_validation) ||
        !run_test("test_cli_help_and_usage", test_cli_help_and_usage) ||
        !run_test("test_cli_specific_hand_vs_range", test_cli_specific_hand_vs_range) ||
        !run_test("test_cli_range_vs_range", test_cli_range_vs_range) ||
        !run_test("test_cli_validation_failures", test_cli_validation_failures) ||
        !run_test("test_cli_pot_odds_and_ev", test_cli_pot_odds_and_ev) ||
        !run_test("test_range_exact_river_equity", test_range_exact_river_equity) ||
        !run_test("test_range_exact_flop_and_turn_equity", test_range_exact_flop_and_turn_equity) ||
        !run_test("test_range_monte_carlo_cross_validation", test_range_monte_carlo_cross_validation) ||
        !run_test("test_range_monte_carlo_determinism", test_range_monte_carlo_determinism) ||
        !run_test("test_range_empty_after_card_removal", test_range_empty_after_card_removal) ||
        !run_test("test_equity_validation", test_equity_validation) ||
        !run_test("test_equity_correctness", test_equity_correctness) ||
        !run_test("test_equity_sanity", test_equity_sanity) ||
        !run_test("test_exact_validation", test_exact_validation) ||
        !run_test("test_exact_correctness", test_exact_correctness) ||
        !run_test("test_exact_monte_carlo_cross_validation", test_exact_monte_carlo_cross_validation)) {
        return 1;
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All tests passed\n";
    return 0;
}