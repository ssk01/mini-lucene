#pragma once

#include <string>

namespace minilucene {
namespace index {

class FieldInfo {
public:
    FieldInfo(std::string name, int number, bool stored, bool indexed, bool tokenized);

    const std::string& Name() const { return name_; }
    int Number() const { return number_; }
    bool IsStored() const { return stored_; }
    bool IsIndexed() const { return indexed_; }
    bool IsTokenized() const { return tokenized_; }

private:
    std::string name_;
    int number_;
    bool stored_;
    bool indexed_;
    bool tokenized_;
};

}  // namespace index
}  // namespace minilucene
