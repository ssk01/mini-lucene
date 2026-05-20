#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace minilucene {
namespace util {

class BitVector {
public:
    explicit BitVector(size_t size);
    ~BitVector() = default;

    void Set(int bit);
    void Clear(int bit);
    bool Get(int bit) const;
    int Count() const;

private:
    void CheckBit(int bit) const;

    std::vector<uint8_t> bits_;
    size_t size_;
    int count_;
};

}  // namespace util
}  // namespace minilucene
