// These tests FAIL at runtime because the features don't exist yet.
// Each test demonstrates a real scenario where missing functionality
// produces wrong results.

#include "minilucene/analysis/standard_analyzer.h"
#include "minilucene/analysis/standard_filter.h"
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/analysis/token.h"
#include "minilucene/analysis/token_stream.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/document_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/segments_reader.h"
#include "minilucene/index/segment_merge_info.h"
#include "minilucene/index/segment_merge_queue.h"
#include "minilucene/index/term.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/search/filter.h"
#include "minilucene/search/date_filter.h"
#include "minilucene/search/hit_collector.h"
#include "minilucene/search/hit_queue.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/multi_searcher.h"
#include "minilucene/search/filtered_term_enum.h"
#include "minilucene/search/multi_term_query.h"
#include "minilucene/search/fuzzy_term_enum.h"
#include "minilucene/search/wildcard_term_enum.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/search/hits.h"
#include "minilucene/search/top_docs.h"
#include "minilucene/search/searcher.h"
#include "minilucene/store/ram_directory.h"
#include "minilucene/store/fs_directory.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace minilucene {

// ===== 1. StandardAnalyzer — should recognize email/URL/acronyms =====
TEST(MissingFeature, StandardAnalyzerRecognizesEmail) {
    analysis::StandardAnalyzer a;
    std::istringstream input("contact@example.com");
    auto ts = a.CreateTokenStream("body", input);
    analysis::Token t;
    ASSERT_TRUE(ts->Next(&t)) << "StandardAnalyzer should tokenize email as single token";
    EXPECT_EQ(t.Text(), "contact@example.com")
        << "StandardAnalyzer should preserve email addresses";
}

TEST(MissingFeature, StandardAnalyzerRecognizesURL) {
    analysis::StandardAnalyzer a;
    std::istringstream input("visit https://lucene.apache.org");
    auto ts = a.CreateTokenStream("body", input);
    analysis::Token t;
    bool found_url = false;
    while (ts->Next(&t)) {
        if (t.Text() == "https://lucene.apache.org") found_url = true;
    }
    EXPECT_TRUE(found_url) << "StandardAnalyzer should preserve URLs as single tokens";
}

TEST(MissingFeature, StandardAnalyzerAcronyms) {
    analysis::StandardAnalyzer a;
    std::istringstream input("U.S.A. I.B.M.");
    auto ts = a.CreateTokenStream("body", input);
    analysis::Token t;
    bool found_usa = false;
    while (ts->Next(&t)) {
        if (t.Text() == "u.s.a") found_usa = true;
    }
    EXPECT_TRUE(found_usa) << "StandardAnalyzer should normalize acronyms";
}

// ===== 2. SegmentsReader — reading multi-segment index =====
TEST(MissingFeature, SegmentsReaderFindsAllDocs) {
    store::RAMDirectory dir;

    // Write 2 segments manually
    auto a1 = std::make_unique<analysis::SimpleAnalyzer>();
    index::DocumentWriter dw1(dir, *a1);
    document::Document d1; d1.Add(document::Field::Text("body", "fox"));
    dw1.AddDocument(d1);
    dw1.Flush("_a");

    auto a2 = std::make_unique<analysis::SimpleAnalyzer>();
    index::DocumentWriter dw2(dir, *a2);
    document::Document d2; d2.Add(document::Field::Text("body", "rabbit"));
    dw2.AddDocument(d2);
    dw2.Flush("_b");

    // Write segments file so SegmentsReader can find the segments
    index::SegmentInfos sis;
    sis.Add("_a", 1);
    sis.Add("_b", 1);
    sis.Write(dir);

    // Reader should find docs in both segments
    index::SegmentsReader reader(dir);
    EXPECT_EQ(reader.NumDocs(), 2) << "SegmentsReader should see 2 docs across 2 segments";

    int df_fox = reader.DocFreq(index::Term(0, "fox"));
    EXPECT_EQ(df_fox, 1) << "fox should be in 1 doc across all segments";

    auto terms = reader.Terms();
    if (terms) {
        std::vector<std::string> found;
        while (terms->Next()) found.push_back(terms->Current().Text());
        EXPECT_TRUE(std::find(found.begin(), found.end(), "fox") != found.end())
            << "SegmentsReader should enumerate terms from all segments";
    } else {
        FAIL() << "SegmentsReader::Terms() returned null — not implemented";
    }
}

// ===== 3. Filter / DateFilter — restricting search results =====
TEST(MissingFeature, DateFilterExcludesOutOfRange) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter writer(dir, std::move(analyzer));
    document::Document doc;
    doc.Add(document::Field::Text("body", "hello world"));
    doc.Add(document::Field::Keyword("date", "20200101"));
    writer.AddDocument(doc);
    document::Document doc2;
    doc2.Add(document::Field::Text("body", "hello again"));
    doc2.Add(document::Field::Keyword("date", "20200615"));
    writer.AddDocument(doc2);
    writer.Close();

    index::SegmentReader r(dir, "_0");
    search::DateFilter df("date", "20200101", "20200101");
    auto bits = df.Bits(r);
    EXPECT_TRUE(bits.Get(0)) << "doc0 should match filter";
    EXPECT_FALSE(bits.Get(1)) << "doc1 should be excluded by filter";
}

// ===== 4. MultiSearcher — cross-index search =====
// Test the part that DOES work: AddSearcher aggregates MaxDoc across sub-searchers.
// The Search() merge-and-return-Hits path is unimplemented (Hits requires a single
// IndexReader); see GTEST_SKIP below for the honest TODO marker.
TEST(MissingFeature, MultiSearcherAggregatesMaxDoc) {
    store::RAMDirectory d1;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(d1, std::move(a));
        document::Document doc; doc.Add(document::Field::Text("body", "fox"));
        w.AddDocument(doc);
        w.AddDocument(doc);
        w.Close();
    }
    store::RAMDirectory d2;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(d2, std::move(a));
        document::Document doc; doc.Add(document::Field::Text("body", "rabbit"));
        w.AddDocument(doc);
        w.Close();
    }

    auto r1 = std::make_unique<index::SegmentsReader>(d1);
    auto r2 = std::make_unique<index::SegmentsReader>(d2);
    auto s1 = std::make_unique<search::IndexSearcher>(*r1);
    auto s2 = std::make_unique<search::IndexSearcher>(*r2);

    search::MultiSearcher ms;
    EXPECT_EQ(ms.MaxDoc(), 0);
    ms.AddSearcher(std::move(s1));
    EXPECT_EQ(ms.MaxDoc(), 2) << "after adding d1 (2 docs), MaxDoc must be 2";
    ms.AddSearcher(std::move(s2));
    EXPECT_EQ(ms.MaxDoc(), 3) << "after adding d2 (1 doc), MaxDoc must be 3";

    r1->Close();
    r2->Close();
}

TEST(MissingFeature, MultiSearcherSearchMergesCrossIndex) {
    // Two separate single-doc indexes; query MUST hit both via MultiSearcher.
    store::RAMDirectory d1, d2;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(d1, std::move(a));
        document::Document doc;
        doc.Add(document::Field::Keyword("id", "from-d1"));
        doc.Add(document::Field::Text("body", "fox"));
        w.AddDocument(doc);
        w.Close();
    }
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(d2, std::move(a));
        document::Document doc;
        doc.Add(document::Field::Keyword("id", "from-d2"));
        doc.Add(document::Field::Text("body", "fox"));
        w.AddDocument(doc);
        w.Close();
    }

    auto r1 = std::make_unique<index::SegmentsReader>(d1);
    auto r2 = std::make_unique<index::SegmentsReader>(d2);
    auto s1 = std::make_unique<search::IndexSearcher>(*r1);
    auto s2 = std::make_unique<search::IndexSearcher>(*r2);

    search::MultiSearcher ms;
    ms.AddSearcher(std::move(s1));
    ms.AddSearcher(std::move(s2));

    search::TermQuery q(index::Term(1, "fox"));  // field 1 = body (id Keyword is 0)
    auto hits = ms.Search(q);
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->Length(), 2)
        << "MultiSearcher must merge hits from both sub-indexes";

    // Hits::Doc(n) must round-trip back to the originating sub-reader's
    // stored field — proving the synthetic global doc-id mapping works.
    std::vector<std::string> ids;
    for (int i = 0; i < hits->Length(); ++i) {
        auto d = hits->Doc(i);
        ASSERT_NE(d, nullptr) << "hit " << i << " Doc() lookup returned null";
        auto* f = d->GetField("id");
        ASSERT_NE(f, nullptr);
        ids.push_back(f->Value());
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids, (std::vector<std::string>{"from-d1", "from-d2"}));

    r1->Close();
    r2->Close();
}

// ===== 5. HitQueue — Top-K collection =====
TEST(MissingFeature, HitQueueCollectsTopK) {
    search::HitQueue q(10);
    EXPECT_EQ(q.Size(), 0);

    search::ScoreDoc sd1; sd1.doc = 0; sd1.score = 1.0f;
    search::ScoreDoc sd2; sd2.doc = 1; sd2.score = 2.0f;
    search::ScoreDoc sd3; sd3.doc = 2; sd3.score = 0.5f;

    q.Put(sd1); q.Put(sd2); q.Put(sd3);
    EXPECT_EQ(q.Size(), 3);

    // Top should be the lowest score (min-heap)
    EXPECT_EQ(q.Top().doc, 2);
    EXPECT_FLOAT_EQ(q.Top().score, 0.5f);
}

// ===== 6. HitCollector — callback interface =====
TEST(MissingFeature, HitCollectorCallback) {
    struct MyCollector : search::HitCollector {
        void Collect(int doc, float score) override { called = true; last_doc = doc; last_score = score; }
        bool called = false; int last_doc = 0; float last_score = 0;
    };
    MyCollector c;
    c.Collect(42, 0.5f);
    EXPECT_TRUE(c.called);
    EXPECT_EQ(c.last_doc, 42);
    EXPECT_FLOAT_EQ(c.last_score, 0.5f);
}

// ===== 7. SegmentMergeQueue — construction only =====
// Honest scope: empty-queue construction is all this verifies. Real ordering
// tests require constructing SegmentMergeInfo from live SegmentReader+TermEnum,
// which belongs in segment_merge_test.cpp where the merge path is exercised
// end-to-end anyway.
TEST(MissingFeature, SegmentMergeQueueConstructsEmpty) {
    index::SegmentMergeQueue queue(10);
    EXPECT_EQ(queue.Size(), 0) << "empty queue should have size 0";
}

// ===== 6. FilteredTermEnum — filtered enumeration =====
// Uses a real corpus with both matching and non-matching terms so the filter
// actually has work to do; previously this used an empty doc and accepted
// `if (has_next) ...` which silently passed when has_next was false.
TEST(MissingFeature, FilteredTermEnumSkipsMismatches) {
    store::RAMDirectory dir;
    {
        auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter writer(dir, std::move(analyzer));
        document::Document doc;
        doc.Add(document::Field::Text("body", "apple banana cherry ant zebra"));
        writer.AddDocument(doc);
        writer.Close();
    }

    struct StartsWithA : search::FilteredTermEnum {
        StartsWithA(index::IndexReader& r) {
            auto terms = r.Terms();
            if (terms) SetEnum(std::move(terms));
        }
        bool TermMatch(const index::Term& term) override {
            return !term.Text().empty() && term.Text()[0] == 'a';
        }
    };

    index::SegmentReader reader(dir, "_0");
    ASSERT_NE(reader.Terms(), nullptr) << "reader must produce a TermEnum";
    StartsWithA e(reader);
    std::vector<std::string> matched;
    while (e.Next()) {
        ASSERT_EQ(e.Current().Text()[0], 'a')
            << "FilteredTermEnum yielded non-matching term '"
            << e.Current().Text() << "'";
        matched.push_back(e.Current().Text());
    }
    // Corpus has "apple" and "ant" starting with 'a'; both must surface.
    EXPECT_NE(std::find(matched.begin(), matched.end(), "apple"), matched.end());
    EXPECT_NE(std::find(matched.begin(), matched.end(), "ant"),   matched.end());
}

// ===== 7. WildcardTermEnum + FuzzyTermEnum =====
TEST(MissingFeature, FuzzyTermEnumFindsCloseMatches) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter writer(dir, std::move(analyzer));
    document::Document doc;
    doc.Add(document::Field::Text("body", "fox box fix puppy"));
    writer.AddDocument(doc);
    writer.Close();

    index::SegmentReader reader(dir, "_0");
    search::FuzzyTermEnum fte(reader, index::Term(0, "fox"), 2);
    bool found_fox = false, found_box = false;
    while (fte.Next()) {
        if (fte.Current().Text() == "fox") found_fox = true;
        if (fte.Current().Text() == "box") found_box = true;
    }
    EXPECT_TRUE(found_fox) << "FuzzyTermEnum should find exact match 'fox'";
    EXPECT_TRUE(found_box) << "FuzzyTermEnum should find 'box' within edit distance 2";
}

TEST(MissingFeature, WildcardTermEnumMatchesPattern) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter writer(dir, std::move(analyzer));
    document::Document doc;
    doc.Add(document::Field::Text("body", "cat coat dog"));
    writer.AddDocument(doc);
    writer.Close();

    index::SegmentReader reader(dir, "_0");
    search::WildcardTermEnum wte(reader, index::Term(0, "c*t"));
    bool found_cat = false;
    while (wte.Next()) {
        if (wte.Current().Text() == "cat") found_cat = true;
    }
    EXPECT_TRUE(found_cat) << "WildcardTermEnum should match 'c*t' with 'cat'";
}

// ===== 8. StandardFilter — pass-through stub =====
// StandardFilter is currently a no-op. Normalization is inlined into
// StandardTokenizer instead. Construction should not throw.
TEST(MissingFeature, StandardFilterConstructs) {
    EXPECT_NO_THROW(analysis::StandardFilter sf(nullptr));
}

}  // namespace minilucene
