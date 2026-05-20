#include "minilucene/analysis/stop_filter.h"
#include "minilucene/analysis/letter_tokenizer.h"
#include "minilucene/analysis/lower_case_filter.h"
#include "minilucene/analysis/token.h"
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace minilucene {
namespace analysis {

std::vector<std::string> TokenizeWithAnalyzer(
    std::unique_ptr<TokenStream> ts) {
    std::vector<std::string> result;
    Token token;
    while (ts->Next(&token)) {
        result.push_back(token.Text());
    }
    return result;
}

TEST(StopFilter, RemovesStopWords) {
    std::istringstream stream("the quick brown fox jumps over the lazy dog");
    auto tokenizer = std::make_unique<LetterTokenizer>(stream);
    auto lower = std::make_unique<LowerCaseFilter>(std::move(tokenizer));
    auto stop = std::make_unique<StopFilter>(
        std::move(lower), StopFilter::DefaultStopWords());

    auto tokens = TokenizeWithAnalyzer(std::move(stop));
    std::vector<std::string> expected = {
        "quick", "brown", "fox", "jumps", "over", "lazy", "dog"
    };
    EXPECT_EQ(tokens, expected);
}

TEST(StopFilter, CustomStopWords) {
    std::istringstream stream("apple banana cherry");
    auto tokenizer = std::make_unique<LetterTokenizer>(stream);
    auto stop = std::make_unique<StopFilter>(
        std::move(tokenizer),
        std::unordered_set<std::string>{"banana"});

    auto tokens = TokenizeWithAnalyzer(std::move(stop));
    std::vector<std::string> expected = {"apple", "cherry"};
    EXPECT_EQ(tokens, expected);
}

}  // namespace analysis
}  // namespace minilucene
