// Regression tests for previously fixed bugs.
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/search/hits.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include <gtest/gtest.h>

namespace minilucene {

document::Document D(const std::string& t) {
    document::Document d;
    d.Add(document::Field::Text("body", t));
    return d;
}

// Bug: BooleanScorer hung in infinite loop when MUST_NOT excluded all docs
TEST(Regression, BooleanScorerMustNotExcludeAll) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(D("fox"));
    w.AddDocument(D("fox lazy"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    search::BooleanQuery bq;
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "fox")), search::Occur::MUST);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "lazy")), search::Occur::MUST_NOT);

    auto hits = s.Search(bq);
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->Length(), 1);  // Only doc0
}

// Bug: BooleanScorer looped when MUST scorer exhausted but SHOULD remained
TEST(Regression, BooleanScorerMustExhaustedWithShould) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(D("fox"));
    w.AddDocument(D("fox jumps"));
    w.AddDocument(D("jumps"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    search::BooleanQuery bq;
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "fox")), search::Occur::MUST);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "jumps")), search::Occur::SHOULD);

    auto hits = s.Search(bq);
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->Length(), 2);  // Doc0, Doc1
    EXPECT_GT(hits->Score(0), hits->Score(1));  // Doc1 (coord=2/2) > Doc0 (coord=1/2)
}

// Bug: PhraseScorer read beyond EOF on single-doc segments
TEST(Regression, PhraseScorerNoCrash) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(D("quick brown fox"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    search::PhraseQuery pq;
    pq.Add(index::Term(0, "quick"));
    pq.Add(index::Term(0, "brown"));

    auto hits = s.Search(pq);
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->Length(), 1);
}

// Bug: SegmentReader num_docs_ was hardcoded to 1
TEST(Regression, NumDocsMultiDoc) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(D("doc1"));
    w.AddDocument(D("doc2"));
    w.AddDocument(D("doc3"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    EXPECT_EQ(r.NumDocs(), 3);
}

}  // namespace minilucene
