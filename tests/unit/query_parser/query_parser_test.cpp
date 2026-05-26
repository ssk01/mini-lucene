#include "minilucene/query_parser/query_parser.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/search/prefix_query.h"
#include "minilucene/search/fuzzy_query.h"
#include "minilucene/search/wildcard_query.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/boolean_clause.h"
#include <gtest/gtest.h>
#include <stdexcept>

namespace minilucene {

TEST(QueryParser, BasicSyntax) {
    query_parser::QueryParser parser("body", "hello");
    auto q = parser.Parse();
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->ToString(), "body:hello");
}

TEST(QueryParser, BooleanSyntax) {
    query_parser::QueryParser parser("body", "+fox -lazy");
    auto q = parser.Parse();
    ASSERT_NE(q, nullptr);
    // +fox is MUST, -lazy is MUST_NOT. ToString format from BooleanQuery
    // uses the +/- prefixes; check substrings rather than full string so
    // this stays robust to inner formatting choices.
    auto s = q->ToString();
    EXPECT_NE(s.find("+body:fox"),  std::string::npos) << "got: " << s;
    EXPECT_NE(s.find("-body:lazy"), std::string::npos) << "got: " << s;
}

TEST(QueryParser, FieldSpecifiedWithoutResolverThrows) {
    // Silent fallback to field 0 is the bug we just fixed. Without a
    // resolver, non-default field name must throw.
    query_parser::QueryParser parser("body", "title:hello");
    EXPECT_THROW(parser.Parse(), std::runtime_error);
}

TEST(QueryParser, FieldSpecifiedWithResolverParses) {
    query_parser::QueryParser parser("body", "title:hello");
    parser.SetFieldResolver([](const std::string& n) {
        if (n == "title") return 7;
        return -1;
    });
    EXPECT_NO_THROW(parser.Parse());
    // End-to-end "the resolver's number actually reaches the Term" is
    // proven by ForensicClaude.QueryParserFieldPrefixIsHonored — search
    // returns hits only when the right field is queried.
}

TEST(QueryParser, ResolverReturnsMinusOneThrows) {
    query_parser::QueryParser parser("body", "unknown:hello");
    parser.SetFieldResolver([](const std::string&) { return -1; });
    EXPECT_THROW(parser.Parse(), std::runtime_error);
}

TEST(QueryParser, PhraseSyntax) {
    query_parser::QueryParser parser("body", "\"quick brown\"");
    auto q = parser.Parse();
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q->ToString(), "body:\"quick brown\"");
}

}  // namespace minilucene
