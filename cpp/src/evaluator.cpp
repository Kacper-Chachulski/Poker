#include "poker/evaluator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace poker {

namespace {

constexpr std::uint8_t kMaxRank = 14U;
constexpr std::uint32_t kCategoryShift = 24U;
constexpr std::uint32_t kNibbleShift0 = 20U;
constexpr std::uint32_t kNibbleShift1 = 16U;
constexpr std::uint32_t kNibbleShift2 = 12U;
constexpr std::uint32_t kNibbleShift3 = 8U;
constexpr std::uint32_t kNibbleShift4 = 4U;

constexpr std::uint32_t pack_score(HandCategory category,
                                   std::array<std::uint8_t, 5> ranks,
                                   std::size_t count) noexcept {
    std::uint32_t score = static_cast<std::uint32_t>(category) << kCategoryShift;
    if (count > 0U) score |= static_cast<std::uint32_t>(ranks[0] & 0x0FU) << kNibbleShift0;
    if (count > 1U) score |= static_cast<std::uint32_t>(ranks[1] & 0x0FU) << kNibbleShift1;
    if (count > 2U) score |= static_cast<std::uint32_t>(ranks[2] & 0x0FU) << kNibbleShift2;
    if (count > 3U) score |= static_cast<std::uint32_t>(ranks[3] & 0x0FU) << kNibbleShift3;
    if (count > 4U) score |= static_cast<std::uint32_t>(ranks[4] & 0x0FU) << kNibbleShift4;
    return score;
}

std::uint16_t straight_mask(std::uint16_t mask) noexcept {
    if ((mask & (1U << 14U)) != 0U) {
        mask |= 1U << 1U;
    }
    return mask;
}

std::uint8_t detect_straight(std::uint16_t mask) noexcept {
    mask = straight_mask(mask);
    for (int high = 14; high >= 5; --high) {
        const std::uint16_t sequence = static_cast<std::uint16_t>(0x1FU << (high - 4));
        if ((mask & sequence) == sequence) {
            return static_cast<std::uint8_t>(high);
        }
    }
    return 0U;
}

void append_rank(std::array<std::uint8_t, 5>& output, std::size_t& position, std::uint8_t rank) noexcept {
    output[position++] = rank;
}

template <typename Predicate>
void collect_top_ranks(const std::array<std::uint8_t, kMaxRank + 1U>& counts,
                       std::array<std::uint8_t, 5>& output,
                       std::size_t& position,
                       Predicate predicate,
                       std::size_t target_count) noexcept {
    for (int rank = 14; rank >= 2 && position < target_count; --rank) {
        if (predicate(counts[static_cast<std::size_t>(rank)], rank)) {
            append_rank(output, position, static_cast<std::uint8_t>(rank));
        }
    }
}

std::array<std::uint8_t, 5> top_five_from_suit(const Card* cards,
                                               std::size_t count,
                                               Suit suit) noexcept {
    std::array<std::uint8_t, 5> ranks{};
    std::size_t position = 0U;
    for (int rank = 14; rank >= 2 && position < 5U; --rank) {
        for (std::size_t index = 0; index < count && position < 5U; ++index) {
            if (cards[index].suit() == suit && static_cast<int>(cards[index].rank()) == rank) {
                append_rank(ranks, position, static_cast<std::uint8_t>(rank));
            }
        }
    }
    return ranks;
}

std::uint16_t ranks_in_suit_mask(const Card* cards, std::size_t count, Suit suit) noexcept {
    std::uint16_t mask = 0U;
    for (std::size_t index = 0; index < count; ++index) {
        if (cards[index].suit() == suit) {
            mask |= static_cast<std::uint16_t>(1U << static_cast<std::uint8_t>(cards[index].rank()));
        }
    }
    return mask;
}

bool has_flush(const std::array<std::uint8_t, 4>& suit_counts, Suit& suit_out) noexcept {
    for (std::size_t index = 0; index < suit_counts.size(); ++index) {
        if (suit_counts[index] >= 5U) {
            suit_out = static_cast<Suit>(index);
            return true;
        }
    }
    return false;
}

std::array<std::uint8_t, 5> best_kickers(const std::array<std::uint8_t, kMaxRank + 1U>& counts,
                                         std::uint8_t exclude1,
                                         std::uint8_t exclude2,
                                         std::uint8_t exclude3) noexcept {
    std::array<std::uint8_t, 5> kickers{};
    std::size_t position = 0U;
    for (int rank = 14; rank >= 2 && position < 5U; --rank) {
        const std::uint8_t current = static_cast<std::uint8_t>(rank);
        if (current == exclude1 || current == exclude2 || current == exclude3) {
            continue;
        }
        for (std::uint8_t repeat = 0; repeat < counts[static_cast<std::size_t>(rank)] && position < 5U; ++repeat) {
            kickers[position++] = current;
        }
    }
    return kickers;
}

std::uint8_t highest_pair(const std::array<std::uint8_t, kMaxRank + 1U>& counts,
                          std::uint8_t exclude) noexcept {
    for (int rank = 14; rank >= 2; --rank) {
        if (static_cast<std::uint8_t>(rank) != exclude && counts[static_cast<std::size_t>(rank)] >= 2U) {
            return static_cast<std::uint8_t>(rank);
        }
    }
    return 0U;
}

}  // namespace

HandValue evaluate(const Card* cards, std::size_t count) {
    std::array<std::uint8_t, kMaxRank + 1U> rank_counts{};
    std::array<std::uint8_t, 4U> suit_counts{};
    std::uint16_t rank_mask = 0U;

    for (std::size_t index = 0; index < count; ++index) {
        const std::uint8_t rank = static_cast<std::uint8_t>(cards[index].rank());
        const std::uint8_t suit = static_cast<std::uint8_t>(cards[index].suit());
        ++rank_counts[rank];
        ++suit_counts[suit];
        rank_mask |= static_cast<std::uint16_t>(1U << rank);
    }

    Suit flush_suit = Suit::clubs;
    const bool has_flush_hand = has_flush(suit_counts, flush_suit);
    if (has_flush_hand) {
        const std::uint16_t flush_mask = ranks_in_suit_mask(cards, count, flush_suit);
        const std::uint8_t straight_flush_high = detect_straight(flush_mask);
        if (straight_flush_high != 0U) {
            return {pack_score(HandCategory::straight_flush, {straight_flush_high, 0U, 0U, 0U, 0U}, 1U)};
        }
    }

    std::uint8_t four_rank = 0U;
    std::uint8_t triple_ranks[3] = {};
    std::size_t triple_count = 0U;
    std::uint8_t pair_ranks[4] = {};
    std::size_t pair_count = 0U;

    for (int rank = 14; rank >= 2; --rank) {
        const std::uint8_t count_at_rank = rank_counts[static_cast<std::size_t>(rank)];
        if (count_at_rank == 4U && four_rank == 0U) {
            four_rank = static_cast<std::uint8_t>(rank);
        }
        if (count_at_rank >= 3U && triple_count < 3U) {
            triple_ranks[triple_count++] = static_cast<std::uint8_t>(rank);
        }
        if (count_at_rank >= 2U && pair_count < 4U) {
            pair_ranks[pair_count++] = static_cast<std::uint8_t>(rank);
        }
    }

    if (four_rank != 0U) {
        const std::array<std::uint8_t, 5> kickers = best_kickers(rank_counts, four_rank, 0U, 0U);
        return {pack_score(HandCategory::four_of_a_kind, {four_rank, kickers[0], 0U, 0U, 0U}, 2U)};
    }

    if (triple_count > 0U) {
        const std::uint8_t pair_rank = (triple_count >= 2U) ? triple_ranks[1] : highest_pair(rank_counts, triple_ranks[0]);
        if (pair_rank != 0U) {
            return {pack_score(HandCategory::full_house, {triple_ranks[0], pair_rank, 0U, 0U, 0U}, 2U)};
        }
    }

    if (has_flush_hand) {
        return {pack_score(HandCategory::flush, top_five_from_suit(cards, count, flush_suit), 5U)};
    }

    const std::uint8_t straight_high = detect_straight(rank_mask);
    if (straight_high != 0U) {
        return {pack_score(HandCategory::straight, {straight_high, 0U, 0U, 0U, 0U}, 1U)};
    }

    if (triple_count > 0U) {
        const std::array<std::uint8_t, 5> kickers = best_kickers(rank_counts, triple_ranks[0], 0U, 0U);
        return {pack_score(HandCategory::three_of_a_kind, {triple_ranks[0], kickers[0], kickers[1], 0U, 0U}, 3U)};
    }

    if (pair_count >= 2U) {
        const std::array<std::uint8_t, 5> kickers = best_kickers(rank_counts, pair_ranks[0], pair_ranks[1], 0U);
        return {pack_score(HandCategory::two_pair, {pair_ranks[0], pair_ranks[1], kickers[0], 0U, 0U}, 3U)};
    }

    if (pair_count == 1U) {
        const std::array<std::uint8_t, 5> kickers = best_kickers(rank_counts, pair_ranks[0], 0U, 0U);
        return {pack_score(HandCategory::one_pair, {pair_ranks[0], kickers[0], kickers[1], kickers[2], 0U}, 4U)};
    }

    std::array<std::uint8_t, 5> high_cards{};
    std::size_t position = 0U;
    for (int rank = 14; rank >= 2 && position < 5U; --rank) {
        for (std::uint8_t repeat = 0; repeat < rank_counts[static_cast<std::size_t>(rank)] && position < 5U; ++repeat) {
            high_cards[position++] = static_cast<std::uint8_t>(rank);
        }
    }

    return {pack_score(HandCategory::high_card, high_cards, 5U)};
}

}  // namespace poker