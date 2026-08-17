#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace poker {

enum class Suit : std::uint8_t {
    clubs = 0,
    diamonds = 1,
    hearts = 2,
    spades = 3,
};

enum class Rank : std::uint8_t {
    two = 2,
    three = 3,
    four = 4,
    five = 5,
    six = 6,
    seven = 7,
    eight = 8,
    nine = 9,
    ten = 10,
    jack = 11,
    queen = 12,
    king = 13,
    ace = 14,
};

class Card {
public:
    constexpr Card() noexcept : index_(kInvalidIndex) {}

    static constexpr Card from_index(std::uint8_t index) noexcept {
        return Card(index);
    }

    static constexpr Card make(Rank rank, Suit suit) noexcept {
        return Card(static_cast<std::uint8_t>(static_cast<std::uint8_t>(suit) * 13U +
                                             static_cast<std::uint8_t>(rank) - 2U));
    }

    static std::optional<Card> from_string(std::string_view text) noexcept;

    constexpr bool valid() const noexcept {
        return index_ < kDeckSize;
    }

    constexpr std::uint8_t index() const noexcept {
        return index_;
    }

    constexpr Rank rank() const noexcept {
        return static_cast<Rank>(index_ % 13U + 2U);
    }

    constexpr Suit suit() const noexcept {
        return static_cast<Suit>(index_ / 13U);
    }

    std::string to_string() const;

    friend constexpr bool operator==(Card lhs, Card rhs) noexcept {
        return lhs.index_ == rhs.index_;
    }

    friend constexpr bool operator!=(Card lhs, Card rhs) noexcept {
        return !(lhs == rhs);
    }

    friend constexpr bool operator<(Card lhs, Card rhs) noexcept {
        return lhs.index_ < rhs.index_;
    }

private:
    static constexpr std::uint8_t kDeckSize = 52U;
    static constexpr std::uint8_t kInvalidIndex = 0xFFU;

    explicit constexpr Card(std::uint8_t index) noexcept : index_(index) {}

    std::uint8_t index_;
};

struct CardHash {
    std::size_t operator()(Card card) const noexcept {
        return card.index();
    }
};

}  // namespace poker

namespace std {

template <>
struct hash<poker::Card> {
    std::size_t operator()(poker::Card card) const noexcept {
        return poker::CardHash{}(card);
    }
};

}  // namespace std