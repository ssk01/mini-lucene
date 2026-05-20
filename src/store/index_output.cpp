#include "minilucene/store/index_output.h"

namespace minilucene {
namespace store {

void IndexOutput::WriteVInt(int32_t v) {
    uint32_t uv = static_cast<uint32_t>(v);
    while ((uv & ~0x7FU) != 0) {
        WriteByte(static_cast<uint8_t>((uv & 0x7F) | 0x80));
        uv >>= 7;
    }
    WriteByte(static_cast<uint8_t>(uv));
}

void IndexOutput::WriteVLong(int64_t v) {
    uint64_t uv = static_cast<uint64_t>(v);
    while ((uv & ~0x7FULL) != 0) {
        WriteByte(static_cast<uint8_t>((uv & 0x7F) | 0x80));
        uv >>= 7;
    }
    WriteByte(static_cast<uint8_t>(uv));
}

void IndexOutput::WriteString(const std::string& s) {
    WriteVInt(static_cast<int32_t>(s.size()));
    for (char c : s) {
        WriteByte(static_cast<uint8_t>(c));
    }
}

}  // namespace store
}  // namespace minilucene
