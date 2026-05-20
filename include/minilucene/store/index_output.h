#pragma once

#include <cstdint>
#include <string>

namespace minilucene {
namespace store {

class IndexOutput {
public:
    virtual ~IndexOutput() = default;

    virtual void WriteByte(uint8_t b) = 0;

    void WriteVInt(int32_t v);
    void WriteVLong(int64_t v);
    void WriteString(const std::string& s);

    virtual int64_t FilePointer() = 0;
    virtual void Flush() = 0;
    virtual void Close() = 0;
};

}  // namespace store
}  // namespace minilucene
