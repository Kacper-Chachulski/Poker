#include "poker/deck.hpp"

#include <algorithm>
#include <stdexcept>

namespace poker {

Deck::Deck() {
    reset();
}

void Deck::reset() noexcept {
    for (std::size_t index = 0; index < cards_.size(); ++index) {
        cards_[index] = Card::from_index(static_cast<std::uint8_t>(index));
    }
    remaining_ = cards_.size();
}

bool Deck::draw(Card& out) noexcept {
    if (remaining_ == 0U) {
        return false;
    }

    out = cards_[--remaining_];
    return true;
}

Card Deck::draw_unchecked() noexcept {
    return cards_[--remaining_];
}

std::size_t Deck::remaining() const noexcept {
    return remaining_;
}

const Card* Deck::data() const noexcept {
    return cards_.data();
}

const Card* Deck::remaining_begin() const noexcept {
    return cards_.data();
}

const Card* Deck::remaining_end() const noexcept {
    return cards_.data() + remaining_;
}

const Card& Deck::operator[](std::size_t index) const noexcept {
    return cards_[index];
}

}  // namespace poker