#include "poker/exact_equity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include "poker/evaluator.hpp"
#include "equity_common.hpp"

namespace poker {

namespace {

struct Counts {
    std::uint64_t wins{0U};
    std::uint64_t ties{0U};
    std::uint64_t losses{0U};
    long double equity_sum{0.0L};
    std::uint64_t states{0U};
};

std::array<Card, detail::kDeckSize> make_full_deck() noexcept {
    std::array<Card, detail::kDeckSize> deck{};
    for (std::uint8_t index = 0; index < detail::kDeckSize; ++index) {
        deck[index] = Card::from_index(index);
    }
    return deck;
}

void remove_card(std::array<Card, detail::kDeckSize>& deck, std::size_t& deck_count, Card card) {
    for (std::size_t index = 0; index < deck_count; ++index) {
        if (deck[index] == card) {
            deck[index] = deck[deck_count - 1U];
            --deck_count;
            return;
        }
    }
    detail::throw_invalid("Known card not found in deck");
}

void evaluate_leaf(const HoldemHand& hero,
                   const std::array<Card, 5>& board,
                   const std::array<std::array<Card, 2>, detail::kMaxOpponents>& opponent_holes,
                   std::size_t opponents,
                   Counts& counts) {
    std::array<Card, 7> hero_cards{};
    hero_cards[0] = hero.hole[0];
    hero_cards[1] = hero.hole[1];
    for (std::size_t index = 0; index < 5U; ++index) {
        hero_cards[2U + index] = board[index];
    }

    const HandValue hero_value = evaluate(hero_cards, hero_cards.size());
    HandValue best_value = hero_value;
    bool hero_is_best = true;
    std::size_t winner_count = 1U;

    std::array<Card, 7> opponent_cards{};
    for (std::size_t opponent = 0; opponent < opponents; ++opponent) {
        opponent_cards[0] = opponent_holes[opponent][0];
        opponent_cards[1] = opponent_holes[opponent][1];
        for (std::size_t index = 0; index < 5U; ++index) {
            opponent_cards[2U + index] = board[index];
        }

        const HandValue opponent_value = evaluate(opponent_cards, opponent_cards.size());
        if (opponent_value > best_value) {
            best_value = opponent_value;
            hero_is_best = false;
            winner_count = 1U;
        } else if (opponent_value == best_value) {
            ++winner_count;
        }
    }

    if (hero_is_best) {
        if (winner_count == 1U) {
            ++counts.wins;
            counts.equity_sum += 1.0L;
        } else {
            ++counts.ties;
            counts.equity_sum += 1.0L / static_cast<long double>(winner_count);
        }
    } else {
        ++counts.losses;
    }

    ++counts.states;
}

void enumerate_opponents(std::array<Card, detail::kDeckSize>& deck,
                         std::size_t deck_count,
                         std::size_t opponent_index,
                         std::size_t opponents,
                         const std::array<Card, 5>& board,
                         std::array<std::array<Card, 2>, detail::kMaxOpponents>& opponent_holes,
                         Counts& counts,
                         const HoldemHand& hero) {
    if (opponent_index == opponents) {
        evaluate_leaf(hero, board, opponent_holes, opponents, counts);
        return;
    }

    for (std::size_t first = 0; first + 1U < deck_count; ++first) {
        const Card first_card = deck[first];
        std::swap(deck[first], deck[deck_count - 1U]);
        --deck_count;

        for (std::size_t second = first; second < deck_count; ++second) {
            const Card second_card = deck[second];
            std::swap(deck[second], deck[deck_count - 1U]);
            --deck_count;

            opponent_holes[opponent_index][0] = first_card;
            opponent_holes[opponent_index][1] = second_card;

            enumerate_opponents(deck,
                                deck_count,
                                opponent_index + 1U,
                                opponents,
                                board,
                                opponent_holes,
                                counts,
                                hero);

            ++deck_count;
            std::swap(deck[second], deck[deck_count - 1U]);
        }

        ++deck_count;
        std::swap(deck[first], deck[deck_count - 1U]);
    }
}

void enumerate_board(std::array<Card, detail::kDeckSize>& deck,
                     std::size_t deck_count,
                     std::size_t board_known,
                     std::size_t board_filled,
                     std::size_t missing_board,
                     std::size_t start_index,
                     std::size_t opponents,
                     std::array<Card, 5>& board,
                     std::array<std::array<Card, 2>, detail::kMaxOpponents>& opponent_holes,
                     Counts& counts,
                     const HoldemHand& hero) {
    if (missing_board == 0U) {
        enumerate_opponents(deck, deck_count, 0U, opponents, board, opponent_holes, counts, hero);
        return;
    }

    for (std::size_t index = start_index; index + missing_board <= deck_count; ++index) {
        const Card chosen = deck[index];
        std::swap(deck[index], deck[deck_count - 1U]);
        --deck_count;

        board[board_known + board_filled] = chosen;
        enumerate_board(deck,
                        deck_count,
                        board_known,
                        board_filled + 1U,
                        missing_board - 1U,
                        index,
                        opponents,
                        board,
                        opponent_holes,
                        counts,
                        hero);

        ++deck_count;
        std::swap(deck[index], deck[deck_count - 1U]);
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

std::uint64_t theoretical_states(std::size_t hero_board_count, std::size_t opponents) {
    const std::size_t missing_board = 5U - hero_board_count;
    std::uint64_t remaining = 52U - 2U - hero_board_count;
    const std::uint64_t board_states = binomial(remaining, missing_board);
    remaining -= missing_board;

    std::uint64_t opponent_states = 1U;
    for (std::size_t opponent = 0; opponent < opponents; ++opponent) {
        opponent_states *= binomial(remaining, 2U);
        remaining -= 2U;
    }

    return board_states * opponent_states;
}

}  // namespace

EquityResult solve_exact_equity(const HoldemHand& hero, std::size_t opponents) {
    detail::validate_equity_request(hero, opponents);

    std::array<Card, detail::kDeckSize> deck = make_full_deck();
    std::size_t deck_count = detail::kDeckSize;
    remove_card(deck, deck_count, hero.hole[0]);
    remove_card(deck, deck_count, hero.hole[1]);

    std::array<Card, 5> board{};
    for (std::size_t index = 0; index < hero.board_count; ++index) {
        board[index] = hero.board[index];
        remove_card(deck, deck_count, hero.board[index]);
    }

    std::array<std::array<Card, 2>, detail::kMaxOpponents> opponent_holes{};
    Counts counts{};

    enumerate_board(deck,
                    deck_count,
                    hero.board_count,
                    0U,
                    5U - hero.board_count,
                    0U,
                    opponents,
                    board,
                    opponent_holes,
                    counts,
                    hero);

    const std::uint64_t expected_states = theoretical_states(hero.board_count, opponents);
    if (counts.states != expected_states) {
        throw std::runtime_error("Exact solver state count mismatch");
    }

    EquityResult result{};
    result.win_probability = static_cast<double>(counts.wins) / static_cast<double>(counts.states);
    result.tie_probability = static_cast<double>(counts.ties) / static_cast<double>(counts.states);
    result.loss_probability = static_cast<double>(counts.losses) / static_cast<double>(counts.states);
    result.equity = static_cast<double>(counts.equity_sum / static_cast<long double>(counts.states));
    result.evaluated_states = counts.states;
    return result;
}

}  // namespace poker
