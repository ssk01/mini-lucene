#include "minilucene/query_parser/query_parser.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/search/prefix_query.h"
#include "minilucene/search/fuzzy_query.h"
#include "minilucene/search/wildcard_query.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/boolean_clause.h"
#include <gtest/gtest.h>

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
}

TEST(QueryParser, FieldSpecified) {
    query_parser::QueryParser parser("body", "title:hello");
    auto q = parser.Parse();
    ASSERT_NE(q, nullptr);
}

TEST(QueryParser, PhraseSyntax) {
    query_parser::QueryParser parser("body", "\"quick brown\"");
    auto q = parser.Parse();
    ASSERT_NE(q, nullptr);
}

}  // namespace minilucene
