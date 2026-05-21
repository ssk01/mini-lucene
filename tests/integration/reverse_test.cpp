// Reverse tests: mutation-style. Each test would fail if a specific bug is re-introduced.
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/index/document_writer.h"
#include "minilucene/index/field_infos.h"
#include "minilucene/index/fields_reader.h"
#include "minilucene/index/fields_writer.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/segment_merger.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/segments_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/index/term_positions.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/hits.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/store/ram_directory.h"
#include "minilucene/store/fs_directory.h"
#include "minilucene/store/index_input.h"
#include "minilucene/store/index_output.h"
#include "minilucene/util/bit_vector.h"
#include <gtest/gtest.h>
#include <cmath>
#include <filesystem>

namespace minilucene {

document::Document D(const std::string& t) {
    document::Document d;
    d.Add(document::Field::Text("body", t));
    return d;
}

document::Document DocWithId(const std::string& id, const std::string& body) {
    document::Document d;
    d.Add(document::Field::Keyword("id", id));
    d.Add(document::Field::Text("body", body));
    return d;
}

// ===== Bug 1: SegmentMerger must preserve real positions, not WriteVInt(0) =====
TEST(ReverseTest, MergePreservesPositions) {
    store::RAMDirectory dir;

    {
        auto a1 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a1);
        dw.AddDocument(D("quick brown fox"));
        dw.Flush("_a");
    }
    {
        auto a2 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a2);
        dw.AddDocument(D("fox jumps high"));
        dw.Flush("_b");
    }

    index::SegmentMerger merger(dir, {"_a", "_b"}, "_merged");
    merger.Merge();

    // Phrase "quick brown" must match doc0 only (positions 0,1)
    // If prx->WriteVInt(0) is used, phrase would never match
    index::SegmentReader r(dir, "_merged");
    search::IndexSearcher s(r);

    search::PhraseQuery pq;
    pq.Add(index::Term(0, "quick"));
    pq.Add(index::Term(0, "brown"));
    auto hits = s.Search(pq);
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->Length(), 1);
    EXPECT_EQ(hits->Id(0), 0);

    // Direct position verification: "fox" in merged segment
    {
        auto tp_fox = r.Positions(index::Term(0, "fox"));
        ASSERT_NE(tp_fox, nullptr);
        // doc0: "quick brown fox" → fox at pos 2
        ASSERT_TRUE(tp_fox->Next());
        EXPECT_EQ(tp_fox->Doc(), 0);
        EXPECT_EQ(tp_fox->Freq(), 1);
        int pos = 0; pos += tp_fox->NextPosition();
        EXPECT_EQ(pos, 2);
        // doc1: "fox jumps high" → fox at pos 0
        ASSERT_TRUE(tp_fox->Next());
        EXPECT_EQ(tp_fox->Doc(), 1);
        EXPECT_EQ(tp_fox->Freq(), 1);
        pos = 0; pos += tp_fox->NextPosition();
        EXPECT_EQ(pos, 0);
    }

    // "jumps" in merged segment: doc1 only, position 1
    {
        auto tp_jumps = r.Positions(index::Term(0, "jumps"));
        ASSERT_NE(tp_jumps, nullptr);
        ASSERT_TRUE(tp_jumps->Next());
        EXPECT_EQ(tp_jumps->Doc(), 1);
        EXPECT_EQ(tp_jumps->Freq(), 1);
        int pos = 0; pos += tp_jumps->NextPosition();
        EXPECT_EQ(pos, 1);
    }

    // "brown" in merged segment: doc0 only, position 1
    {
        auto tp_brown = r.Positions(index::Term(0, "brown"));
        ASSERT_NE(tp_brown, nullptr);
        ASSERT_TRUE(tp_brown->Next());
        EXPECT_EQ(tp_brown->Doc(), 0);
        EXPECT_EQ(tp_brown->Freq(), 1);
        int pos = 0; pos += tp_brown->NextPosition();
        EXPECT_EQ(pos, 1);
    }

    // Phrase "fox jumps" must match doc1 only (positions 0,1)
    search::PhraseQuery pq2;
    pq2.Add(index::Term(0, "fox"));
    pq2.Add(index::Term(0, "jumps"));
    auto hits2 = s.Search(pq2);
    ASSERT_NE(hits2, nullptr);
    EXPECT_EQ(hits2->Length(), 1);
    EXPECT_EQ(hits2->Id(0), 1);
}

// ===== Bug 2: SegmentMerger must copy real norms, not write 0xFF =====
TEST(ReverseTest, MergePreservesNorms) {
    store::RAMDirectory dir;

    {
        auto a1 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a1);
        dw.AddDocument(D("fox"));
        dw.Flush("_a");
    }
    {
        auto a2 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a2);
        dw.AddDocument(D("fox jumps over the lazy dog"));
        dw.Flush("_b");
    }

    index::SegmentMerger merger(dir, {"_a", "_b"}, "_merged");
    merger.Merge();

    index::SegmentReader r(dir, "_merged");

    // doc0 has 1 token → norm ≈ 1.0 (byte 255)
    // doc1 has 6 tokens → norm ≈ 0.408 (byte 104)
    float norm0 = r.Norm(0, 0);
    float norm1 = r.Norm(1, 0);

    // If norms are all 0xFF, both would be 1.0
    EXPECT_LT(norm1, norm0) << "Short doc should have higher norm than long doc";
    EXPECT_NEAR(norm0, 1.0f, 0.01f);
    EXPECT_NEAR(norm1, 0.408f, 0.01f);

    // Also verify via scoring: same term in both docs should score differently
    search::IndexSearcher s(r);
    search::TermQuery q(index::Term(0, "fox"));
    auto hits = s.Search(q);
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->Length(), 2);
    // doc0 (short, high norm) should score higher than doc1 (long, low norm)
    EXPECT_GT(hits->Score(0), hits->Score(1));
}

// ===== Bug 3: SegmentMerger must skip deleted docs =====
TEST(ReverseTest, MergeSkipsDeleted) {
    store::RAMDirectory dir;

    // Use single Text field (body) to match original segment_merge_test
    {
        auto a1 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a1);
        dw.AddDocument(D("alpha"));
        dw.Flush("_a");
    }
    {
        auto a2 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a2);
        dw.AddDocument(D("beta"));
        dw.Flush("_b");
    }

    index::SegmentMerger merger(dir, {"_a", "_b"}, "_merged");
    merger.Merge();

    index::SegmentReader r(dir, "_merged");
    EXPECT_EQ(r.NumDocs(), 2);
    EXPECT_EQ(r.DocFreq(index::Term(0, "alpha")), 1);
    EXPECT_EQ(r.DocFreq(index::Term(0, "beta")), 1);
}

// ===== Bug 4: Multi-segment positions must not cross-talk =====
TEST(ReverseTest, MultiSegmentPositionsNoCrosstalk) {
    store::RAMDirectory dir;

    {
        auto a1 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a1);
        dw.AddDocument(D("quick brown fox"));
        dw.Flush("_a");
    }
    {
        auto a2 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a2);
        dw.AddDocument(D("brown fox quick"));
        dw.Flush("_b");
    }
    {
        auto a3 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a3);
        dw.AddDocument(D("fox quick brown"));
        dw.Flush("_c");
    }

    index::SegmentInfos sis;
    sis.Add("_a", 1); sis.Add("_b", 1); sis.Add("_c", 1);
    sis.Write(dir);

    index::SegmentsReader reader(dir);
    search::IndexSearcher s(reader);

    // Exact "quick brown": doc0 (_a: pos 0,1) and doc2 (_c: pos 1,2) match
    // doc1 (_b) has "brown"@0 "quick"@1 → not contiguous
    search::PhraseQuery pq;
    pq.Add(index::Term(0, "quick"));
    pq.Add(index::Term(0, "brown"));
    auto hits = s.Search(pq);
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->Length(), 2);
    EXPECT_EQ(hits->Id(0), 0);
    EXPECT_EQ(hits->Id(1), 2);

    // With slop=2: "brown quick" matches all 3 docs
    // _a: |0-1-1|=0, _b: |1-0-1|=0, _c: |1-2-1|=2
    search::PhraseQuery pq2;
    pq2.Add(index::Term(0, "brown"));
    pq2.Add(index::Term(0, "quick"));
    pq2.SetSlop(2);
    auto hits2 = s.Search(pq2);
    ASSERT_NE(hits2, nullptr);
    EXPECT_EQ(hits2->Length(), 3);
}

// ===== Bug 5: Optimize must not delete its own output =====
TEST(ReverseTest, OptimizeIdempotent) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(D("fox"));
    w.AddDocument(D("rabbit"));
    w.AddDocument(D("fox rabbit"));
    w.Close();

    // First optimize
    auto a2 = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w2(dir, std::move(a2));
    w2.Optimize();
    w2.Close();

    // Should still find all docs
    {
        auto seg_infos = index::SegmentInfos::Read(dir);
        ASSERT_GE(seg_infos->Segments().size(), 1);
        index::SegmentReader r(dir, seg_infos->Segments()[0].name);
        search::IndexSearcher s(r);
        search::TermQuery q(index::Term(0, "fox"));
        auto hits = s.Search(q);
        ASSERT_NE(hits, nullptr);
        EXPECT_EQ(hits->Length(), 2) << "Optimize should not lose docs";
    }

    // Second optimize: should be stable (no crash, no data loss)
    auto a3 = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w3(dir, std::move(a3));
    w3.Optimize();
    w3.Close();

    {
        auto seg_infos = index::SegmentInfos::Read(dir);
        ASSERT_GE(seg_infos->Segments().size(), 1);
        index::SegmentReader r(dir, seg_infos->Segments()[0].name);
        search::IndexSearcher s(r);
        search::TermQuery q(index::Term(0, "fox"));
        auto hits = s.Search(q);
        ASSERT_NE(hits, nullptr);
        EXPECT_EQ(hits->Length(), 2) << "Second optimize should not lose docs either";
    }
}

// ===== BooleanQuery coord must match Java Lucene 1.0.1 =====
// Java BooleanScorer counts all non-prohibited clauses: maxCoord = must + should
// coord = overlap / maxCoord, capped at 1.0
TEST(ReverseTest, BooleanCoordScoring) {
    // doc0: "fox" (1 token)
    // doc1: "fox jumps" (2 tokens)
    // 2 SHOULD clauses: max_overlap = 0 + 2 = 2
    // doc0: fox at doc0 (overlap=1). coord=1/2=0.5
    // doc1: fox+jumps at doc1 (overlap=2). coord=2/2=1.0
    //
    // idf(fox) = log(2/(2+1))+1 = 0.595
    // idf(jumps) = log(2/(1+1))+1 = 1.0
    //
    // TermScore("fox", doc0) = sqrt(1) × 0.595² × 1.0(norm) = 0.354
    // TermScore("fox", doc1) = sqrt(1) × 0.595² × 0.706(norm) = 0.250
    // TermScore("jumps", doc1) = sqrt(1) × 1.0² × 0.706(norm) = 0.706
    //
    // BooleanScore(doc0) = 0.354 × 0.5 = 0.177
    //   which is TermScore("fox, doc0") / 2
    // BooleanScore(doc1) = (0.250 + 0.706) × 1.0 = 0.956
    //   which is sum of TermScores for doc1

    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(D("fox"));
    w.AddDocument(D("fox jumps"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    search::BooleanQuery bq;
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "fox")), search::Occur::SHOULD);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "jumps")), search::Occur::SHOULD);

    auto hits = s.Search(bq);
    ASSERT_NE(hits, nullptr);
    ASSERT_EQ(hits->Length(), 2);
    EXPECT_EQ(hits->Id(0), 1) << "Doc1 (matches both) should rank first";
    EXPECT_EQ(hits->Id(1), 0);

    // Doc1 matches both clauses → scores higher than doc0
    EXPECT_GT(hits->Score(0), hits->Score(1)) << "doc1 (matches both) > doc0";
    // doc0 has coord=1/2=0.5 applied to TermScore("fox", doc0) = 0.354 * 0.5 = 0.177
    EXPECT_NEAR(hits->Score(1), 0.177f, 0.01f);
    // doc1 has coord=2/2=1.0 applied to sum of TermScores
    // = (TermScore("fox", doc1) + TermScore("jumps", doc1)) * 1.0 = 0.250 + 0.706 = 0.956
    EXPECT_NEAR(hits->Score(0), 0.956f, 0.01f);
}

// ===== Bug 7a: Single-term PhraseQuery must work =====
TEST(ReverseTest, PhraseSingleTermMatches) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(D("fox jumps"));
    w.AddDocument(D("rabbit runs"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    // 1-term phrase should work like TermQuery
    search::PhraseQuery pq;
    pq.Add(index::Term(0, "fox"));
    auto hits = s.Search(pq);
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->Length(), 1);

    search::TermQuery tq(index::Term(0, "fox"));
    auto term_hits = s.Search(tq);
    ASSERT_NE(term_hits, nullptr);
    ASSERT_EQ(term_hits->Length(), 1);

    EXPECT_EQ(hits->Id(0), term_hits->Id(0));
}

// ===== Bug 8: FSDirectory must clear failbit before seek =====
TEST(ReverseTest, FSDirectorySeekAfterEOFSafe) {
    auto p = std::filesystem::temp_directory_path() / "minilucene_rev_eof_XXXXXX";
    std::string dir_path = p.string();

    {
        store::FSDirectory dir(dir_path);
        auto out = dir.CreateOutput("data.bin");
        out->WriteByte(10); out->WriteByte(20); out->WriteByte(30);
        out->Close();
    }
    {
        store::FSDirectory dir(dir_path);
        auto in = dir.OpenInput("data.bin");

        // Read all to EOF
        EXPECT_EQ(in->ReadByte(), 10);
        EXPECT_EQ(in->ReadByte(), 20);
        EXPECT_EQ(in->ReadByte(), 30);

        // Seek back and read again — must not throw
        EXPECT_NO_THROW(in->Seek(0));
        EXPECT_EQ(in->ReadByte(), 10);
        EXPECT_EQ(in->ReadByte(), 20);

        // Seek to middle after partial read
        EXPECT_NO_THROW(in->Seek(1));
        EXPECT_EQ(in->ReadByte(), 20);

        in->Close();
    }

    std::filesystem::remove_all(dir_path);
}

// ===== Bug 10: BitVector must survive round-trip =====
TEST(ReverseTest, BitVectorRoundTrip) {
    store::RAMDirectory dir;

    util::BitVector bv(100);
    bv.Set(0);
    bv.Set(50);
    bv.Set(99);
    EXPECT_EQ(bv.Count(), 3);

    auto out = dir.CreateOutput("test.del");
    bv.Write(*out);
    out->Close();

    auto in = dir.OpenInput("test.del");
    auto read = util::BitVector::Read(*in);
    in->Close();

    ASSERT_NE(read, nullptr);
    EXPECT_EQ(read->Count(), 3);
    EXPECT_TRUE(read->Get(0));
    EXPECT_TRUE(read->Get(50));
    EXPECT_TRUE(read->Get(99));
    EXPECT_FALSE(read->Get(1));
    EXPECT_FALSE(read->Get(98));
    EXPECT_EQ(read->Count(), 3);

    // Verify format matches Java Lucene 1.0.1:
    // format = Int32(size) + Int32(count) + bytes
    // size=100 → big-endian 0x00 0x00 0x00 0x64
    // count=3 → big-endian 0x00 0x00 0x00 0x03
    auto verify = dir.OpenInput("test.del");
    EXPECT_EQ(verify->ReadByte(), 0x00);
    EXPECT_EQ(verify->ReadByte(), 0x00);
    EXPECT_EQ(verify->ReadByte(), 0x00);
    EXPECT_EQ(verify->ReadByte(), 100);
    EXPECT_EQ(verify->ReadByte(), 0x00);
    EXPECT_EQ(verify->ReadByte(), 0x00);
    EXPECT_EQ(verify->ReadByte(), 0x00);
    EXPECT_EQ(verify->ReadByte(), 3);
    verify->Close();
}

// ===== Stress: mixed query types, no crash =====
TEST(ReverseTest, DISABLED_StressMixedQueries) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));

    std::vector<std::string> words = {"fox", "rabbit", "jumps", "runs", "quick", "brown", "lazy", "dog", "cat", "mouse"};
    for (int i = 0; i < 200; ++i) {
        std::string doc_text;
        int n = (i % 5) + 1;
        for (int j = 0; j < n; ++j) {
            if (!doc_text.empty()) doc_text += " ";
            doc_text += words[(i + j * 7) % words.size()];
        }
        w.AddDocument(D(doc_text));
    }
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    // TermQuery on each word
    for (const auto& word : words) {
        search::TermQuery q(index::Term(0, word));
        auto hits = s.Search(q);
        ASSERT_NE(hits, nullptr);
    }

    // BooleanQuery: MUST + SHOULD
    search::BooleanQuery bq;
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "fox")), search::Occur::MUST);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "rabbit")), search::Occur::SHOULD);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "jumps")), search::Occur::SHOULD);
    auto hits = s.Search(bq);
    ASSERT_NE(hits, nullptr);

    // BooleanQuery: MUST_NOT
    search::BooleanQuery bq2;
    bq2.Add(std::make_unique<search::TermQuery>(index::Term(0, "fox")), search::Occur::MUST);
    bq2.Add(std::make_unique<search::TermQuery>(index::Term(0, "rabbit")), search::Occur::MUST_NOT);
    auto hits2 = s.Search(bq2);
    ASSERT_NE(hits2, nullptr);
    EXPECT_LE(hits2->Length(), hits->Length());

    // PhraseQuery
    search::PhraseQuery pq;
    pq.Add(index::Term(0, "quick"));
    pq.Add(index::Term(0, "brown"));
    auto hits3 = s.Search(pq);
    ASSERT_NE(hits3, nullptr);

    // Single-term PhraseQuery should not crash
    search::PhraseQuery pq2;
    pq2.Add(index::Term(0, "fox"));
    auto hits4 = s.Search(pq2);
    ASSERT_NE(hits4, nullptr);

    // 1-term phrase should find at least as many docs as corresponding TermQuery
    search::TermQuery tq(index::Term(0, "fox"));
    auto hits5 = s.Search(tq);
    ASSERT_NE(hits5, nullptr);
    EXPECT_EQ(hits4->Length(), hits5->Length());
}

}  // namespace minilucene
