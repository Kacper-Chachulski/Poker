#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "poker/card.hpp"

namespace poker {

// Supported notation:
// - Pocket pairs: AA, KK, QQ, 22, and QQ+ / 22+.
// - Suited hands: AKs, A5s, QJs, and suited plus notation such as A5s+.
//   Suited plus keeps the high rank fixed and increases the low rank upward until
//   just below the high rank, e.g. A5s+ -> A5s, A6s, ..., AKs.
// - Offsuit hands: AKo, KQo.
// - Any-suit hands: AK, KQ, 76.
// - Comma-separated unions with optional surrounding whitespace.
// Not supported in this stage: bounded ranges using '-' and offsuit/any-suit plus notation.

class RangeParseError : public std::invalid_argument {
public:
    explicit RangeParseError(const std::string& message)
        : std::invalid_argument(message) {}
};

class HandCombo {
public:
    HandCombo() noexcept = default;
    HandCombo(Card first, Card second) noexcept;

    std::array<Card, 2> cards() const noexcept;
    bool valid() const noexcept;

    friend bool operator==(const HandCombo& lhs, const HandCombo& rhs) noexcept;
    friend bool operator!=(const HandCombo& lhs, const HandCombo& rhs) noexcept;

private:
    std::array<Card, 2> cards_{};
};

class HandRange {
public:
    using const_iterator = std::vector<HandCombo>::const_iterator;

    HandRange() = default;

    static HandRange from_combo(const HandCombo& combo);

    static HandRange parse(std::string_view notation);

    std::size_t size() const noexcept;
    bool empty() const noexcept;
    bool contains(const HandCombo& combo) const noexcept;
    bool contains(Card first, Card second) const noexcept;

    const std::vector<HandCombo>& combos() const noexcept;
    const_iterator begin() const noexcept;
    const_iterator end() const noexcept;

private:
    static constexpr std::size_t kComboUniverseSize = 1326U;

    explicit HandRange(std::bitset<kComboUniverseSize> membership);

    std::bitset<kComboUniverseSize> membership_{};
    std::vector<HandCombo> combos_{};
};

std::size_t hash_value(const HandCombo& combo) noexcept;

}  // namespace poker

namespace std {

template <>
struct hash<poker::HandCombo> {
    std::size_t operator()(const poker::HandCombo& combo) const noexcept {
        return poker::hash_value(combo);
    }
};

}  // namespace std