#pragma once

#include "minilucene/analysis/letter_tokenizer.h"

namespace minilucene {
namespace analysis {

class LowerCaseTokenizer : public LetterTokenizer {
public:
    using LetterTokenizer::LetterTokenizer;
    bool Next(Token* token) override;
};

}  // namespace analysis
}  // namespace minilucene
