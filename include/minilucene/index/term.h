#pragma once

#include <string>
#include <utility>

namespace minilucene {
namespace index {

class Term {
public:
    Term() = default;
    Term(int field_number, std::string text)
        : field_number_(field_number), text_(std::move(text)) {}

    int FieldNumber() const { return field_number_; }
    const std::string& Text() const { return text_; }

    bool operator<(const Term& other) const {
        if (field_number_ != other.field_number_)
            return field_number_ < other.field_number_;
        return text_ < other.text_;
    }
    bool operator==(const Term& other) const {
        return field_number_ == other.field_number_ && text_ == other.text_;
    }

private:
    int field_number_ = 0;
    std::string text_;
};

}  // namespace index
}  // namespace minilucene
