#include "poker/hand.hpp"

namespace poker {

std::size_t HoldemHand::total_cards() const noexcept {
    return 2U + board_count;
}

std::array<Card, 7> HoldemHand::cards() const noexcept {
    std::array<Card, 7> result{};
    result[0] = hole[0];
    result[1] = hole[1];
    for (std::size_t index = 0; index < board_count; ++index) {
        result[2U + index] = board[index];
    }
    return result;
}

}  // namespace poker