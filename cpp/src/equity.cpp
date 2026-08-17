#include "poker/equity.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <random>
#include <stdexcept>

#include "poker/evaluator.hpp"

namespace poker {

namespace {

constexpr std::size_t kDeckSize = 52U;
constexpr std::size_t kMaxKnownCards = 7U;
constexpr std::size_t kMaxOpponents = 5U;

std::array<Card, kDeckSize> make_full_deck() noexcept {
    std::array<Card, kDeckSize> deck{};
    for (std::uint8_t index = 0; index < kDeckSize; ++index) {
        deck[index] = Card::from_index(index);
    }
    return deck;
}

bool is_valid_board_size(std::size_t board_count) noexcept {
    return board_count == 0U || board_count == 3U || board_count == 4U || board_count == 5U;
}

void throw_invalid(const char* message) {
    throw std::invalid_argument(message);
}

void validate_hero_hand(const HoldemHand& hero) {
    if (!hero.hole[0].valid() || !hero.hole[1].valid()) {
        throw_invalid("Hero must contain exactly 2 valid cards");
    }

    if (hero.hole[0] == hero.hole[1]) {
        throw_invalid("Hero cards must be unique");
    }

    if (!is_valid_board_size(hero.board_count)) {
        throw_invalid("Board must contain 0, 3, 4, or 5 cards");
    }

    for (std::size_t index = 0; index < hero.board_count; ++index) {
        if (!hero.board[index].valid()) {
            throw_invalid("Board contains an invalid card");
        }
    }

    std::array<Card, kMaxKnownCards> known_cards{};
    known_cards[0] = hero.hole[0];
    known_cards[1] = hero.hole[1];
    for (std::size_t index = 0; index < hero.board_count; ++index) {
        known_cards[2U + index] = hero.board[index];
    }

    for (std::size_t left = 0; left < 2U + hero.board_count; ++left) {
        for (std::size_t right = left + 1U; right < 2U + hero.board_count; ++right) {
            if (known_cards[left] == known_cards[right]) {
                throw_invalid("Duplicate cards are not allowed");
            }
        }
    }
}

void validate_simulation_parameters(std::size_t opponents, std::size_t simulations) {
    if (opponents < 1U || opponents > kMaxOpponents) {
        throw_invalid("Opponents must be between 1 and 5");
    }

    if (simulations == 0U) {
        throw_invalid("Simulations must be greater than 0");
    }
}

std::size_t collect_known_cards(const HoldemHand& hero, std::array<Card, kMaxKnownCards>& known_cards) {
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

Card draw_card(std::array<Card, kDeckSize>& deck, std::size_t& remaining, std::mt19937_64& rng) {
    std::uniform_int_distribution<std::size_t> distribution(0U, remaining - 1U);
    const std::size_t index = distribution(rng);
    const Card drawn = deck[index];
    deck[index] = deck[remaining - 1U];
    --remaining;
    return drawn;
}

}  // namespace

EquityResult simulate_equity(const HoldemHand& hero,
                             std::size_t opponents,
                             std::size_t simulations,
                             std::uint64_t seed) {
    validate_hero_hand(hero);
    validate_simulation_parameters(opponents, simulations);

    std::array<Card, kMaxKnownCards> known_cards{};
    const std::size_t known_count = collect_known_cards(hero, known_cards);
    const std::array<Card, kDeckSize> full_deck = make_full_deck();
    std::mt19937_64 rng(seed);

    std::uint64_t win_count = 0U;
    std::uint64_t tie_count = 0U;
    std::uint64_t loss_count = 0U;
    double equity_sum = 0.0;

    std::array<Card, 5U> board{};
    std::array<Card, 7U> hero_cards{};
    std::array<Card, 7U> opponent_cards{};

    for (std::size_t simulation = 0; simulation < simulations; ++simulation) {
        std::array<Card, kDeckSize> deck{};
        std::size_t remaining = 0U;

        for (std::size_t index = 0U; index < kDeckSize; ++index) {
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