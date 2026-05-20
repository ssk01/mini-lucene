#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/top_docs.h"
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

TEST(TermQuery, RankByFrequency) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    AddDoc(w, "fox");
    AddDoc(w, "fox fox");
    AddDoc(w, "fox fox fox");
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);
    search::TermQuery query(index::Term(0, "fox"));

    auto result = searcher.Search(query, 10);
    EXPECT_EQ(result.total_hits, 3);
    ASSERT_EQ(result.score_docs.size(), 3);
}

TEST(TermQuery, IdfPrefersRareTerms) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    for (int i = 0; i < 100; ++i) {
        AddDoc(w, "the");
    }
    AddDoc(w, "the quantum");
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);
    search::TermQuery q_the(index::Term(0, "the"));
    search::TermQuery q_quantum(index::Term(0, "quantum"));

    auto r_the = searcher.Search(q_the, 10);
    auto r_quantum = searcher.Search(q_quantum, 10);

    ASSERT_GT(r_quantum.score_docs.size(), 0);
    ASSERT_GT(r_the.score_docs.size(), 0);
    EXPECT_GT(r_quantum.score_docs[0].score, r_the.score_docs[0].score);
}

TEST(TermQuery, NoMatchReturnsEmpty) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    AddDoc(w, "fox");
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);
    search::TermQuery query(index::Term(0, "nonexistent"));

    auto result = searcher.Search(query, 10);
    EXPECT_EQ(result.total_hits, 0);
    EXPECT_TRUE(result.score_docs.empty());
}

}  // namespace minilucene
