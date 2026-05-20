#include "minilucene/search/prefix_query.h"
#include "minilucene/search/wildcard_query.h"
#include "minilucene/search/fuzzy_query.h"
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

document::Document Doc(const std::string& value) {
    document::Document doc;
    doc.Add(document::Field::Text("body", value));
    return doc;
}

TEST(PrefixQuery, MatchesPrefix) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    w.AddDocument(Doc("cat car card dog"));
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);

    search::PrefixQuery query(index::Term(0, "ca"));
    auto result = searcher.Search(query, 10);
    EXPECT_EQ(result.total_hits, 1);
}

TEST(WildcardQuery, StarMatchesAny) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    w.AddDocument(Doc("cat coat dog"));
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);

    search::WildcardQuery query(index::Term(0, "c*t"));
    auto result = searcher.Search(query, 10);
    EXPECT_EQ(result.total_hits, 1);
}

TEST(FuzzyQuery, EditDistance) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));
    w.AddDocument(Doc("fox box fix puppy"));
    w.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);

    search::FuzzyQuery query(index::Term(0, "fox"));
    auto result = searcher.Search(query, 10);
    EXPECT_EQ(result.total_hits, 1);
}

}  // namespace minilucene
