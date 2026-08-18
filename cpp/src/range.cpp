#include "poker/range.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <utility>

namespace poker {

namespace {

constexpr std::size_t kComboUniverseSize = 1326U;
constexpr std::size_t kSuitCount = 4U;
constexpr std::size_t kRankCount = 13U;

constexpr std::array<Suit, kSuitCount> kSuits{Suit::clubs, Suit::diamonds, Suit::hearts, Suit::spades};

struct ParsedToken {
    Rank high_rank{};
    Rank low_rank{};
    bool plus{false};
    bool suited{false};
    bool offsuit{false};
    bool pair{false};
    bool any_suit{false};
};

[[noreturn]] void fail(const std::string& message) {
    throw RangeParseError(message);
}

std::string_view trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string_view::npos) {
        return {};
    }

    const std::size_t last = text.find_last_not_of(" \t\n\r\f\v");
    return text.substr(first, last - first + 1U);
}

std::uint8_t rank_value(Rank rank) noexcept {
    return static_cast<std::uint8_t>(rank);
}

std::optional<Rank> parse_rank(char symbol) noexcept {
    switch (symbol) {
        case '2': return Rank::two;
        case '3': return Rank::three;
        case '4': return Rank::four;
        case '5': return Rank::five;
        case '6': return Rank::six;
        case '7': return Rank::seven;
        case '8': return Rank::eight;
        case '9': return Rank::nine;
        case 'T': return Rank::ten;
        case 'J': return Rank::jack;
        case 'Q': return Rank::queen;
        case 'K': return Rank::king;
        case 'A': return Rank::ace;
        default: return std::nullopt;
    }
}

bool is_valid_combo_card_pair(Card first, Card second) noexcept {
    return first.valid() && second.valid() && first != second;
}

std::size_t combo_index(std::uint8_t first_index, std::uint8_t second_index) noexcept {
    if (first_index > second_index) {
        std::swap(first_index, second_index);
    }

    const std::size_t first = first_index;
    const std::size_t second = second_index;
    return first * 51U - (first * (first - 1U)) / 2U + (second - first - 1U);
}

void add_combo(std::bitset<kComboUniverseSize>& membership, Card first, Card second) {
    if (!is_valid_combo_card_pair(first, second)) {
        fail("Concrete combo must contain two distinct valid cards");
    }

    membership.set(combo_index(first.index(), second.index()));
}

void add_pair_combos(std::bitset<kComboUniverseSize>& membership, Rank rank) {
    for (std::size_t left = 0; left < kSuitCount; ++left) {
        for (std::size_t right = left + 1U; right < kSuitCount; ++right) {
            add_combo(membership,
                      Card::make(rank, kSuits[left]),
                      Card::make(rank, kSuits[right]));
        }
    }
}

void add_suited_combos(std::bitset<kComboUniverseSize>& membership, Rank high, Rank low) {
    for (Suit suit : kSuits) {
        add_combo(membership, Card::make(high, suit), Card::make(low, suit));
    }
}

void add_offsuit_combos(std::bitset<kComboUniverseSize>& membership, Rank high, Rank low) {
    for (Suit high_suit : kSuits) {
        for (Suit low_suit : kSuits) {
            if (high_suit != low_suit) {
                add_combo(membership, Card::make(high, high_suit), Card::make(low, low_suit));
            }
        }
    }
}

void add_any_suit_combos(std::bitset<kComboUniverseSize>& membership, Rank high, Rank low) {
    for (Suit high_suit : kSuits) {
        for (Suit low_suit : kSuits) {
            add_combo(membership, Card::make(high, high_suit), Card::make(low, low_suit));
        }
    }
}

void add_suited_plus(std::bitset<kComboUniverseSize>& membership, Rank high, Rank low) {
    const std::uint8_t high_value = rank_value(high);
    const std::uint8_t low_value = rank_value(low);
    for (std::uint8_t value = low_value; value <= high_value; ++value) {
        add_suited_combos(membership, high, static_cast<Rank>(value));
    }
}

void add_pair_plus(std::bitset<kComboUniverseSize>& membership, Rank low_pair) {
    for (std::uint8_t value = rank_value(low_pair); value <= rank_value(Rank::ace); ++value) {
        add_pair_combos(membership, static_cast<Rank>(value));
    }
}

ParsedToken parse_token(std::string_view token) {
    if (token.empty()) {
        fail("Range token is empty");
    }

    if (token.find('-') != std::string_view::npos) {
        fail(std::string("Bounded ranges using '-' are not supported: ") + std::string(token));
    }

    ParsedToken parsed{};
    if (token.back() == '+') {
        parsed.plus = true;
        token.remove_suffix(1U);
        if (token.empty()) {
            fail("Plus notation is missing its base token");
        }
    }

    if (token.size() == 2U) {
        const std::optional<Rank> first = parse_rank(token[0]);
        const std::optional<Rank> second = parse_rank(token[1]);
        if (!first || !second) {
            fail(std::string("Invalid rank in token: ") + std::string(token));
        }

        if (*first == *second) {
            if (parsed.plus) {
                parsed.pair = true;
                parsed.high_rank = *first;
                parsed.low_rank = *second;
                return parsed;
            }

            parsed.pair = true;
            parsed.high_rank = *first;
            parsed.low_rank = *second;
            return parsed;
        }

        if (parsed.plus) {
            fail(std::string("Plus notation requires a suited base token, such as A5s+: ") + std::string(token));
        }

        if (rank_value(*first) < rank_value(*second)) {
            fail(std::string("Malformed rank ordering in token: ") + std::string(token));
        }

        parsed.any_suit = true;
        parsed.high_rank = *first;
        parsed.low_rank = *second;
        return parsed;
    }

    if (token.size() == 3U) {
        const std::optional<Rank> first = parse_rank(token[0]);
        const std::optional<Rank> second = parse_rank(token[1]);
        const char suffix = token[2];
        if (!first || !second) {
            fail(std::string("Invalid rank in token: ") + std::string(token));
        }

        if (rank_value(*first) <= rank_value(*second)) {
            fail(std::string("Malformed rank ordering in token: ") + std::string(token));
        }

        if (suffix == 's') {
            parsed.suited = true;
        } else if (suffix == 'o') {
            if (parsed.plus) {
                fail(std::string("Offsuit plus notation is not supported: ") + std::string(token));
            }
            parsed.offsuit = true;
        } else {
            fail(std::string("Invalid suited/offsuit suffix in token: ") + std::string(token));
        }

        if (parsed.plus && !parsed.suited) {
            fail(std::string("Plus notation is only supported for suited tokens and pocket pairs: ") + std::string(token));
        }

        parsed.high_rank = *first;
        parsed.low_rank = *second;
        return parsed;
    }

    fail(std::string("Malformed range token: ") + std::string(token));
}

void apply_token(std::bitset<kComboUniverseSize>& membership, std::string_view token) {
    const ParsedToken parsed = parse_token(token);

    if (parsed.pair) {
        if (parsed.plus) {
            add_pair_plus(membership, parsed.low_rank);
        } else {
            add_pair_combos(membership, parsed.high_rank);
        }
        return;
    }

    if (parsed.suited) {
        if (parsed.plus) {
            add_suited_plus(membership, parsed.high_rank, parsed.low_rank);
        } else {
            add_suited_combos(membership, parsed.high_rank, parsed.low_rank);
        }
        return;
    }

    if (parsed.offsuit) {
        add_offsuit_combos(membership, parsed.high_rank, parsed.low_rank);
        return;
    }

    if (parsed.any_suit) {
        add_any_suit_combos(membership, parsed.high_rank, parsed.low_rank);
        return;
    }

    fail(std::string("Unhandled range token: ") + std::string(token));
}

}  // namespace

HandCombo::HandCombo(Card first, Card second) noexcept {
    if (second.index() < first.index()) {
        cards_[0] = second;
        cards_[1] = first;
    } else {
        cards_[0] = first;
        cards_[1] = second;
    }
}

std::array<Card, 2> HandCombo::cards() const noexcept {
    return cards_;
}

bool HandCombo::valid() const noexcept {
    return is_valid_combo_card_pair(cards_[0], cards_[1]);
}

bool operator==(const HandCombo& lhs, const HandCombo& rhs) noexcept {
    return lhs.cards_ == rhs.cards_;
}

bool operator!=(const HandCombo& lhs, const HandCombo& rhs) noexcept {
    return !(lhs == rhs);
}

std::size_t hash_value(const HandCombo& combo) noexcept {
    const std::array<Card, 2> cards = combo.cards();
    return static_cast<std::size_t>(cards[0].index()) * 53U + cards[1].index();
}

HandRange::HandRange(std::bitset<kComboUniverseSize> membership)
    : membership_(std::move(membership)) {
    combos_.reserve(membership_.count());
    for (std::uint8_t first = 0U; first < 51U; ++first) {
        for (std::uint8_t second = static_cast<std::uint8_t>(first + 1U); second < 52U; ++second) {
            const std::size_t index = combo_index(first, second);
            if (membership_.test(index)) {
                combos_.emplace_back(Card::from_index(first), Card::from_index(second));
            }
        }
    }
}

HandRange HandRange::parse(std::string_view notation) {
    std::bitset<kComboUniverseSize> membership{};

    notation = trim(notation);
    if (notation.empty()) {
        fail("Range notation is empty");
    }

    std::size_t start = 0U;
    while (start <= notation.size()) {
        const std::size_t comma = notation.find(',', start);
        const std::string_view raw_token = notation.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        const std::string_view token = trim(raw_token);
        if (token.empty()) {
            fail("Range notation contains an empty token");
        }

        apply_token(membership, token);

        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1U;
    }

    return HandRange(std::move(membership));
}

std::size_t HandRange::size() const noexcept {
    return combos_.size();
}

bool HandRange::empty() const noexcept {
    return combos_.empty();
}

bool HandRange::contains(const HandCombo& combo) const noexcept {
    if (!combo.valid()) {
        return false;
    }

    const std::array<Card, 2> cards = combo.cards();
    return membership_.test(combo_index(cards[0].index(), cards[1].index()));
}

bool HandRange::contains(Card first, Card second) const noexcept {
    return contains(HandCombo(first, second));
}

const std::vector<HandCombo>& HandRange::combos() const noexcept {
    return combos_;
}

HandRange::const_iterator HandRange::begin() const noexcept {
    return combos_.begin();
}

HandRange::const_iterator HandRange::end() const noexcept {
    return combos_.end();
}

}  // namespace poker