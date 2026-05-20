#include "minilucene/search/boolean_query.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/top_docs.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include <gtest/gtest.h>
#include <memory>

namespace minilucene {

document::Document MakeDoc(const std::string& field, const std::string& value) {
    document::Document doc;
    doc.Add(document::Field::Text(field, value));
    return doc;
}

void AddDoc(index::IndexWriter& w, const std::string& value) {
    w.AddDocument(MakeDoc("body", value));
}

TEST(BooleanQuery, MustAndShould) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    AddDoc(w, "fox");
    AddDoc(w, "fox jumps");
    AddDoc(w, "jumps");
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);

    search::BooleanQuery query;
    query.Add(std::make_unique<search::TermQuery>(index::Term(0, "fox")),
              search::Occur::MUST);
    query.Add(std::make_unique<search::TermQuery>(index::Term(0, "jumps")),
              search::Occur::SHOULD);

    auto result = searcher.Search(query, 10);
    EXPECT_EQ(result.total_hits, 2);
    ASSERT_EQ(result.score_docs.size(), 2);
}

TEST(BooleanQuery, MustNotExcludes) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    AddDoc(w, "fox");
    AddDoc(w, "fox lazy");
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);

    search::BooleanQuery query;
    query.Add(std::make_unique<search::TermQuery>(index::Term(0, "fox")),
              search::Occur::MUST);
    query.Add(std::make_unique<search::TermQuery>(index::Term(0, "lazy")),
              search::Occur::MUST_NOT);

    auto result = searcher.Search(query, 10);
    EXPECT_EQ(result.total_hits, 1);
    ASSERT_EQ(result.score_docs.size(), 1);
}

TEST(BooleanQuery, MaxClauseLimit) {
    search::BooleanQuery query;
    search::BooleanQuery::MAX_CLAUSE_COUNT;
    for (int i = 0; i < 32; ++i) {
        query.Add(std::make_unique<search::TermQuery>(index::Term(0, "x")),
                  search::Occur::SHOULD);
    }
    EXPECT_THROW(
        query.Add(std::make_unique<search::TermQuery>(index::Term(0, "y")),
                  search::Occur::SHOULD),
        search::TooManyClauses);
}

TEST(BooleanQuery, NoMatchReturnsEmpty) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    AddDoc(w, "fox");
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);

    search::BooleanQuery query;
    query.Add(std::make_unique<search::TermQuery>(index::Term(0, "fox")),
              search::Occur::MUST);
    query.Add(std::make_unique<search::TermQuery>(index::Term(0, "nonexistent")),
              search::Occur::MUST);

    auto result = searcher.Search(query, 10);
    EXPECT_EQ(result.total_hits, 0);
}

}  // namespace minilucene
