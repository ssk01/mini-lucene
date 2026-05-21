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
    auto write_int = [&](int32_t v) {
        output.WriteByte(static_cast<uint8_t>((v >> 24) & 0xff));
        output.WriteByte(static_cast<uint8_t>((v >> 16) & 0xff));
        output.WriteByte(static_cast<uint8_t>((v >> 8) & 0xff));
        output.WriteByte(static_cast<uint8_t>(v & 0xff));
    };
    write_int(static_cast<int32_t>(size_));
    write_int(count_);
    int byte_count = static_cast<int>((size_ + 7) / 8);
    for (int i = 0; i < byte_count; ++i) {
        output.WriteByte(i < static_cast<int>(bits_.size()) ? bits_[i] : 0);
    }
}

std::unique_ptr<BitVector> BitVector::Read(store::IndexInput& input) {
    auto read_int = [&]() -> int32_t {
        uint32_t v = 0;
        v |= static_cast<uint32_t>(input.ReadByte()) << 24;
        v |= static_cast<uint32_t>(input.ReadByte()) << 16;
        v |= static_cast<uint32_t>(input.ReadByte()) << 8;
        v |= static_cast<uint32_t>(input.ReadByte());
        return static_cast<int32_t>(v);
    };
    int size = read_int();
    int count = read_int();
    auto bv = std::make_unique<BitVector>(size);
    int byte_count = static_cast<int>((size + 7) / 8);
    for (int i = 0; i < byte_count; ++i) {
        bv->bits_[i] = input.ReadByte();
    }
    bv->count_ = count;
    return bv;
}

}  // namespace util
}  // namespace minilucene
