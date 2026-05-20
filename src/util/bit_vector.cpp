#include "minilucene/util/bit_vector.h"

#include <stdexcept>

namespace minilucene {
namespace util {

BitVector::BitVector(size_t size)
    : bits_((size + 7) / 8, 0), size_(size), count_(0) {}

void BitVector::CheckBit(int bit) const {
    if (bit < 0 || static_cast<size_t>(bit) >= size_) {
        throw std::out_of_range("bit index out of range");
    }
}

void BitVector::Set(int bit) {
    CheckBit(bit);
    auto& b = bits_[bit / 8];
    uint8_t mask = static_cast<uint8_t>(1 << (bit % 8));
    if (!(b & mask)) {
        b |= mask;
        ++count_;
    }
}

void BitVector::Clear(int bit) {
    CheckBit(bit);
    auto& b = bits_[bit / 8];
    uint8_t mask = static_cast<uint8_t>(1 << (bit % 8));
    if (b & mask) {
        b &= static_cast<uint8_t>(~mask);
        --count_;
    }
}

bool BitVector::Get(int bit) const {
    CheckBit(bit);
    return (bits_[bit / 8] >> (bit % 8)) & 1;
}

int BitVector::Count() const {
    return count_;
}

}  // namespace util
}  // namespace minilucene
