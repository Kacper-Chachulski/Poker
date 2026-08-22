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
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "poker/card.hpp"
#include "poker/decision.hpp"
#include "poker/deck.hpp"
#include "poker/game_state.hpp"
#include "poker/ev.hpp"
#include "poker/evaluator.hpp"
#include "poker/hand.hpp"
#include "poker/equity.hpp"
#include "poker/game_session.hpp"
#include "poker/range.hpp"
#include "../src/equity_common.hpp"

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

poker::GameState make_royal_tie_state(std::initializer_list<poker::Opponent> opponents) {
    return make_game_state(poker::Street::river,
                           "2c",
                           "3d",
                           {"As", "Ks", "Qs", "Js", "Ts"},
                           opponents,
                           100.0,
                           50.0,
                           1000.0);
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

std::string normalize_cli_output(const std::string& output) {
    std::istringstream stream(output);
    std::ostringstream normalized{};
    std::string line{};

    while (std::getline(stream, line)) {
        if (line.rfind("Time:", 0U) == 0U || line.rfind("Elapsed:", 0U) == 0U || line.rfind("Speed:", 0U) == 0U) {
            continue;
        }

        normalized << line << '\n';
    }

    return normalized.str();
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

void test_action_order_layer() {
    auto make_order = []() {
        poker::ActionOrderState order{};
        order.player_count = 6U;
        order.button_seat = 0U;
        order.street = poker::Street::preflop;
        order.seats.assign(6U, poker::PlayerStatus::active);
        return order;
    };

    {
        poker::ActionOrderState order = make_order();
        order.player_count = 2U;
        order.seats.assign(2U, poker::PlayerStatus::active);
        expect_true(order.first_to_act().has_value() && *order.first_to_act() == 0U, "heads-up preflop should start with the button/small blind");
        order.street = poker::Street::flop;
        expect_true(order.first_to_act().has_value() && *order.first_to_act() == 1U, "heads-up postflop should start with the big blind");
    }

    {
        poker::ActionOrderState order = make_order();
        expect_true(order.first_to_act().has_value() && *order.first_to_act() == 3U, "six-handed preflop should start left of the big blind");
        order.street = poker::Street::flop;
        expect_true(order.first_to_act().has_value() && *order.first_to_act() == 1U, "six-handed postflop should start left of the button");
        order.street = poker::Street::turn;
        expect_true(order.first_to_act().has_value() && *order.first_to_act() == 1U, "turn should keep postflop first actor order");
        order.street = poker::Street::river;
        expect_true(order.first_to_act().has_value() && *order.first_to_act() == 1U, "river should keep postflop first actor order");
    }

    {
        poker::ActionOrderState order = make_order();
        order.set_status(1U, poker::PlayerStatus::folded);
        expect_true(order.first_to_act().has_value() && *order.first_to_act() == 3U, "small blind folding should skip to the next active seat");
    }

    {
        poker::ActionOrderState order = make_order();
        order.set_status(2U, poker::PlayerStatus::folded);
        expect_true(order.first_to_act().has_value() && *order.first_to_act() == 3U, "big blind folding should skip to the next active seat");
    }

    {
        poker::ActionOrderState order = make_order();
        order.set_status(1U, poker::PlayerStatus::folded);
        order.set_status(2U, poker::PlayerStatus::folded);
        order.set_status(3U, poker::PlayerStatus::folded);
        expect_true(order.first_to_act().has_value() && *order.first_to_act() == 4U, "multiple consecutive folds should still preserve seat order");
    }

    {
        poker::ActionOrderState order = make_order();
        order.set_status(4U, poker::PlayerStatus::all_in);
        expect_true(order.first_to_act().has_value() && *order.first_to_act() == 3U, "all-in players should be skipped when selecting the first actor");
        expect_true(order.next_to_act(3U).has_value() && *order.next_to_act(3U) == 5U, "all-in players should be skipped when advancing to the next actor");
    }

    {
        poker::ActionOrderState order = make_order();
        for (std::size_t seat = 1U; seat < 6U; ++seat) {
            order.set_status(seat, poker::PlayerStatus::folded);
        }
        expect_true(order.hand_over(), "one active player remaining should end the hand");
        expect_true(!order.first_to_act().has_value(), "one active player remaining should not produce a next actor");
    }
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

void test_game_state_mixed_exact_equity() {
    const poker::EquityOptions exact = exact_options();

    const poker::EquityResult specific_random = poker::calculate_equity(
        make_royal_tie_state({make_specific_opponent("4c", "5d"), make_random_opponent()}),
        exact);
    expect_eq(specific_random.evaluated_states, 903U, "specific plus random should enumerate the remaining river combinations");
    expect_close(specific_random.win_probability, 0.0, 1e-12, "royal board should force no wins");
    expect_close(specific_random.tie_probability, 1.0, 1e-12, "royal board should force all ties");
    expect_close(specific_random.loss_probability, 0.0, 1e-12, "royal board should force no losses");
    expect_close(specific_random.equity, 1.0 / 3.0, 1e-12, "royal board should split equity evenly across three players");

    const poker::EquityResult range_random = poker::calculate_equity(
        make_royal_tie_state({make_range_opponent("AK"), make_random_opponent()}),
        exact);
    expect_eq(range_random.evaluated_states, 8127U, "range plus random should enumerate all legal concrete tuples");
    expect_close(range_random.win_probability, 0.0, 1e-12, "royal board should force no wins");
    expect_close(range_random.tie_probability, 1.0, 1e-12, "royal board should force all ties");
    expect_close(range_random.loss_probability, 0.0, 1e-12, "royal board should force no losses");
    expect_close(range_random.equity, 1.0 / 3.0, 1e-12, "royal board should split equity evenly across three players");

    const poker::EquityResult range_range = poker::calculate_equity(
        make_royal_tie_state({make_range_opponent("AA"), make_range_opponent("KK")}),
        exact);
    expect_eq(range_range.evaluated_states, 9U, "AA versus KK should leave 3 by 3 legal concrete pairs on the royal board");
    expect_close(range_range.win_probability, 0.0, 1e-12, "royal board should force no wins");
    expect_close(range_range.tie_probability, 1.0, 1e-12, "royal board should force all ties");
    expect_close(range_range.loss_probability, 0.0, 1e-12, "royal board should force no losses");
    expect_close(range_range.equity, 1.0 / 3.0, 1e-12, "royal board should split equity evenly across three players");

    const poker::EquityResult specific_range_random = poker::calculate_equity(
        make_royal_tie_state({make_specific_opponent("4c", "5d"), make_range_opponent("AA"), make_random_opponent()}),
        exact);
    expect_eq(specific_range_random.evaluated_states, 2460U, "specific plus range plus random should respect card removal across all seats");
    expect_close(specific_range_random.win_probability, 0.0, 1e-12, "royal board should force no wins");
    expect_close(specific_range_random.tie_probability, 1.0, 1e-12, "royal board should force all ties");
    expect_close(specific_range_random.loss_probability, 0.0, 1e-12, "royal board should force no losses");
    expect_close(specific_range_random.equity, 0.25, 1e-12, "royal board should split equity evenly across four players");

    const poker::EquityResult two_random = poker::calculate_equity(
        make_royal_tie_state({make_random_opponent(), make_random_opponent()}),
        exact);
    expect_eq(two_random.evaluated_states, theoretical_exact_states(5U, 2U), "two random opponents should enumerate the full river state space");
    expect_close(two_random.win_probability, 0.0, 1e-12, "royal board should force no wins");
    expect_close(two_random.tie_probability, 1.0, 1e-12, "royal board should force all ties");
    expect_close(two_random.loss_probability, 0.0, 1e-12, "royal board should force no losses");
    expect_close(two_random.equity, 1.0 / 3.0, 1e-12, "royal board should split equity evenly across three players");
}

void test_game_state_mixed_monte_carlo() {
    const poker::GameState mixed_state = make_game_state(poker::Street::river,
                                                         "As",
                                                         "Ks",
                                                         {"Qh", "Jh", "Th", "9s", "2d"},
                                                         {make_specific_opponent("4c", "5d"), make_range_opponent("AA"), make_random_opponent()},
                                                         100.0,
                                                         50.0,
                                                         1000.0);

    const poker::EquityResult exact = poker::calculate_equity(mixed_state, exact_options());
    const poker::EquityResult monte = poker::calculate_equity(mixed_state, monte_carlo_options(20000U, 20260821U));

    expect_true(std::fabs(exact.equity - monte.equity) < 0.03,
                "mixed Monte Carlo should converge toward the exact mixed equity");

    const poker::EquityResult first = poker::calculate_equity(mixed_state, monte_carlo_options(3000U, 424242U));
    const poker::EquityResult second = poker::calculate_equity(mixed_state, monte_carlo_options(3000U, 424242U));
    expect_true(first.win_probability == second.win_probability &&
                first.tie_probability == second.tie_probability &&
                first.loss_probability == second.loss_probability &&
                first.equity == second.equity,
                "mixed Monte Carlo should remain deterministic with a fixed seed");
}

void test_game_state_mixed_validation() {
    expect_invalid_argument([&] {
        (void)poker::calculate_equity(make_game_state(poker::Street::river,
                                                      "As",
                                                      "Ah",
                                                      {"Qd", "Jd", "Td", "2c", "3s"},
                                                      {make_specific_opponent("Ac", "Ad"), make_range_opponent("AA")},
                                                      100.0,
                                                      50.0,
                                                      1000.0),
                                       exact_options());
    }, "fully blocked range should be rejected in mixed GameState equity");

    expect_invalid_argument([&] {
        (void)poker::calculate_equity(make_game_state(poker::Street::river,
                                                      "As",
                                                      "Ks",
                                                      {"Qh", "Jh", "Th", "9s", "2d"},
                                                      {make_random_opponent(),
                                                       make_random_opponent(),
                                                       make_random_opponent(),
                                                       make_random_opponent(),
                                                       make_random_opponent(),
                                                       make_random_opponent()},
                                                      100.0,
                                                      50.0,
                                                      1000.0),
                                       exact_options());
    }, "opponent count should not exceed the table limit");
}

void test_decision_engine() {
    const poker::GameState facing_bet = make_game_state(poker::Street::flop,
                                                        "As",
                                                        "Ks",
                                                        {"Qh", "7c", "2s"},
                                                        {make_random_opponent()},
                                                        100.0,
                                                        50.0,
                                                        500.0,
                                                        50.0,
                                                        false);

    const poker::DecisionResult positive = poker::evaluate_decision(facing_bet, 0.40);
    expect_true(positive.best_action.has_value() && *positive.best_action == poker::BettingAction::call,
                "best action should be call when call EV is positive");
    expect_true(positive.best_ev.has_value(), "best EV should be present when a supported action exists");
    expect_close(*positive.best_ev, 10.0, 1e-12, "best EV should match call EV");
    expect_eq(static_cast<std::uint64_t>(positive.actions.size()), 4U, "facing a bet should produce four legal actions");

    const auto fold_eval = std::find_if(positive.actions.begin(), positive.actions.end(), [](const poker::ActionEvaluation& evaluation) {
        return evaluation.action == poker::BettingAction::fold;
    });
    const auto call_eval = std::find_if(positive.actions.begin(), positive.actions.end(), [](const poker::ActionEvaluation& evaluation) {
        return evaluation.action == poker::BettingAction::call;
    });
    const auto raise_eval = std::find_if(positive.actions.begin(), positive.actions.end(), [](const poker::ActionEvaluation& evaluation) {
        return evaluation.action == poker::BettingAction::raise;
    });
    const auto all_in_eval = std::find_if(positive.actions.begin(), positive.actions.end(), [](const poker::ActionEvaluation& evaluation) {
        return evaluation.action == poker::BettingAction::all_in;
    });

    expect_true(fold_eval != positive.actions.end() && fold_eval->legal && fold_eval->supported && fold_eval->ev.has_value() && *fold_eval->ev == 0.0,
                "fold should be legal, supported, and zero EV");
    expect_true(call_eval != positive.actions.end() && call_eval->legal && call_eval->supported && call_eval->ev.has_value() && std::fabs(*call_eval->ev - 10.0) < 1e-12,
                "call should be legal, supported, and positive EV");
    expect_true(raise_eval != positive.actions.end() && raise_eval->legal && !raise_eval->supported && !raise_eval->ev.has_value(),
                "raise should be legal but unsupported");
    expect_true(all_in_eval != positive.actions.end() && all_in_eval->legal && !all_in_eval->supported && !all_in_eval->ev.has_value(),
                "all-in should be legal but unsupported");

    const poker::DecisionResult break_even = poker::evaluate_decision(facing_bet, 1.0 / 3.0);
    const auto break_even_call = std::find_if(break_even.actions.begin(), break_even.actions.end(), [](const poker::ActionEvaluation& evaluation) {
        return evaluation.action == poker::BettingAction::call;
    });
    expect_true(break_even_call != break_even.actions.end() && break_even_call->ev.has_value() && std::fabs(*break_even_call->ev) < 1e-12,
                "break-even call EV should be approximately zero");

    const poker::DecisionResult negative = poker::evaluate_decision(facing_bet, 0.25);
    expect_true(negative.best_action.has_value() && *negative.best_action == poker::BettingAction::fold,
                "fold should be the best supported action when call EV is negative");
    const auto negative_call = std::find_if(negative.actions.begin(), negative.actions.end(), [](const poker::ActionEvaluation& evaluation) {
        return evaluation.action == poker::BettingAction::call;
    });
    expect_true(negative_call != negative.actions.end() && negative_call->ev.has_value() && *negative_call->ev < 0.0,
                "call EV should be negative below break-even");

    const poker::DecisionResult strong_no_bet = poker::evaluate_decision(poker::BettingState{100.0, 0.0, 500.0, 10.0, true}, 0.72);
    expect_true(strong_no_bet.best_action.has_value() && *strong_no_bet.best_action == poker::BettingAction::bet,
                "strong no-bet equity should recommend betting");
    expect_true(strong_no_bet.heuristic_recommendation && strong_no_bet.suggested_amount.has_value(),
                "bet recommendation should include a suggested amount");
    expect_close(*strong_no_bet.suggested_amount, 66.0, 1e-12, "opening bet should default to 66% of pot");

    const poker::DecisionResult weak_no_bet = poker::evaluate_decision(poker::BettingState{100.0, 0.0, 500.0, 10.0, true}, 0.20);
    expect_true(weak_no_bet.best_action.has_value() && *weak_no_bet.best_action == poker::BettingAction::check,
                "weak no-bet equity should recommend checking");
    expect_true(weak_no_bet.heuristic_recommendation && !weak_no_bet.suggested_amount.has_value(),
                "check recommendation should not invent an amount");

    const poker::BettingState raise_facing_bet{100.0, 20.0, 500.0, 20.0, false};
    const poker::DecisionResult strong_raise = poker::evaluate_decision(raise_facing_bet, 0.75);
    expect_true(strong_raise.best_action.has_value() && *strong_raise.best_action == poker::BettingAction::raise,
                "strong facing-bet equity should recommend raising");
    expect_true(strong_raise.suggested_amount.has_value(), "raise should include a suggested amount");
    expect_close(*strong_raise.suggested_amount, 50.0, 1e-12, "raise sizing should default to 2.5x the call amount");

    const poker::DecisionResult medium_call = poker::evaluate_decision(raise_facing_bet, 0.50);
    expect_true(medium_call.best_action.has_value() && *medium_call.best_action == poker::BettingAction::call,
                "medium facing-bet equity should recommend calling");

    const poker::DecisionResult weak_fold = poker::evaluate_decision(raise_facing_bet, 0.10);
    expect_true(weak_fold.best_action.has_value() && *weak_fold.best_action == poker::BettingAction::fold,
                "weak facing-bet equity should recommend folding");

    const poker::DecisionResult capped_raise = poker::evaluate_decision(poker::BettingState{100.0, 20.0, 45.0, 1.0, false}, 0.90);
    expect_true(capped_raise.best_action.has_value() && *capped_raise.best_action == poker::BettingAction::all_in,
                "raise should cap at the available stack and fall back to all-in");
    expect_true(capped_raise.suggested_amount.has_value() && *capped_raise.suggested_amount == 45.0,
                "capped raise should report the full stack amount");

    const poker::DecisionResult all_in_no_bet = poker::evaluate_decision(poker::BettingState{100.0, 0.0, 40.0, 10.0, true}, 0.80);
    expect_true(all_in_no_bet.best_action.has_value() && *all_in_no_bet.best_action == poker::BettingAction::all_in,
                "strong no-bet short stack should recommend all-in");
    expect_true(all_in_no_bet.suggested_amount.has_value() && *all_in_no_bet.suggested_amount == 40.0,
                "all-in recommendation should expose the full stack amount");

    const poker::DecisionResult no_bet = poker::evaluate_decision(make_game_state(poker::Street::preflop,
                                                                                  "As",
                                                                                  "Ks",
                                                                                  {},
                                                                                  {make_random_opponent()},
                                                                                  100.0,
                                                                                  0.0,
                                                                                  500.0,
                                                                                  10.0,
                                                                                  true),
                                                                  0.40);
    expect_true(no_bet.best_action.has_value() && *no_bet.best_action == poker::BettingAction::check,
                "unsupported-only no-bet states should fall back to checking");
    for (const poker::ActionEvaluation& evaluation : no_bet.actions) {
        expect_true(evaluation.legal && !evaluation.supported && !evaluation.ev.has_value(),
                    "no-bet actions should be legal but unsupported");
    }

    const poker::ActionEvaluation illegal_call = poker::evaluate_action(poker::BettingState{100.0, 0.0, 500.0, 10.0, true}, 0.40, poker::BettingAction::call);
    expect_true(!illegal_call.legal && !illegal_call.supported && !illegal_call.ev.has_value(),
                "illegal actions should not be evaluated");

    const poker::ActionEvaluation illegal_raise = poker::evaluate_action(raise_facing_bet, 0.75, poker::BettingAction::bet);
    expect_true(!illegal_raise.legal && !illegal_raise.supported && !illegal_raise.ev.has_value(),
                "illegal actions should never be recommended or evaluated");

    expect_invalid_argument([&] {
        (void)poker::evaluate_action(facing_bet, 1.5, poker::BettingAction::call);
    }, "invalid equity should be rejected by decision evaluation");

    expect_invalid_argument([&] {
        (void)poker::evaluate_decision(make_game_state(poker::Street::flop,
                                                      "As",
                                                      "Ks",
                                                      {},
                                                      {make_random_opponent()},
                                                      100.0,
                                                      50.0,
                                                      500.0,
                                                      50.0,
                                                      false),
                                       0.40);
    }, "invalid GameState should be rejected by decision evaluation");

    expect_invalid_argument([&] {
        (void)poker::evaluate_decision(poker::BettingState{100.0, -1.0, 500.0, 50.0, false}, 0.40);
    }, "invalid BettingState should be rejected by decision evaluation");
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

void test_range_vs_range_asymmetric_weighting() {
    const poker::HandRange hero_range = poker::HandRange::parse("AK, AQ");
    const poker::HandRange villain_range = poker::HandRange::parse("AA, KK, QQ, JJ");
    const std::vector<poker::Card> board = make_board({"9h", "8d", "7c", "4s", "2d"});

    long double weighted_equity_sum = 0.0L;
    std::uint64_t weighted_legal_pair_count = 0U;

    for (const poker::HandCombo& hero_combo : hero_range) {
        const std::array<poker::Card, 2> cards = hero_combo.cards();
        const poker::HoldemHand hero_hand = make_hand(cards[0].to_string(), cards[1].to_string(), {"9h", "8d", "7c", "4s", "2d"});
        const std::size_t legal_villain_count = count_legal_villain_combos(hero_hand, villain_range);
        if (legal_villain_count == 0U) {
            continue;
        }

        const poker::EquityResult hero_equity = poker::calculate_equity(hero_hand, villain_range, exact_options());
        weighted_equity_sum += static_cast<long double>(hero_equity.equity) * static_cast<long double>(legal_villain_count);
        weighted_legal_pair_count += legal_villain_count;
    }

    const poker::EquityResult result = poker::calculate_equity(hero_range, villain_range, board, exact_options());
    expect_true(weighted_legal_pair_count > 0U, "brute-force reference should have at least one legal ordered pair");
    expect_close(result.equity, static_cast<double>(weighted_equity_sum / static_cast<long double>(weighted_legal_pair_count)), 1e-12,
                "range-vs-range exact equity should weight hero combos by their legal villain counts");
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
    expect_cli_success("poker.exe decision --help", {"Usage:", "poker decision", "--opponents", "--call"});
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

void test_cli_decision_command() {
    expect_cli_success(
        "poker.exe decision As Ks --board Qh 7c 2s --opponents 1 --pot 100 --call 50 --method exact",
        {"Hero: As Ks", "Board: Qh 7c 2s", "Opponents: 1", "Method: Exact", "Equity:", "Required equity:", "Actions:", "Recommendation: CALL"});

    expect_cli_success(
        "poker.exe decision 2c 3d --board Ah Kd Qs --opponents 1 --pot 5 --call 20 --method exact",
        {"Hero: 2c 3d", "Board: Ah Kd Qs", "Opponents: 1", "Method: Exact", "Recommendation: FOLD"});

    expect_cli_success(
        "poker.exe decision As Ad --board Qh 7c 2s --opponents 1 --pot 100 --call 0 --stack 200 --method exact",
        {"Recommendation: BET", "Suggested amount:", "Reason: Strong equity"});

    const CliRunResult first = run_cli_command("poker.exe decision As Ks --board Qh 7c 2s --opponents 2 --pot 100 --call 50 --method montecarlo --simulations 1000 --seed 12345");
    const CliRunResult second = run_cli_command("poker.exe decision As Ks --board Qh 7c 2s --opponents 2 --pot 100 --call 50 --method montecarlo --simulations 1000 --seed 12345");
    expect_eq(static_cast<std::uint64_t>(first.exit_code), 0U, "first deterministic decision CLI run should succeed");
    expect_eq(static_cast<std::uint64_t>(second.exit_code), 0U, "second deterministic decision CLI run should succeed");
    expect_true(normalize_cli_output(first.output) == normalize_cli_output(second.output),
                "decision CLI Monte Carlo output should be deterministic with a fixed seed");
}

void test_cli_analyze_command() {
    expect_cli_success(
        "poker.exe analyze As Ks --board Qh 7c 2s --opponents 1 --pot 100 --call 50 --stack 500 --method exact",
        {"Hero: As Ks", "Board: Qh 7c 2s", "Opponents: 1", "Method: Exact", "Win:", "Pot before call:", "Call EV:", "Recommendation: CALL"});

    expect_cli_success(
        "poker.exe analyze 2c 3d --board Ah Kd Qs --opponents 1 --pot 5 --call 20 --stack 500 --method exact",
        {"Recommendation: FOLD"});

    expect_cli_success(
        "poker.exe analyze As Ad --board Qh 7c 2s --opponents 1 --pot 100 --call 0 --stack 200 --method exact",
        {"Recommendation: BET", "Suggested amount:", "Reason: Strong equity"});

    expect_cli_success(
        "poker.exe analyze 2c 3d --board Ah Kd Qs --opponents 1 --pot 5 --call 0 --stack 200 --method exact",
        {"Recommendation: CHECK"});

    const CliRunResult first = run_cli_command("poker.exe analyze As Ks --board Qh 7c 2s --opponents 2 --pot 100 --call 50 --method montecarlo --simulations 1000 --seed 12345");
    const CliRunResult second = run_cli_command("poker.exe analyze As Ks --board Qh 7c 2s --opponents 2 --pot 100 --call 50 --method montecarlo --simulations 1000 --seed 12345");
    expect_eq(static_cast<std::uint64_t>(first.exit_code), 0U, "first deterministic analyze CLI run should succeed");
    expect_eq(static_cast<std::uint64_t>(second.exit_code), 0U, "second deterministic analyze CLI run should succeed");
    expect_true(normalize_cli_output(first.output) == normalize_cli_output(second.output),
                "analyze CLI Monte Carlo output should be deterministic with a fixed seed");
}

void test_cli_analyze_validation_failures() {
    expect_cli_failure(
        "poker.exe analyze As Ks --opponents 1 --pot 100",
        {"--call is required"});

    expect_cli_failure(
        "poker.exe analyze As Ks --opponents 1 --call 50",
        {"--pot is required"});

    expect_cli_failure(
        "poker.exe analyze As Ks --opponents 1 --pot 100 --call 50 --method bogus",
        {"Invalid method"});
}

void test_exact_state_limit_helpers() {
    expect_true(poker::detail::exact_equity_allowed(poker::detail::kMaxExactStates), "10,000,000 states should be allowed");
    expect_true(!poker::detail::exact_equity_allowed(poker::detail::kMaxExactStates + 1U),
                "10,000,001 states should be rejected");

    const std::uint64_t below_limit_states = poker::theoretical_exact_states(make_hand("As", "Ks", {"Qh", "7c", "2s"}), 1U);
    expect_true(below_limit_states < poker::detail::kMaxExactStates, "known below-limit case should stay under the safety limit");

    expect_no_throw([&] {
        (void)poker::calculate_equity(make_hand("As", "Ks", {"Qh", "7c", "2s"}), 1U, exact_options());
    }, "below-limit exact equity should still execute");
}

void test_cli_exact_state_limit_selection() {
    expect_cli_success(
        "poker.exe equity As Ks --board Qh 7c 2s --opponents 1",
        {"Method: Exact", "States:"});

    expect_cli_success(
        "poker.exe equity As Ks --opponents 5 --simulations 1000 --seed 12345",
        {"Method: Monte Carlo", "Reason: Exact enumeration would require", "states (> 10,000,000 limit)"});

    expect_cli_success(
        "poker.exe decision As Ks --opponents 5 --pot 5 --call 3 --simulations 1000 --seed 12345",
        {"Method: Monte Carlo", "Reason: Exact enumeration would require", "Recommendation:"});
}

void test_cli_exact_state_limit_rejection() {
    expect_cli_failure(
        "poker.exe equity As Ks --opponents 5 --method exact",
        {"Exact equity was refused", "Use Monte Carlo instead"});

    expect_cli_failure(
        "poker.exe decision As Ks --opponents 5 --pot 5 --call 3 --method exact",
        {"Exact equity was refused", "Use Monte Carlo instead"});
}

void test_game_session_helpers() {
    expect_true(poker::parse_table_position("UTG") == poker::TablePosition::utg, "UTG should parse");
    expect_true(poker::parse_table_position("btn") == poker::TablePosition::btn, "BTN should parse case-insensitively");

    poker::GameSessionConfig config{};
    config.hero_cards = {card("As"), card("Ks")};
    config.opponents = 2U;
    config.small_blind = 1.0;
    config.big_blind = 2.0;
    config.hero_position = 4U;
    config.starting_stack = 100.0;

    poker::GameSession session(config);
    const poker::GameState state = session.current_game_state();
    expect_eq(static_cast<std::uint64_t>(state.player_count), 3U, "session state should report hero plus opponents");
    expect_eq(static_cast<std::uint64_t>(state.opponents.size()), 2U, "session state should expose random opponents");
    expect_close(state.betting.hero_stack, 100.0, 1e-12, "starting stack should be reflected in the game state");
}

void test_game_session_reprompts_with_fresh_setup() {
    std::istringstream input(
        "As Ks\n"
        "2\n"
        "1\n"
        "2\n"
        "4\n"
        "100\n"
        "f\n"
        "y\n"
        "Ah Qh\n"
        "1\n"
        "0.25\n"
        "0.5\n"
        "4\n"
        "250\n"
        "f\n"
        "n\n");
    std::ostringstream output;

    expect_no_throw([&] {
        poker::run_game_session(input, output);
    }, "play session should accept two independent hand setups");

    const std::string transcript = output.str();
    expect_true(transcript.find("Hero hole cards (e.g. As Kd): ") != std::string::npos, "first setup prompt should appear");
    expect_true(transcript.find("Start another hand? (y/n): ") != std::string::npos, "repeat-hand prompt should appear");
    expect_true(transcript.find("Hero cards: As Ks") != std::string::npos, "first hand should use first setup");
    expect_true(transcript.find("Hero cards: Ah Qh") != std::string::npos, "second hand should use second setup");
    expect_true(transcript.find("Opponents: 2") != std::string::npos, "first hand opponent count should appear");
    expect_true(transcript.find("Opponents: 1") != std::string::npos, "second hand opponent count should appear");
    expect_true(transcript.find("SB: 1") != std::string::npos, "first hand blinds should appear");
    expect_true(transcript.find("SB: 0.25") != std::string::npos, "second hand blinds should appear");
    expect_true(transcript.find("BB: 2") != std::string::npos, "first hand big blind should appear");
    expect_true(transcript.find("BB: 0.5") != std::string::npos, "second hand big blind should appear");
    expect_true(transcript.find("Position: 4") != std::string::npos, "first hand position should appear");
    expect_true(transcript.find("Position: 4") != std::string::npos, "second hand position should appear");
    expect_true(transcript.find("Starting stack: 100") != std::string::npos, "first hand stack should appear");
    expect_true(transcript.find("Starting stack: 250") != std::string::npos, "second hand stack should appear");
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
        !run_test("test_action_order_layer", test_action_order_layer) ||
        !run_test("test_game_state_equity_dispatch", test_game_state_equity_dispatch) ||
        !run_test("test_game_state_mixed_exact_equity", test_game_state_mixed_exact_equity) ||
        !run_test("test_game_state_mixed_monte_carlo", test_game_state_mixed_monte_carlo) ||
        !run_test("test_game_state_mixed_validation", test_game_state_mixed_validation) ||
        !run_test("test_decision_engine", test_decision_engine) ||
        !run_test("test_ev_calculations", test_ev_calculations) ||
        !run_test("test_range_equity_smoke_and_card_removal", test_range_equity_smoke_and_card_removal) ||
        !run_test("test_range_vs_range_river_manual", test_range_vs_range_river_manual) ||
        !run_test("test_range_vs_range_aa_kk_exact", test_range_vs_range_aa_kk_exact) ||
        !run_test("test_range_vs_range_ak_overlap_removal", test_range_vs_range_ak_overlap_removal) ||
        !run_test("test_range_vs_range_flop_turn_exact", test_range_vs_range_flop_turn_exact) ||
        !run_test("test_range_vs_range_monte_carlo_cross_validation", test_range_vs_range_monte_carlo_cross_validation) ||
        !run_test("test_range_vs_range_monte_carlo_determinism", test_range_vs_range_monte_carlo_determinism) ||
        !run_test("test_range_vs_range_asymmetric_weighting", test_range_vs_range_asymmetric_weighting) ||
        !run_test("test_range_vs_range_validation", test_range_vs_range_validation) ||
        !run_test("test_cli_help_and_usage", test_cli_help_and_usage) ||
        !run_test("test_cli_specific_hand_vs_range", test_cli_specific_hand_vs_range) ||
        !run_test("test_cli_range_vs_range", test_cli_range_vs_range) ||
        !run_test("test_cli_validation_failures", test_cli_validation_failures) ||
        !run_test("test_cli_pot_odds_and_ev", test_cli_pot_odds_and_ev) ||
        !run_test("test_cli_decision_command", test_cli_decision_command) ||
        !run_test("test_cli_analyze_command", test_cli_analyze_command) ||
        !run_test("test_cli_analyze_validation_failures", test_cli_analyze_validation_failures) ||
        !run_test("test_game_session_helpers", test_game_session_helpers) ||
        !run_test("test_game_session_reprompts_with_fresh_setup", test_game_session_reprompts_with_fresh_setup) ||
        !run_test("test_exact_state_limit_helpers", test_exact_state_limit_helpers) ||
        !run_test("test_cli_exact_state_limit_selection", test_cli_exact_state_limit_selection) ||
        !run_test("test_cli_exact_state_limit_rejection", test_cli_exact_state_limit_rejection) ||
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