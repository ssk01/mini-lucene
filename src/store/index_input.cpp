#include "minilucene/store/index_input.h"

namespace minilucene {
namespace store {

int32_t IndexInput::ReadVInt() {
    uint8_t b = ReadByte();
    int32_t v = b & 0x7F;
    int shift = 7;
    while (b & 0x80) {
        b = ReadByte();
        v |= (static_cast<int32_t>(b & 0x7F) << shift);
        shift += 7;
    }
    return v;
}

int64_t IndexInput::ReadVLong() {
    uint8_t b = ReadByte();
    int64_t v = b & 0x7F;
    int shift = 7;
    while (b & 0x80) {
        b = ReadByte();
        v |= (static_cast<int64_t>(b & 0x7F) << shift);
        shift += 7;
    }
    return v;
}

std::string IndexInput::ReadString() {
    int32_t length = ReadVInt();
    std::string s;
    s.resize(length);
    for (int32_t i = 0; i < length; ++i) {
        s[i] = static_cast<char>(ReadByte());
    }
    return s;
}

}  // namespace store
}  // namespace minilucene
