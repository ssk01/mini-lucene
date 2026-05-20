#pragma once

#include "minilucene/analysis/token_filter.h"

#include <string>
#include <unordered_set>

namespace minilucene {
namespace analysis {

class StopFilter : public TokenFilter {
public:
    StopFilter(std::unique_ptr<TokenStream> input,
               std::unordered_set<std::string> stop_words);

    bool Next(Token* token) override;

    static const std::unordered_set<std::string>& DefaultStopWords();

private:
    std::unordered_set<std::string> stop_words_;
};

}  // namespace analysis
}  // namespace minilucene
