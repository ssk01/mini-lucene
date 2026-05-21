// forensic_claude_test.cpp
//
// Owned by claude-opus-4-7 per AGENTS.md §2.
// Oracles in this file come ONLY from the §5 white-listed sources:
//   - hand-computed values (with derivation in comments)
//   - Lucene 1.0.1 spec / Java source references
//   - mathematical / scenario invariants
//
// deepseek MUST NOT modify this file. If a test fails, the implementation is
// suspect — not the test. Push back via REFLECTION.md only if the oracle
// derivation in the comment is wrong.

#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/segments_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/search/hits.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/store/ram_directory.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace minilucene {

namespace {

// Build a document with an indexed-stored `id` (Keyword) and a tokenized
// `body` (Text). Keyword fields are stored and indexed as a single token.
document::Document MakeDoc(const std::string& id, const std::string& body) {
    document::Document d;
    d.Add(document::Field::Keyword("id", id));
    d.Add(document::Field::Text("body", body));
    return d;
}

}  // namespace

// =============================================================================
// Forensic Test 1: Delete + Optimize + Stored field read consistency
// =============================================================================
//
// Scenario:
//   1. Write 5 docs with id = "doc-1" .. "doc-5" (single segment).
//   2. Open reader, delete slots {0, 2} (i.e. id "doc-1" and "doc-3").
//   3. Reopen writer, Optimize() (which compacts and drops deleted slots).
//   4. Reopen reader.
//
// Oracle (scenario invariant — NOT derived from implementation output):
//   After optimize:
//     - NumDocs == 3
//     - MaxDoc  == 3   (optimize compacts deleted-marker bits away)
//     - The remaining docs, in their original write order, are:
//         slot 0 -> id "doc-2"
//         slot 1 -> id "doc-4"
//         slot 2 -> id "doc-5"
//   The id values themselves are exactly what we wrote — there is no
//   transformation between Field::Keyword input and Document::GetField output.
//
// This test catches mutation classes such as:
//   - Optimize forgetting to skip deleted docs
//   - Stored-field offsets reshuffled by optimize
//   - Keyword field value lost / truncated during merge
//   - Delete bitmap not honored when reading stored fields
TEST(ForensicClaude, DeleteOptimizeStoredFieldReadConsistency) {
    store::RAMDirectory dir;

    // Step 1: write 5 docs into a single segment (mergeFactor large -> no merge yet).
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.mergeFactor = 10000;
        for (int i = 1; i <= 5; ++i) {
            w.AddDocument(MakeDoc("doc-" + std::to_string(i), "body content"));
        }
        w.Close();
    }

    // Step 2: open reader, delete slots {0, 2} (= id "doc-1" and "doc-3").
    {
        index::SegmentsReader r(dir);
        ASSERT_EQ(r.NumDocs(), 5) << "precondition: 5 docs before delete";
        ASSERT_EQ(r.MaxDoc(), 5);
        r.Delete(0);
        r.Delete(2);
        EXPECT_EQ(r.NumDocs(), 3) << "after delete: NumDocs should reflect tombstones";
        r.Close();
    }

    // Step 3: optimize. Optimize is supposed to drop deleted slots physically.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.Optimize();
        w.Close();
    }

    // Step 4: verify the surviving docs.
    {
        index::SegmentsReader r(dir);
        EXPECT_EQ(r.NumDocs(), 3) << "after optimize: NumDocs == surviving count";
        EXPECT_EQ(r.MaxDoc(), 3)
            << "after optimize: MaxDoc should equal NumDocs because deleted "
               "slots are physically removed (no tombstones remain)";

        const std::vector<std::string> expected_ids = {"doc-2", "doc-4", "doc-5"};
        for (int i = 0; i < 3; ++i) {
            EXPECT_FALSE(r.IsDeleted(i)) << "slot " << i << " unexpectedly deleted";
            auto doc = r.Document(i);
            ASSERT_NE(doc, nullptr) << "Document(" << i << ") returned null";
            const auto* f = doc->GetField("id");
            ASSERT_NE(f, nullptr) << "slot " << i << " missing 'id' field";
            EXPECT_EQ(f->Value(), expected_ids[i])
                << "slot " << i << " id mismatch — write order should be "
                << "preserved through delete+optimize";
        }
        r.Close();
    }
}

// =============================================================================
// Forensic Test 2: PhraseQuery slop — basic gating
// =============================================================================
//
// Scenario:
//   Doc body = "alpha beta gamma" (positions: alpha=0, beta=1, gamma=2).
//   Query: phrase ["alpha", "gamma"] (a hole of 1 between them).
//
// Oracle (Lucene 1.0.1 PhraseQuery semantics):
//   - PhraseScorer's exact mode (slop=0) requires positions to differ by
//     exactly the in-query offset (here 1). Actual offset is 2. So slop=0
//     MUST yield 0 hits.
//   - With slop >= 1, SloppyPhraseScorer allows up to `slop` extra position
//     gap. Actual extra gap is 1, so slop=1 MUST yield 1 hit.
//   - This is purely a gating invariant; we are not asserting any score.
//
// This catches:
//   - SetSlop being ignored entirely
//   - Sloppy mode falling back to exact match
//   - Exact mode accidentally accepting gapped matches
TEST(ForensicClaude, PhraseSlopGating) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        document::Document d;
        d.Add(document::Field::Text("body", "alpha beta gamma"));
        w.AddDocument(d);
        w.Close();
    }

    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    // slop = 0: must NOT match.
    {
        search::PhraseQuery pq;
        pq.Add(index::Term(0, "alpha"));
        pq.Add(index::Term(0, "gamma"));
        pq.SetSlop(0);
        auto hits = s.Search(pq);
        ASSERT_NE(hits, nullptr);
        EXPECT_EQ(hits->Length(), 0)
            << "exact phrase 'alpha gamma' must not match 'alpha beta gamma'";
    }

    // slop = 1: MUST match exactly once.
    {
        search::PhraseQuery pq;
        pq.Add(index::Term(0, "alpha"));
        pq.Add(index::Term(0, "gamma"));
        pq.SetSlop(1);
        auto hits = s.Search(pq);
        ASSERT_NE(hits, nullptr);
        EXPECT_EQ(hits->Length(), 1)
            << "with slop=1, 'alpha gamma' must match 'alpha beta gamma' "
               "(extra gap of 1 is within allowance)";
        if (hits->Length() == 1) {
            EXPECT_EQ(hits->Id(0), 0);
        }
    }
}

// =============================================================================
// Forensic Test 3: Sloppy phrase scores strictly less than exact phrase scores
// =============================================================================
//
// Scenario:
//   doc0 body = "alpha gamma"             (positions: alpha=0, gamma=1 → exact match)
//   doc1 body = "alpha beta gamma"        (positions: alpha=0, gamma=2 → 1 extra gap)
//   Query: phrase ["alpha", "gamma"], slop=1 (both docs match).
//
// Oracle (Lucene 1.0.1 SloppyPhraseScorer + default Similarity.sloppyFreq):
//   Per-match score contribution to freq is `1/(matchLength+1)`, where
//   matchLength is the number of extra positions beyond an exact match.
//     - doc0: matchLength = 0 → freq contribution = 1/(0+1) = 1.0
//     - doc1: matchLength = 1 → freq contribution = 1/(1+1) = 0.5
//
//   The score formula factors freq through Similarity.tf(freq) = sqrt(freq)
//   and a doc-norm. Both docs are searched with the same field-norm-encoding
//   pipeline, but doc1 has more tokens (3 vs 2), so its lengthNorm is
//   ALSO smaller. Both effects push doc1's score strictly below doc0's.
//
//   We assert the qualitative invariant ONLY:
//     score(doc0) > score(doc1) > 0
//
//   We do NOT pin exact numeric values here because the project's
//   Similarity.EncodeNorm uses a simplified linear encoding (see
//   include/minilucene/search/similarity.h:34) rather than Lucene 1.0.1's
//   SmallFloat 3+5 representation — pinning exact bytes would couple the
//   oracle to that implementation choice. The qualitative invariant is
//   sufficient to catch any mutation that breaks slop scoring monotonicity.
//
// This catches:
//   - Sloppy phrase scoring ignoring matchLength (giving both docs equal score)
//   - Sloppy phrase scoring reversing the relationship (boosting gappy matches)
//   - Loss of doc-norm during scoring
TEST(ForensicClaude, SloppyPhraseScoreDecreasesWithDistance) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        document::Document d0;
        d0.Add(document::Field::Text("body", "alpha gamma"));
        w.AddDocument(d0);
        document::Document d1;
        d1.Add(document::Field::Text("body", "alpha beta gamma"));
        w.AddDocument(d1);
        w.Close();
    }

    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    search::PhraseQuery pq;
    pq.Add(index::Term(0, "alpha"));
    pq.Add(index::Term(0, "gamma"));
    pq.SetSlop(1);
    auto hits = s.Search(pq);
    ASSERT_NE(hits, nullptr);
    ASSERT_EQ(hits->Length(), 2)
        << "with slop=1 both docs must match (exact + 1-gap)";

    // Pull scores by underlying doc id, not by ranked order — we want to verify
    // the ordering AND that the result list reflects it.
    float score_doc0 = -1.0f;
    float score_doc1 = -1.0f;
    for (int i = 0; i < hits->Length(); ++i) {
        if (hits->Id(i) == 0) score_doc0 = hits->Score(i);
        if (hits->Id(i) == 1) score_doc1 = hits->Score(i);
    }
    ASSERT_GT(score_doc0, 0.0f) << "doc0 (exact match) must have a positive score";
    ASSERT_GT(score_doc1, 0.0f) << "doc1 (1-gap match) must have a positive score";
    EXPECT_GT(score_doc0, score_doc1)
        << "exact match must score strictly higher than gapped match; "
        << "got doc0=" << score_doc0 << " vs doc1=" << score_doc1;

    // And the ranked list should reflect this ordering.
    EXPECT_EQ(hits->Id(0), 0) << "top hit must be doc0 (exact match)";
    EXPECT_EQ(hits->Id(1), 1) << "second hit must be doc1 (1-gap match)";
}

// =============================================================================
// Forensic Test 4: Optimize() must preserve PhraseQuery hits across segments
// =============================================================================
//
// Scenario:
//   Two separate writer sessions create two segments (mergeFactor large so no
//   auto-merge happens):
//     Segment A:
//       doc A0: id="A0", body="alpha beta gamma delta"     -> contains "beta gamma"
//       doc A1: id="A1", body="epsilon zeta beta gamma"    -> contains "beta gamma"
//     Segment B:
//       doc B0: id="B0", body="alpha mu nu omicron"        -> no match
//       doc B1: id="B1", body="theta beta gamma iota"      -> contains "beta gamma"
//
//   Phrase query: ["beta", "gamma"] with slop=0 (strict adjacency).
//
// Oracle (scenario invariant — independent of implementation):
//   Optimize() is a *physical* operation: it compacts segments and rebuilds
//   on-disk structures, but by definition it MUST NOT change query semantics.
//   Therefore:
//     - PRE-optimize hit count == POST-optimize hit count
//     - The matching docs, identified by their stored "id" field, must be the
//       SAME SET both times: {"A0", "A1", "B1"}
//     - The non-matching doc "B0" must remain absent in both cases
//
//   The "id" stored field gives stable identity across docID renumbering that
//   Optimize() may perform, so we compare *contents*, not slot numbers.
//
// This is the canonical test for REVIEW.md §2 Bug 1 (SegmentMerger writing
// `0` for every position into .prx). With that bug present, after Optimize()
// every term in the merged segment has position 0, so:
//   - exact PhraseScorer demands position(gamma) == position(beta)+1
//   - but both are 0 -> NO matches
//   - hit count crashes from 3 to 0
//
// This test catches:
//   - SegmentMerger zeroing / dropping position data (.prx corruption)
//   - SegmentMerger emitting wrong delta encoding for positions
//   - Phrase query going through a multi-segment reader returning a different
//     hit set than the same query through a single optimized segment
TEST(ForensicClaude, OptimizeThenPhrasePreservesHits) {
    store::RAMDirectory dir;

    // Build segment A: 2 docs.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.mergeFactor = 10000;  // never auto-merge
        w.AddDocument(MakeDoc("A0", "alpha beta gamma delta"));
        w.AddDocument(MakeDoc("A1", "epsilon zeta beta gamma"));
        w.Close();
    }
    // Build segment B: 2 more docs. Re-opening the writer with mergeFactor
    // large means this creates a second segment, not a merge.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.mergeFactor = 10000;
        w.AddDocument(MakeDoc("B0", "alpha mu nu omicron"));
        w.AddDocument(MakeDoc("B1", "theta beta gamma iota"));
        w.Close();
    }

    // Helper: run phrase("beta gamma") and return the set of stored "id"
    // values for the hits. Set, not list, because Optimize() can legally
    // renumber docIDs.
    auto query_ids = [&]() {
        index::SegmentsReader r(dir);
        search::IndexSearcher s(r);
        search::PhraseQuery pq;
        // MakeDoc adds id first (Field::Keyword) then body (Field::Text), so
        // FieldInfos assigns id=0, body=1. The phrase tokens live in body,
        // so we must query field 1. Using Term(0, ...) here would query the
        // id field, returning 0 hits for an unrelated reason.
        pq.Add(index::Term(1, "beta"));
        pq.Add(index::Term(1, "gamma"));
        pq.SetSlop(0);
        auto hits = s.Search(pq);
        std::vector<std::string> ids;
        if (hits) {
            for (int i = 0; i < hits->Length(); ++i) {
                auto doc = r.Document(hits->Id(i));
                if (doc) {
                    const auto* f = doc->GetField("id");
                    if (f) ids.push_back(f->Value());
                }
            }
        }
        r.Close();
        std::sort(ids.begin(), ids.end());
        return ids;
    };

    // PRE-optimize: must hit A0, A1, B1 across the two segments.
    const std::vector<std::string> expected = {"A0", "A1", "B1"};
    {
        auto ids = query_ids();
        ASSERT_EQ(ids.size(), 3u)
            << "pre-optimize: phrase 'beta gamma' must match exactly 3 docs "
               "across the 2 segments; got " << ids.size();
        EXPECT_EQ(ids, expected)
            << "pre-optimize: hit set must be {A0, A1, B1}";
    }

    // Optimize: physical compaction; query semantics MUST be preserved.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.Optimize();
        w.Close();
    }

    // POST-optimize: same hit set.
    {
        auto ids = query_ids();
        ASSERT_EQ(ids.size(), 3u)
            << "post-optimize: phrase 'beta gamma' must STILL match exactly 3 "
               "docs (Optimize must not change query semantics); got "
            << ids.size()
            << ". If 0, SegmentMerger is likely dropping .prx position data "
               "(REVIEW.md §2 Bug 1).";
        EXPECT_EQ(ids, expected)
            << "post-optimize: hit set must remain {A0, A1, B1}";
    }
}

}  // namespace minilucene
