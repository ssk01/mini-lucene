#include "minilucene/analysis/porter_stemmer.h"
#include "minilucene/analysis/porter_stem_filter.h"
#include "minilucene/analysis/letter_tokenizer.h"
#include "minilucene/analysis/lower_case_filter.h"
#include "minilucene/analysis/token.h"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

namespace minilucene {
namespace analysis {

TEST(PorterStemmer, BasicStemming) {
    PorterStemmer stemmer;
    struct { const char* input; const char* expected; } cases[] = {
        {"running", "run"},
        {"ran", "ran"},
        {"runner", "runner"},
        {"easily", "easili"},
        {"fairly", "fairli"},
        {"consign", "consign"},
        {"consigned", "consign"},
        {"consigning", "consign"},
        {"consignment", "consign"},
        {"generalization", "gener"},
        {"generalizations", "gener"},
        {"hoping", "hope"},
        {"hopped", "hop"},
        {"hopping", "hop"},
        {"connect", "connect"},
        {"connected", "connect"},
        {"connection", "connect"},
        {"trouble", "troubl"},
        {"troubled", "troubl"},
        {"troubles", "troubl"},
    };
    for (auto& c : cases) {
        std::string word = c.input;
        stemmer.Stem(word);
        EXPECT_EQ(word, c.expected) << "stemming '" << c.input << "'";
    }
}

TEST(PorterStemFilter, Pipeline) {
    std::istringstream stream("running jumped foxes");
    auto tokenizer = std::make_unique<LetterTokenizer>(stream);
    auto lower = std::make_unique<LowerCaseFilter>(std::move(tokenizer));
    auto stemmer = std::make_unique<PorterStemFilter>(std::move(lower));

    Token token;
    std::vector<std::string> result;
    while (stemmer->Next(&token)) {
        result.push_back(token.Text());
    }
    std::vector<std::string> expected = {"run", "jump", "fox"};
    EXPECT_EQ(result, expected);
}

}  // namespace analysis
}  // namespace minilucene
