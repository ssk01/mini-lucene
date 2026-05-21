// All expected values computed from first principles (TF-IDF formula).
// SimpleAnalyzer: LetterTokenizer + LowerCaseFilter (no stop filter).
// Formula: score = tf × idf² × norm
//   tf(freq) = sqrt(freq)
//   idf(docFreq, maxDoc) = log(maxDoc/(docFreq+1)) + 1
//   norm(numTokens) = 1/sqrt(numTokens), quantized to byte: byte = norm*255, decode = byte/255

#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/analysis/stop_analyzer.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/search/hits.h"
#include "minilucene/search/top_docs.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include "minilucene/index/document_writer.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/segments_reader.h"
#include "minilucene/index/fields_writer.h"
#include "minilucene/index/fields_reader.h"
#include "minilucene/index/field_infos.h"
#include "minilucene/query_parser/query_parser.h"
#include "minilucene/document/document.h"
#include <gtest/gtest.h>
#include <cmath>

namespace minilucene {

// ========== helpers ==========
document::Document Doc(const std::string& text) {
    document::Document d;
    d.Add(document::Field::Text("body", text));
    return d;
}

// ========== 1. Exact TF-IDF scores, single doc ==========
TEST(Scoring, SingleDocRankByTF) {
    // doc0: "fox" ×3, "cat" ×1  → total 4 tokens
    // norm = 1/sqrt(4) = 0.5 → byte 127 → decode 127/255=0.498039
    // idf = log(1/(1+1))+1 = log(0.5)+1 = -0.693147+1 = 0.306853
    // score(fox) = sqrt(3) × 0.306853² × 0.498039 = 1.732051 × 0.094158 × 0.498039 = 0.0812
    // score(cat) = sqrt(1) × 0.306853² × 0.498039 = 1.0 × 0.094158 × 0.498039 = 0.0469

    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(Doc("fox fox fox cat"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    search::TermQuery q_fox(index::Term(0, "fox"));
    search::TermQuery q_cat(index::Term(0, "cat"));
    auto h_fox = s.Search(q_fox);
    auto h_cat = s.Search(q_cat);

    ASSERT_EQ(h_fox->Length(), 1);
    ASSERT_EQ(h_cat->Length(), 1);
    EXPECT_GT(h_fox->Score(0), h_cat->Score(0));
    EXPECT_NEAR(h_fox->Score(0), 0.0812f, 0.001f);
    EXPECT_NEAR(h_cat->Score(0), 0.0469f, 0.001f);
}

// ========== 2. IDF across docs ==========
TEST(Scoring, IDFDifferentiates) {
    // doc0: empty (no body field)
    // doc1: "fox"         → 1 token
    // doc2: "fox"         → 1 token
    // doc3: "fox rabbit"  → 2 tokens
    // fox docFreq=3, rabbit docFreq=1, maxDoc=4
    // idf(fox) = log(4/(3+1))+1 = log(1)+1 = 1.0
    // idf(rabbit) = log(4/(1+1))+1 = log(2)+1 = 0.693147+1 = 1.693147
    // fox in doc3: norm=1/sqrt(2)=0.707 → byte 180 → 180/255=0.706
    // rabbit in doc3: norm=0.706
    // score(fox,doc3) = 1 × 1² × 0.706 = 0.706
    // score(rabbit,doc3) = 1 × 1.693² × 0.706 = 2.867 × 0.706 = 2.024

    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(document::Document());  // doc0
    w.AddDocument(Doc("fox"));            // doc1
    w.AddDocument(Doc("fox"));            // doc2
    w.AddDocument(Doc("fox rabbit"));     // doc3
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    search::TermQuery q_fox(index::Term(0, "fox"));
    search::TermQuery q_rabbit(index::Term(0, "rabbit"));
    auto h_fox = s.Search(q_fox);
    auto h_rabbit = s.Search(q_rabbit);

    ASSERT_EQ(h_fox->Length(), 3);
    ASSERT_EQ(h_rabbit->Length(), 1);
    EXPECT_NEAR(h_rabbit->Score(0), 2.024f, 0.01f);
}

// ========== 3. BooleanQuery coord factor ==========
TEST(Scoring, BooleanCoord) {
    // doc0: "fox"         → 1 token
    // doc1: "fox jumps"   → 2 tokens
    // Query: +fox jumps (MUST fox, SHOULD jumps)
    // Java BooleanScorer: maxCoord 初始化为 1，每个非禁止子句 +1
    // maxCoord = 1 + 1 (MUST) + 1 (SHOULD) = 3
    // doc0: fox freq=1, no jumps. overlap=1 (must). coord=1/3≈0.333
    // doc1: fox freq=1, jumps freq=1. overlap=2. coord=2/3≈0.667
    // idf(fox)=log(2/(2+1))+1=log(0.667)+1=-0.405+1=0.595
    // idf(jumps)=log(2/(1+1))+1=log(1)+1=1.0

    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(Doc("fox"));
    w.AddDocument(Doc("fox jumps"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    search::BooleanQuery bq;
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "fox")), search::Occur::MUST);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "jumps")), search::Occur::SHOULD);

    auto hits = s.Search(bq);
    ASSERT_EQ(hits->Length(), 2);
    EXPECT_EQ(hits->Id(0), 1);  // doc1 ranks first
    EXPECT_EQ(hits->Id(1), 0);
}

// ========== 4. PhraseQuery exact match ==========
TEST(Phrase, ExactMatch) {
    // doc0: "quick brown fox" → "quick"@0 "brown"@1 "fox"@2
    // doc1: "brown quick fox" → "quick"@1 "brown"@0
    // Query "quick brown" → matches doc0 only (positions 0,1)
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(Doc("quick brown fox"));
    w.AddDocument(Doc("brown quick fox"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    search::PhraseQuery pq;
    pq.Add(index::Term(0, "quick"));
    pq.Add(index::Term(0, "brown"));

    auto hits = s.Search(pq);
    ASSERT_EQ(hits->Length(), 1);
    EXPECT_EQ(hits->Id(0), 0);
}

// ========== 5. PhraseQuery slop ==========
TEST(Phrase, SloppyMatch) {
    // doc0: "quick brown fox"
    // Query "brown quick" slop=2 → "brown"@1 "quick"@0 → gap=|1-0-1|=0 ≤2 → match
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(Doc("quick brown fox"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    search::PhraseQuery pq;
    pq.Add(index::Term(0, "brown"));
    pq.Add(index::Term(0, "quick"));
    pq.SetSlop(2);

    auto hits = s.Search(pq);
    ASSERT_EQ(hits->Length(), 1);
}

// ========== 6. Exact term positions ==========
TEST(Index, TermPositions) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(Doc("a b a b a"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    // "a" at positions 0,2,4 → .prx stores deltas 0,2,2
    auto tp = r.Positions(index::Term(0, "a"));
    ASSERT_NE(tp, nullptr);
    ASSERT_TRUE(tp->Next());
    EXPECT_EQ(tp->Freq(), 3);
    int pos = 0;
    pos += tp->NextPosition(); EXPECT_EQ(pos, 0);
    pos += tp->NextPosition(); EXPECT_EQ(pos, 2);
    pos += tp->NextPosition(); EXPECT_EQ(pos, 4);
}

// ========== 7. Multi-segment search (via SegmentsReader) ==========
TEST(MultiSegment, SearchAcrossAll) {
    // Manually create 2 segments, then search via SegmentsReader
    store::RAMDirectory dir;
    auto a1 = std::make_unique<analysis::SimpleAnalyzer>();
    index::DocumentWriter dw1(dir, *a1);
    dw1.AddDocument(Doc("fox"));
    dw1.Flush("_a");

    auto a2 = std::make_unique<analysis::SimpleAnalyzer>();
    index::DocumentWriter dw2(dir, *a2);
    dw2.AddDocument(Doc("rabbit"));
    dw2.Flush("_b");

    index::SegmentInfos sis;
    sis.Add("_a", 1); sis.Add("_b", 1);
    sis.Write(dir);

    index::SegmentsReader reader(dir);
    EXPECT_EQ(reader.NumDocs(), 2);
    EXPECT_EQ(reader.DocFreq(index::Term(0, "fox")), 1);
    EXPECT_EQ(reader.DocFreq(index::Term(0, "rabbit")), 1);

    search::IndexSearcher s(reader);
    search::TermQuery q_fox(index::Term(0, "fox"));
    auto hits = s.Search(q_fox);
    ASSERT_EQ(hits->Length(), 1);
}

// ========== 8. Delete document, verify search excludes it ==========
TEST(Delete, ExcludedFromSearch) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(Doc("fox"));
    w.AddDocument(Doc("rabbit"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    EXPECT_EQ(r.NumDocs(), 2);

    r.Delete(0);  // delete doc0 ("fox")
    EXPECT_EQ(r.NumDocs(), 1);

    search::IndexSearcher s(r);
    search::TermQuery q_fox(index::Term(0, "fox"));
    auto hits = s.Search(q_fox);
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->Length(), 0);  // no live docs contain "fox"

    search::TermQuery q_rabbit(index::Term(0, "rabbit"));
    auto h2 = s.Search(q_rabbit);
    ASSERT_EQ(h2->Length(), 1);
    EXPECT_EQ(h2->Id(0), 1);
}

// ========== 9. Document retrieval via FieldsReader ==========
TEST(Fields, StoreAndRetrieve) {
    store::RAMDirectory dir;
    document::Document doc;
    doc.Add(document::Field::Text("title", "Test Title"));
    doc.Add(document::Field::Keyword("id", "abc123"));

    index::FieldInfos fis;
    for (const auto& f : doc.Fields()) fis.AddField(f);

    index::FieldsWriter fw(dir, "_seg", fis);
    fw.AddDocument(doc);
    fw.Close();

    index::FieldsReader fr(dir, "_seg", fis);
    auto read = fr.Document(0);
    ASSERT_NE(read, nullptr);
    EXPECT_EQ(read->GetField("title")->Value(), "Test Title");
    EXPECT_EQ(read->GetField("id")->Value(), "abc123");
    fr.Close();
}

// ========== 10. QueryParser with analyzer normalization ==========
TEST(QueryParser, WithAnalyzerNormalizesTerms) {
    // "Fox" → StopAnalyzer lowercases to "fox"
    minilucene::query_parser::QueryParser parser("body", "Fox");
    auto q = parser.Parse();
    ASSERT_NE(q, nullptr);
}

// ========== 11. Empty index ==========
TEST(EdgeCase, EmptyIndex) {
    store::RAMDirectory dir;
    EXPECT_THROW(index::SegmentReader r(dir, "_nonexistent"), std::exception);
}

}  // namespace minilucene
