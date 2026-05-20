#include "minilucene/analysis/standard_analyzer.h"
#include "minilucene/analysis/letter_tokenizer.h"
#include "minilucene/analysis/lower_case_filter.h"
#include "minilucene/analysis/stop_filter.h"

namespace minilucene {
namespace analysis {

StandardAnalyzer::StandardAnalyzer() {}

std::unique_ptr<TokenStream> StandardAnalyzer::CreateTokenStream(
    const std::string& field, std::istream& input) {
    // STUB: uses SimpleAnalyzer behavior instead of proper StandardAnalyzer
    auto tokenizer = std::make_unique<LetterTokenizer>(input);
    auto lower = std::make_unique<LowerCaseFilter>(std::move(tokenizer));
    return std::make_unique<StopFilter>(std::move(lower), StopFilter::DefaultStopWords());
}

}  // namespace analysis
}  // namespace minilucene
