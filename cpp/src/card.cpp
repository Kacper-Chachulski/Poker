#include "poker/card.hpp"

#include <array>
#include <cctype>

namespace poker {

namespace {

constexpr char kRankChars[] = {'0', '0', '2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A'};

std::optional<std::uint8_t> parse_rank(std::string_view text) noexcept {
    if (text == "10") {
        return static_cast<std::uint8_t>(Rank::ten);
    }

    if (text.size() != 1U) {
        return std::nullopt;
    }

    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])))) {
        case '2': return static_cast<std::uint8_t>(Rank::two);
        case '3': return static_cast<std::uint8_t>(Rank::three);
        case '4': return static_cast<std::uint8_t>(Rank::four);
        case '5': return static_cast<std::uint8_t>(Rank::five);
        case '6': return static_cast<std::uint8_t>(Rank::six);
        case '7': return static_cast<std::uint8_t>(Rank::seven);
        case '8': return static_cast<std::uint8_t>(Rank::eight);
        case '9': return static_cast<std::uint8_t>(Rank::nine);
        case 'T': return static_cast<std::uint8_t>(Rank::ten);
        case 'J': return static_cast<std::uint8_t>(Rank::jack);
        case 'Q': return static_cast<std::uint8_t>(Rank::queen);
        case 'K': return static_cast<std::uint8_t>(Rank::king);
        case 'A': return static_cast<std::uint8_t>(Rank::ace);
        default: return std::nullopt;
    }
}

std::optional<std::uint8_t> parse_suit(char text) noexcept {
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(text)))) {
        case 'c': return static_cast<std::uint8_t>(Suit::clubs);
        case 'd': return static_cast<std::uint8_t>(Suit::diamonds);
        case 'h': return static_cast<std::uint8_t>(Suit::hearts);
        case 's': return static_cast<std::uint8_t>(Suit::spades);
        default: return std::nullopt;
    }
}

}  // namespace

std::optional<Card> Card::from_string(std::string_view text) noexcept {
    if (text.size() != 2U && text.size() != 3U) {
        return std::nullopt;
    }

    const std::string_view rank_text = text.substr(0, text.size() - 1U);
    const std::optional<std::uint8_t> rank = parse_rank(rank_text);
    const std::optional<std::uint8_t> suit = parse_suit(text.back());
    if (!rank || !suit) {
        return std::nullopt;
    }

    return Card::make(static_cast<Rank>(*rank), static_cast<Suit>(*suit));
}

std::string Card::to_string() const {
    if (!valid()) {
        return "??";
    }

    const char rank_char = kRankChars[static_cast<std::size_t>(rank())];
    const char suit_char = [&]() {
        switch (suit()) {
            case Suit::clubs: return 'c';
            case Suit::diamonds: return 'd';
            case Suit::hearts: return 'h';
            case Suit::spades: return 's';
        }
        return '?';
    }();

    return std::string{rank_char, suit_char};
}

}  // namespace poker