#include "minilucene/util/bit_vector.h"
#include "minilucene/store/index_input.h"
#include "minilucene/store/index_output.h"

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

void BitVector::Write(store::IndexOutput& output) const {
    output.WriteVInt(static_cast<int32_t>(size_));
    int byte_count = static_cast<int>(bits_.size());
    output.WriteVInt(byte_count);
    for (int i = 0; i < byte_count; ++i) {
        output.WriteByte(bits_[i]);
    }
}

std::unique_ptr<BitVector> BitVector::Read(store::IndexInput& input) {
    int size = input.ReadVInt();
    int byte_count = input.ReadVInt();
    auto bv = std::make_unique<BitVector>(size);
    for (int i = 0; i < byte_count; ++i) {
        bv->bits_[i] = input.ReadByte();
    }
    bv->count_ = 0;
    for (size_t i = 0; i < bv->size_; ++i) {
        if (bv->Get(static_cast<int>(i))) ++bv->count_;
    }
    return bv;
}

}  // namespace util
}  // namespace minilucene
