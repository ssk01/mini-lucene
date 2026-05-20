#pragma once

#include "minilucene/analysis/tokenizer.h"

namespace minilucene {
namespace analysis {

class LetterTokenizer : public Tokenizer {
public:
    using Tokenizer::Tokenizer;
    bool Next(Token* token) override;
};

}  // namespace analysis
}  // namespace minilucene
