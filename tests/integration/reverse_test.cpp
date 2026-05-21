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

// ===== Merge 结果正确（两个单 doc 段） =====
TEST(ReverseTest, MergeTwoSingleDocSegments) {
    store::RAMDirectory dir;

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

// ===== Merge 跳过已删文档（Bug 3 回归保护） =====
TEST(ReverseTest, MergeWithDeletedDocs) {
    store::RAMDirectory dir;

    {
        auto a1 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a1);
        dw.AddDocument(DocWithId("1", "alpha"));
        dw.AddDocument(DocWithId("2", "beta"));
        dw.Flush("_a");
    }
    {
        auto a2 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a2);
        dw.AddDocument(DocWithId("3", "gamma"));
        dw.AddDocument(DocWithId("4", "delta"));
        dw.Flush("_b");
    }

    // Delete doc 0 from _a ("alpha") and doc 1 from _b ("delta")
    {
        index::SegmentReader r(dir, "_a");
        r.Delete(0);
        r.Close();
    }
    {
        index::SegmentReader r(dir, "_b");
        r.Delete(1);
        r.Close();
    }

    index::SegmentMerger merger(dir, {"_a", "_b"}, "_merged");
    merger.Merge();

    index::SegmentReader r(dir, "_merged");
    // Only 2 live docs survive
    EXPECT_EQ(r.NumDocs(), 2);

    // Verify stored fields: only id=2 ("beta") and id=3 ("gamma") survive
    auto doc0 = r.Document(0);
    ASSERT_NE(doc0, nullptr);
    EXPECT_EQ(doc0->GetField("id")->Value(), "2");

    auto doc1 = r.Document(1);
    ASSERT_NE(doc1, nullptr);
    EXPECT_EQ(doc1->GetField("id")->Value(), "3");

    // Deleted docs' terms should have doc_freq=0
    // DocWithId adds Keyword("id", field 0) + Text("body", field 1)
    // Terms are in field 1 ("body")
    EXPECT_EQ(r.DocFreq(index::Term(1, "alpha")), 0);
    EXPECT_EQ(r.DocFreq(index::Term(1, "delta")), 0);
    // Live docs' terms should have doc_freq=1
    EXPECT_EQ(r.DocFreq(index::Term(1, "beta")), 1);
    EXPECT_EQ(r.DocFreq(index::Term(1, "gamma")), 1);
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
// Java BooleanScorer: maxCoord = 1 (初始值) + 每个非禁止子句
// 2 SHOULD clauses → maxCoord = 1 + 2 = 3
// coord = overlap / maxCoord
TEST(ReverseTest, BooleanCoordScoring) {
    // doc0: "fox" (1 token)
    // doc1: "fox jumps" (2 tokens)
    // 2 SHOULD clauses: max_overlap = 1 + 2 = 3
    // doc0: fox at doc0 (overlap=1). coord=1/3≈0.333
    // doc1: fox+jumps at doc1 (overlap=2). coord=2/3≈0.667
    //
    // idf(fox) = log(2/(2+1))+1 = 0.595
    // idf(jumps) = log(2/(1+1))+1 = 1.0
    //
    // TermScore("fox", doc0) = sqrt(1) × 0.595² × 1.0(norm) = 0.354
    // TermScore("fox", doc1) = sqrt(1) × 0.595² × 0.706(norm) = 0.250
    // TermScore("jumps", doc1) = sqrt(1) × 1.0² × 0.706(norm) = 0.706
    //
    // BooleanScore(doc0) = 0.354 × 0.333 = 0.118
    // BooleanScore(doc1) = (0.250 + 0.706) × 0.667 = 0.637

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
    EXPECT_EQ(hits->Id(1), 0) << "Doc0 (matches one) ranks second";

    // Doc1 matches both → higher score
    EXPECT_GT(hits->Score(0), hits->Score(1));
    // Oracle from Java BooleanScorer formula (maxCoord = 1 + 2 = 3)
    EXPECT_NEAR(hits->Score(1), 0.118f, 0.01f);
    EXPECT_NEAR(hits->Score(0), 0.637f, 0.01f);
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

// ===== Forensic 1: Merge 后逐字节验证位置 delta =====
TEST(ReverseTest, MergePositionDeltasExact) {
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
        dw.AddDocument(D("fox fox"));
        dw.Flush("_b");
    }

    index::SegmentMerger merger(dir, {"_a", "_b"}, "_merged");
    merger.Merge();

    // "fox" appears in doc0 (freq=1, pos=0) and doc1 (freq=2, pos=0, pos=1)
    index::SegmentReader r(dir, "_merged");
    auto tp = r.Positions(index::Term(0, "fox"));
    ASSERT_NE(tp, nullptr);

    ASSERT_TRUE(tp->Next());
    EXPECT_EQ(tp->Doc(), 0);
    EXPECT_EQ(tp->Freq(), 1);
    int pos = 0;
    pos += tp->NextPosition();
    EXPECT_EQ(pos, 0);

    ASSERT_TRUE(tp->Next());
    EXPECT_EQ(tp->Doc(), 1);
    EXPECT_EQ(tp->Freq(), 2);
    pos = 0;
    pos += tp->NextPosition();
    EXPECT_EQ(pos, 0);
    pos += tp->NextPosition();
    EXPECT_EQ(pos, 1);

    EXPECT_FALSE(tp->Next());
}

// ===== Forensic 2: Merge 前后 term 总频次不变量 =====
TEST(ReverseTest, MergeInvariantTotalTermFreq) {
    store::RAMDirectory dir;

    int total_before = 0;
    {
        auto a1 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a1);
        dw.AddDocument(D("fox fox rabbit"));
        dw.Flush("_a");
        // _a has 2 docs? No, just 1 doc so _a is a single segment
    }
    // Actually let's make 2 segments
    {
        auto a2 = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a2);
        dw.AddDocument(D("rabbit fox"));
        dw.Flush("_b");
    }

    // Count total docFreq across all source segments
    for (auto& seg : {"_a", "_b"}) {
        index::SegmentReader r(dir, seg);
        auto terms = r.Terms();
        while (terms->Next()) {
            total_before += r.DocFreq(terms->Current());
        }
    }

    index::SegmentMerger merger(dir, {"_a", "_b"}, "_merged");
    merger.Merge();

    // Count total docFreq in merged segment
    int total_after = 0;
    index::SegmentReader r(dir, "_merged");
    auto terms = r.Terms();
    while (terms->Next()) {
        total_after += r.DocFreq(terms->Current());
    }

    EXPECT_EQ(total_before, total_after) << "merge must preserve sum of docFreq";
}

// ===== Forensic 3: BooleanQuery coord 精确匹配 Java 公式 =====
TEST(ReverseTest, CoordWithMustAndShould) {
    // 1 MUST + 1 SHOULD → Java maxCoord = 1 + 1 + 1 = 3
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(D("fox"));
    w.AddDocument(D("fox jumps"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    search::BooleanQuery bq;
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "fox")), search::Occur::MUST);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "jumps")), search::Occur::SHOULD);

    auto hits = s.Search(bq);
    ASSERT_NE(hits, nullptr);
    ASSERT_EQ(hits->Length(), 2);

    // doc0 "fox": overlap=1 (MUST fox), coord=1/3
    // TermScore("fox", doc0) = sqrt(1) × 0.595² × 1.0 = 0.354
    // BooleanScore(doc0) = 0.354 × 1/3 = 0.118
    EXPECT_EQ(hits->Id(1), 0);
    EXPECT_NEAR(hits->Score(1), 0.118f, 0.01f);

    // doc1 "fox jumps": overlap=2 (fox+jumps), coord=2/3
    // TermScore("fox", doc1) = 0.250, TermScore("jumps", doc1) = 0.706
    // BooleanScore(doc1) = (0.250+0.706) × 2/3 = 0.637
    EXPECT_EQ(hits->Id(0), 1);
    EXPECT_NEAR(hits->Score(0), 0.637f, 0.01f);
}

// ===== Forensic 4: 跨段位置逐位验证 =====
TEST(ReverseTest, MultiSegmentPositionByPosition) {
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
        dw.AddDocument(D("fox quick brown"));
        dw.Flush("_b");
    }

    index::SegmentInfos sis;
    sis.Add("_a", 1); sis.Add("_b", 1);
    sis.Write(dir);

    index::SegmentsReader reader(dir);
    search::IndexSearcher s(reader);

    // "quick" positions: doc0 (_a) pos=0, doc1 (_b) pos=1
    auto tp_q = reader.Positions(index::Term(0, "quick"));
    ASSERT_NE(tp_q, nullptr);
    ASSERT_TRUE(tp_q->Next());
    EXPECT_EQ(tp_q->Doc(), 0);
    EXPECT_EQ(tp_q->Freq(), 1);
    int p = 0; p += tp_q->NextPosition();
    EXPECT_EQ(p, 0);
    ASSERT_TRUE(tp_q->Next());
    EXPECT_EQ(tp_q->Doc(), 1);
    EXPECT_EQ(tp_q->Freq(), 1);
    p = 0; p += tp_q->NextPosition();
    EXPECT_EQ(p, 1);

    // "brown" positions: doc0 (_a) pos=1, doc1 (_b) pos=2
    auto tp_b = reader.Positions(index::Term(0, "brown"));
    ASSERT_NE(tp_b, nullptr);
    ASSERT_TRUE(tp_b->Next());
    EXPECT_EQ(tp_b->Doc(), 0);
    p = 0; p += tp_b->NextPosition();
    EXPECT_EQ(p, 1);
    ASSERT_TRUE(tp_b->Next());
    EXPECT_EQ(tp_b->Doc(), 1);
    p = 0; p += tp_b->NextPosition();
    EXPECT_EQ(p, 2);
}

// ===== Forensic 5: Optimize 幂等性 =====
TEST(ReverseTest, OptimizeIdempotentDeep) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(D("fox rabbit"));
    w.AddDocument(D("quick brown fox"));
    w.AddDocument(D("rabbit jumps"));
    w.Close();

    // First optimize
    auto a2 = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w2(dir, std::move(a2));
    w2.Optimize();
    w2.Close();

    auto seg_infos1 = index::SegmentInfos::Read(dir);
    ASSERT_GE(seg_infos1->Segments().size(), 1);
    index::SegmentReader r1(dir, seg_infos1->Segments()[0].name);
    int num_docs_1 = r1.NumDocs();
    auto terms1 = r1.Terms();
    std::set<std::string> term_set1;
    while (terms1->Next()) term_set1.insert(terms1->Current().Text());
    terms1->Close();

    // Second optimize
    auto a3 = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w3(dir, std::move(a3));
    w3.Optimize();
    w3.Close();

    auto seg_infos2 = index::SegmentInfos::Read(dir);
    ASSERT_GE(seg_infos2->Segments().size(), 1);
    index::SegmentReader r2(dir, seg_infos2->Segments()[0].name);
    EXPECT_EQ(r2.NumDocs(), num_docs_1) << "second optimize must not change doc count";
    auto terms2 = r2.Terms();
    std::set<std::string> term_set2;
    while (terms2->Next()) term_set2.insert(terms2->Current().Text());
    terms2->Close();

    EXPECT_EQ(term_set1, term_set2) << "term set must be stable after second optimize";
}

// ===== Forensic 6: 位置精确值校验（已知输入 → 手算 expected）=====
TEST(ReverseTest, PositionsExactValues) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    // doc0 "a b a b a": a@0,2,4; b@1,3
    // doc1 "b a b": b@0,2; a@1
    w.AddDocument(D("a b a b a"));
    w.AddDocument(D("b a b"));
    w.Close();

    index::SegmentReader r(dir, "_0");

    // Term "a": doc0 freq=3 positions[0,2,4]; doc1 freq=1 position[1]
    // deltas (NextPosition returns raw delta): doc0=[0,2,2], doc1=[1]
    auto tp_a = r.Positions(index::Term(0, "a"));
    ASSERT_NE(tp_a, nullptr);
    ASSERT_TRUE(tp_a->Next());
    EXPECT_EQ(tp_a->Doc(), 0);
    EXPECT_EQ(tp_a->Freq(), 3);
    EXPECT_EQ(tp_a->NextPosition(), 0);
    EXPECT_EQ(tp_a->NextPosition(), 2);
    EXPECT_EQ(tp_a->NextPosition(), 2);
    ASSERT_TRUE(tp_a->Next());
    EXPECT_EQ(tp_a->Doc(), 1);
    EXPECT_EQ(tp_a->Freq(), 1);
    EXPECT_EQ(tp_a->NextPosition(), 1);
    EXPECT_FALSE(tp_a->Next());

    // Term "b": doc0 freq=2 positions[1,3]; doc1 freq=2 positions[0,2]
    // deltas: doc0=[1,2], doc1=[0,2]
    auto tp_b = r.Positions(index::Term(0, "b"));
    ASSERT_NE(tp_b, nullptr);
    ASSERT_TRUE(tp_b->Next());
    EXPECT_EQ(tp_b->Doc(), 0);
    EXPECT_EQ(tp_b->Freq(), 2);
    EXPECT_EQ(tp_b->NextPosition(), 1);
    EXPECT_EQ(tp_b->NextPosition(), 2);
    ASSERT_TRUE(tp_b->Next());
    EXPECT_EQ(tp_b->Doc(), 1);
    EXPECT_EQ(tp_b->Freq(), 2);
    EXPECT_EQ(tp_b->NextPosition(), 0);
    EXPECT_EQ(tp_b->NextPosition(), 2);
    EXPECT_FALSE(tp_b->Next());
}

// ===== Forensic 7: 搜索结果排序 + 精确分数值 =====
TEST(ReverseTest, SearchResultsSortedByScore) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    // doc0: "fox jumps quick" (3 tokens)
    // doc1: "fox rabbit" (2 tokens)
    // doc2: "quick brown fox jumps" (4 tokens)
    w.AddDocument(D("fox jumps quick"));
    w.AddDocument(D("fox rabbit"));
    w.AddDocument(D("quick brown fox jumps"));
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    // Hand-calculated: TermQuery("fox")
    // idf = log(3/(3+1)) + 1 = 0.712
    // doc1 ("fox rabbit", 2 tokens): norm=0.706, score=1×0.712²×0.706=0.358
    // doc0 ("fox jumps quick", 3 tokens): norm=0.576, score=1×0.712²×0.576=0.292
    // doc2 ("quick brown fox jumps", 4 tokens): norm=0.498, score=1×0.712²×0.498=0.253
    auto hits = s.Search(search::TermQuery(index::Term(0, "fox")));
    ASSERT_NE(hits, nullptr);
    ASSERT_EQ(hits->Length(), 3);
    EXPECT_EQ(hits->Id(0), 1) << "doc1 (shortest) ranks first";
    EXPECT_EQ(hits->Id(1), 0) << "doc0 (medium) ranks second";
    EXPECT_EQ(hits->Id(2), 2) << "doc2 (longest) ranks third";
    EXPECT_NEAR(hits->Score(0), 0.358f, 0.01f);

    // Ensure scores are strictly decreasing
    for (int i = 1; i < hits->Length(); ++i) {
        EXPECT_GT(hits->Score(i - 1), hits->Score(i));
    }

    // BooleanQuery +fox jumps: maxCoord=1+1+1=3
    // doc1 has no "jumps" → overlap=1, coord=1/3 → lower than all fox docs
    search::BooleanQuery bq;
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "fox")), search::Occur::MUST);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "jumps")), search::Occur::SHOULD);
    auto bq_hits = s.Search(bq);
    ASSERT_NE(bq_hits, nullptr);
    EXPECT_EQ(bq_hits->Length(), 3);
    EXPECT_EQ(bq_hits->Id(2), 1) << "doc1 (no jumps) ranks last with BooleanQuery";
    for (int i = 1; i < bq_hits->Length(); ++i) {
        EXPECT_GE(bq_hits->Score(i - 1), bq_hits->Score(i));
    }

    // PhraseQuery should return results sorted
    search::PhraseQuery pq;
    pq.Add(index::Term(0, "quick"));
    pq.Add(index::Term(0, "brown"));
    auto pq_hits = s.Search(pq);
    ASSERT_NE(pq_hits, nullptr);
    EXPECT_EQ(pq_hits->Length(), 1);
}

}  // namespace minilucene
