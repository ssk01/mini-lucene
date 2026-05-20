#include "minilucene/analysis/porter_stem_filter.h"
#include "minilucene/analysis/porter_stemmer.h"
#include "minilucene/analysis/token.h"

namespace minilucene {
namespace analysis {

bool PorterStemFilter::Next(Token* token) {
    if (!input_->Next(token)) return false;
    std::string word = token->Text();
    PorterStemmer().Stem(word);
    token->SetText(word);
    return true;
}

}  // namespace analysis
}  // namespace minilucene
