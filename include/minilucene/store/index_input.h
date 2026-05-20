#pragma once

#include <cstdint>
#include <string>

namespace minilucene {
namespace store {

class IndexInput {
public:
    virtual ~IndexInput() = default;

    virtual uint8_t ReadByte() = 0;

    int32_t ReadVInt();
    int64_t ReadVLong();
    std::string ReadString();

    virtual void Seek(int64_t pos) = 0;
    virtual int64_t FilePointer() = 0;
    virtual int64_t Length() const = 0;

    virtual void Close() = 0;
};

}  // namespace store
}  // namespace minilucene
