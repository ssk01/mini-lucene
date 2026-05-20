// Stress test: index many random docs, run many queries.
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/hits.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>

namespace minilucene {

std::string RandomWord(std::mt19937& rng, int len) {
    static const char* letters = "abcdefghijklmnopqrstuvwxyz";
    std::string w;
    for (int i = 0; i < len; ++i)
        w += letters[rng() % 26];
    return w;
}

std::string RandomDoc(std::mt19937& rng, int words) {
    std::string d;
    for (int i = 0; i < words; ++i) {
        if (i > 0) d += " ";
        d += RandomWord(rng, 3 + rng() % 8);
    }
    return d;
}

TEST(Stress, Index500DocsSearch100Queries) {
    std::mt19937 rng(42);
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.mergeFactor = 10000;

    // Index 500 random docs, each with 10-30 words
    std::vector<std::string> docs;
    for (int i = 0; i < 500; ++i) {
        std::string text = RandomDoc(rng, 10 + rng() % 20);
        docs.push_back(text);
        document::Document doc;
        doc.Add(document::Field::Text("body", text));
        w.AddDocument(doc);
    }
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    // Extract all unique words from docs
    std::vector<std::string> all_words;
    for (auto& d : docs) {
        std::istringstream ss(d);
        std::string word;
        while (ss >> word) all_words.push_back(word);
    }

    // Run 100 random queries
    int queries_with_hits = 0;
    for (int i = 0; i < 100; ++i) {
        std::string query_word = all_words[rng() % all_words.size()];
        search::TermQuery tq(index::Term(0, query_word));
        auto hits = s.Search(tq);
        if (hits && hits->Length() > 0) ++queries_with_hits;
    }

    EXPECT_GT(queries_with_hits, 5) << "Some random queries should find hits";
    EXPECT_EQ(r.NumDocs(), 500);
}

}  // namespace minilucene
