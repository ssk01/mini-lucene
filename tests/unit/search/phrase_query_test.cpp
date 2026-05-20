#include "minilucene/search/phrase_query.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/top_docs.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include <gtest/gtest.h>

namespace minilucene {

document::Document MakeDoc(const std::string& field, const std::string& value) {
    document::Document doc;
    doc.Add(document::Field::Text(field, value));
    return doc;
}

TEST(PhraseQuery, ExactMatch) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    w.AddDocument(MakeDoc("body", "the quick brown fox"));
    w.AddDocument(MakeDoc("body", "the quick fox brown"));
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);

    search::PhraseQuery query;
    query.Add(index::Term(0, "quick"));
    query.Add(index::Term(0, "brown"));

    auto result = searcher.Search(query, 10);
    EXPECT_EQ(result.total_hits, 1);
    ASSERT_EQ(result.score_docs.size(), 1);
    EXPECT_EQ(result.score_docs[0].doc, 0);
}

TEST(PhraseQuery, SlopAllowsReordering) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    w.AddDocument(MakeDoc("body", "the quick brown fox"));
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);

    search::PhraseQuery query;
    query.Add(index::Term(0, "brown"));
    query.Add(index::Term(0, "quick"));
    query.SetSlop(2);

    auto result = searcher.Search(query, 10);
    EXPECT_EQ(result.total_hits, 1);
}

TEST(PhraseQuery, NoMatchReturnsEmpty) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    w.AddDocument(MakeDoc("body", "the quick brown fox"));
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);

    search::PhraseQuery query;
    query.Add(index::Term(0, "quick"));
    query.Add(index::Term(0, "fox"));

    auto result = searcher.Search(query, 10);
    EXPECT_EQ(result.total_hits, 0);
}

}  // namespace minilucene
