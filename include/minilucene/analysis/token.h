#pragma once

#include <string>

namespace minilucene {
namespace analysis {

class Token {
public:
    Token() = default;
    Token(std::string text, int start_offset, int end_offset, std::string type);

    const std::string& Text() const { return text_; }
    int StartOffset() const { return start_offset_; }
    int EndOffset() const { return end_offset_; }
    const std::string& Type() const { return type_; }

    void SetText(const std::string& text) { text_ = text; }

private:
    std::string text_;
    int start_offset_ = 0;
    int end_offset_ = 0;
    std::string type_;
};

}  // namespace analysis
}  // namespace minilucene
