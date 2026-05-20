#pragma once

#include <istream>
#include <memory>
#include <string>

namespace minilucene {
namespace document {

class Field {
public:
    static Field Keyword(const std::string& name, const std::string& value);
    static Field Text(const std::string& name, const std::string& value);
    static Field Text(const std::string& name, std::istream& value);
    static Field UnIndexed(const std::string& name, const std::string& value);
    static Field UnStored(const std::string& name, const std::string& value);

    const std::string& Name() const { return name_; }
    const std::string& Value() const { return value_; }
    bool IsStored() const { return stored_; }
    bool IsIndexed() const { return indexed_; }
    bool IsTokenized() const { return tokenized_; }

private:
    Field(std::string name, std::string value, bool stored, bool indexed, bool tokenized);

    std::string name_;
    std::string value_;
    bool stored_;
    bool indexed_;
    bool tokenized_;
};

}  // namespace document
}  // namespace minilucene
