#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/analysis/token.h"
#include "minilucene/analysis/token_stream.h"
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

namespace minilucene {
namespace analysis {

std::vector<std::string> TokenizeAll(SimpleAnalyzer& analyzer, const std::string& text) {
    std::istringstream stream(text);
    auto ts = analyzer.CreateTokenStream("body", stream);
    std::vector<std::string> result;
    Token token;
    while (ts->Next(&token)) {
        result.push_back(token.Text());
    }
    return result;
}

TEST(SimpleAnalyzer, BasicSplit) {
    SimpleAnalyzer analyzer;
    auto tokens = TokenizeAll(analyzer, "Hello, World!");
    std::vector<std::string> expected = {"hello", "world"};
    EXPECT_EQ(tokens, expected);
}

TEST(SimpleAnalyzer, OffsetsCorrect) {
    std::istringstream stream("Hello World");
    SimpleAnalyzer analyzer;
    auto ts = analyzer.CreateTokenStream("body", stream);
    Token token;

    ASSERT_TRUE(ts->Next(&token));
    EXPECT_EQ(token.Text(), "hello");
    EXPECT_EQ(token.StartOffset(), 0);
    EXPECT_EQ(token.EndOffset(), 5);

    ASSERT_TRUE(ts->Next(&token));
    EXPECT_EQ(token.Text(), "world");
    EXPECT_EQ(token.StartOffset(), 6);
    EXPECT_EQ(token.EndOffset(), 11);

    EXPECT_FALSE(ts->Next(&token));
}

TEST(SimpleAnalyzer, EmptyInput) {
    SimpleAnalyzer analyzer;
    auto tokens = TokenizeAll(analyzer, "");
    EXPECT_TRUE(tokens.empty());
}

TEST(SimpleAnalyzer, OnlyDelimiters) {
    SimpleAnalyzer analyzer;
    auto tokens = TokenizeAll(analyzer, "!!! ,,,\n\t");
    EXPECT_TRUE(tokens.empty());
}

TEST(SimpleAnalyzer, UnicodeNotCrash) {
    SimpleAnalyzer analyzer;
    auto tokens = TokenizeAll(analyzer, "café résumé");
    EXPECT_FALSE(tokens.empty());
}

}  // namespace analysis
}  // namespace minilucene
