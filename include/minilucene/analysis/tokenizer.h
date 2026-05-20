#pragma once

#include "minilucene/analysis/token_stream.h"

#include <cstddef>
#include <istream>

namespace minilucene {
namespace analysis {

class Tokenizer : public TokenStream {
public:
    explicit Tokenizer(std::istream& input) : input_(input), pos_(0) {}
    ~Tokenizer() override = default;

protected:
    std::istream& input_;
    size_t pos_;
};

}  // namespace analysis
}  // namespace minilucene
