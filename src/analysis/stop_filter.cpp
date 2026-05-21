#include "minilucene/analysis/stop_filter.h"
#include "minilucene/analysis/token.h"

namespace minilucene {
namespace analysis {

const std::unordered_set<std::string>& StopFilter::DefaultStopWords() {
    static const std::unordered_set<std::string> words = {
        "a", "and", "are", "as", "at", "be", "but", "by",
        "for", "if", "in", "into", "is", "it",
        "no", "not", "of", "on", "or", "s", "such",
        "t", "that", "the", "their", "then", "there", "these",
        "they", "this", "to", "was", "will", "with",
    };
    return words;
}

StopFilter::StopFilter(std::unique_ptr<TokenStream> input,
                       std::unordered_set<std::string> stop_words)
    : TokenFilter(std::move(input))
    , stop_words_(std::move(stop_words)) {}

bool StopFilter::Next(Token* token) {
    while (input_->Next(token)) {
        if (stop_words_.find(token->Text()) == stop_words_.end()) {
            return true;
        }
    }
    return false;
}

}  // namespace analysis
}  // namespace minilucene
