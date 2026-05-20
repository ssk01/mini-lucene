#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/analysis/letter_tokenizer.h"
#include "minilucene/analysis/lower_case_filter.h"

namespace minilucene {
namespace analysis {

std::unique_ptr<TokenStream> SimpleAnalyzer::CreateTokenStream(
    const std::string& field, std::istream& input) {
    auto tokenizer = std::make_unique<LetterTokenizer>(input);
    return std::make_unique<LowerCaseFilter>(std::move(tokenizer));
}

}  // namespace analysis
}  // namespace minilucene
