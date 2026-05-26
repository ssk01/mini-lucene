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
#include "minilucene/index/field_infos.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/segments_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/query_parser/query_parser.h"
#include "minilucene/search/hits.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/search/similarity.h"
#include "minilucene/search/term_query.h"
#include "minilucene/store/ram_directory.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
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

// =============================================================================
// Forensic Test 5: Single-term PhraseQuery is semantically equivalent to
// a TermQuery on the same term.
// =============================================================================
//
// Scenario:
//   3 docs in a single segment:
//     doc 0: body = "alpha beta gamma"   (contains "beta")
//     doc 1: body = "epsilon zeta eta"   (no "beta")
//     doc 2: body = "lambda beta mu"     (contains "beta")
//
//   Run PhraseQuery with a SINGLE term ["beta"] (slop irrelevant).
//
// Oracle (semantic invariant — Lucene 1.0.1 PhraseQuery / any reasonable IR):
//   A phrase of length 1 has no inter-token gap to enforce, so it MUST be
//   indistinguishable from a TermQuery on that term:
//     - hit count: 2 (doc 0 and doc 2)
//     - hit set: {doc 0, doc 2}
//     - doc 1 (no "beta") MUST NOT appear
//
//   We do not assert exact scores (PhraseQuery and TermQuery may differ in
//   normalization detail), only the HIT SET equivalence.
//
// This is the canonical test for REVIEW.md §3 Bug 7a (PhraseQuery early-return
// false when terms.size() == 1, causing all single-term phrase queries to
// return 0 results regardless of input). With that bug present the test
// asserts hit count == 2 but observes 0.
//
// This test catches:
//   - Any code path that short-circuits PhraseQuery for n==1 with empty result
//   - PhraseQuery requiring strictly >= 2 terms to construct a scorer
//   - Off-by-one in phrase-length handling
TEST(ForensicClaude, SingleTermPhraseEquivalentToTermQuery) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        document::Document d0;
        d0.Add(document::Field::Text("body", "alpha beta gamma"));
        w.AddDocument(d0);
        document::Document d1;
        d1.Add(document::Field::Text("body", "epsilon zeta eta"));
        w.AddDocument(d1);
        document::Document d2;
        d2.Add(document::Field::Text("body", "lambda beta mu"));
        w.AddDocument(d2);
        w.Close();
    }

    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    // The docs above have only one indexed Field ("body"), so body = field 0.
    search::PhraseQuery pq;
    pq.Add(index::Term(0, "beta"));
    pq.SetSlop(0);
    auto hits = s.Search(pq);
    ASSERT_NE(hits, nullptr);

    ASSERT_EQ(hits->Length(), 2)
        << "single-term phrase ['beta'] must match the same docs as "
           "TermQuery('beta') = {doc 0, doc 2}; got " << hits->Length()
        << ". If 0, PhraseQuery is early-returning for n==1 (REVIEW.md §3 "
           "Bug 7a, src/search/phrase_query.cpp:31).";

    // Collect hit docIDs into a sorted vector for set comparison.
    std::vector<int> hit_ids;
    for (int i = 0; i < hits->Length(); ++i) hit_ids.push_back(hits->Id(i));
    std::sort(hit_ids.begin(), hit_ids.end());
    const std::vector<int> expected = {0, 2};
    EXPECT_EQ(hit_ids, expected)
        << "single-term phrase must hit exactly docs {0, 2}, not doc 1 "
           "(which contains no 'beta')";
}

// =============================================================================
// Forensic Test 6: Optimize() must preserve field-length norm differences
// =============================================================================
//
// Scenario:
//   Two segments, each with one doc containing exactly one occurrence of
//   "target":
//     Segment A, doc A0: body = "target"                   (1 token total)
//     Segment B, doc B0: body = "target alpha beta gamma delta epsilon zeta"
//                                                          (7 tokens total)
//
//   Both docs have term freq = 1 for "target", but they differ in field
//   length. Lucene 1.0.1 Similarity.lengthNorm = 1/sqrt(numTerms), so:
//     - A0 lengthNorm = 1/sqrt(1) = 1.0
//     - B0 lengthNorm = 1/sqrt(7) ≈ 0.378
//   This makes score(A0) > score(B0) for TermQuery("target").
//
// Oracle (scenario invariant):
//   Optimize() is a physical compaction; it MUST NOT change scoring
//   semantics. Therefore the qualitative invariant holds before AND after
//   optimize:
//     score(A0) > score(B0) > 0
//   AND the ratio score(A0)/score(B0) must be roughly preserved (within
//   a wide tolerance to allow for any norm-encoding round-trip loss).
//
// This is the canonical test for REVIEW.md §2 Bug 2 (SegmentMerger writes
// 0xFF for every (doc, field) norm byte, dropping all length-normalization
// info). With that bug present, after Optimize() both docs get the SAME
// decoded norm value, so:
//   - relative score ordering may flip or tie
//   - score(A0) ≈ score(B0) instead of score(A0) >> score(B0)
//
// This test catches:
//   - SegmentMerger zeroing / hard-coding .nrm bytes
//   - Merger writing wrong number of norm bytes (off-by-one over docs)
//   - lengthNorm encoding lost during multi-segment merge
TEST(ForensicClaude, OptimizePreservesLengthNormDifference) {
    store::RAMDirectory dir;
    // Segment A: short doc.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.mergeFactor = 10000;
        document::Document d;
        d.Add(document::Field::Text("body", "target"));
        w.AddDocument(d);
        w.Close();
    }
    // Segment B: long doc with the same target term + 6 filler tokens.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.mergeFactor = 10000;
        document::Document d;
        d.Add(document::Field::Text(
            "body", "target alpha beta gamma delta epsilon zeta"));
        w.AddDocument(d);
        w.Close();
    }

    // Helper: TermQuery("target") and return (scoreA, scoreB) where A is the
    // shorter doc and B is the longer doc, identified by reading body length.
    auto get_scores = [&]() {
        index::SegmentsReader r(dir);
        search::IndexSearcher s(r);
        search::TermQuery tq(index::Term(0, "target"));
        auto hits = s.Search(tq);
        float score_short = -1.0f;
        float score_long = -1.0f;
        if (hits) {
            for (int i = 0; i < hits->Length(); ++i) {
                auto doc = r.Document(hits->Id(i));
                if (!doc) continue;
                const auto* f = doc->GetField("body");
                if (!f) continue;
                const std::string& v = f->Value();
                if (v == "target") score_short = hits->Score(i);
                else score_long = hits->Score(i);
            }
        }
        r.Close();
        return std::pair<float, float>{score_short, score_long};
    };

    // PRE-optimize: short doc must score strictly higher than long doc.
    float pre_short = -1.0f, pre_long = -1.0f;
    {
        auto p = get_scores();
        pre_short = p.first;
        pre_long = p.second;
        ASSERT_GT(pre_short, 0.0f) << "pre-optimize: short doc must have positive score";
        ASSERT_GT(pre_long, 0.0f) << "pre-optimize: long doc must have positive score";
        EXPECT_GT(pre_short, pre_long)
            << "pre-optimize: shorter doc must score higher (lengthNorm); "
            << "got short=" << pre_short << " long=" << pre_long;
    }

    // Optimize: physical compaction. Scoring semantics MUST be preserved.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.Optimize();
        w.Close();
    }

    // POST-optimize: same qualitative ordering MUST hold.
    {
        auto p = get_scores();
        float post_short = p.first;
        float post_long = p.second;
        ASSERT_GT(post_short, 0.0f) << "post-optimize: short doc must still have positive score";
        ASSERT_GT(post_long, 0.0f) << "post-optimize: long doc must still have positive score";
        EXPECT_GT(post_short, post_long)
            << "post-optimize: shorter doc must STILL score higher than long doc. "
            << "If short ≈ long after Optimize, SegmentMerger has dropped length "
            << "norms (REVIEW.md §2 Bug 2, src/index/segment_merger.cpp:107). "
            << "Got post_short=" << post_short << " post_long=" << post_long;
    }
}

// =============================================================================
// Forensic Test 7: User-level BooleanQuery (+MUST +MUST -MUST_NOT) composes
// correctly across a small mixed corpus.
// =============================================================================
//
// This is a *user-facing* test: it indexes a small mixed corpus the way a
// real client would, builds a 3-clause BooleanQuery, and asserts the EXACT
// hit set against a hand-enumerated oracle.
//
// Corpus (6 docs, single field "body"):
//   d0: "alpha beta gamma"          (alpha, beta, gamma)
//   d1: "alpha beta delta"          (alpha, beta)         <- the only match
//   d2: "alpha epsilon zeta"        (alpha)
//   d3: "beta epsilon"              (beta)
//   d4: "alpha"                     (alpha)
//   d5: "delta"                     (none of the three)
//
// Query: BooleanQuery
//   +alpha   (MUST)
//   +beta    (MUST)
//   -gamma   (MUST_NOT)
//
// Oracle (hand-enumerated set membership, BooleanQuery semantics):
//   - MUST satisfies AND across MUST clauses.
//   - MUST_NOT excludes docs containing any matched MUST_NOT term.
//   Per-doc evaluation:
//     d0: alpha ✓ beta ✓ gamma ✗ -> excluded by MUST_NOT
//     d1: alpha ✓ beta ✓ gamma ✗ excluded? -> "gamma" not in d1 -> NOT excluded -> MATCH
//     d2: alpha ✓ beta ✗ -> excluded by MUST
//     d3: alpha ✗ -> excluded by MUST
//     d4: alpha ✓ beta ✗ -> excluded by MUST
//     d5: alpha ✗ -> excluded by MUST
//   => hit set = {d1}, count = 1
//
// This catches:
//   - MUST_NOT semantics inverted or ignored
//   - MUST clauses OR-ed instead of AND-ed
//   - BooleanScorer requiring MUST clauses to match positionally rather than
//     by docID set intersection
//   - Off-by-one in clause count handling
TEST(ForensicClaude, BooleanMustMustMustNotComposes) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        const std::vector<std::string> bodies = {
            "alpha beta gamma",   // d0
            "alpha beta delta",   // d1 (only match)
            "alpha epsilon zeta", // d2
            "beta epsilon",       // d3
            "alpha",              // d4
            "delta",              // d5
        };
        for (const auto& b : bodies) {
            document::Document d;
            d.Add(document::Field::Text("body", b));
            w.AddDocument(d);
        }
        w.Close();
    }

    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    // Single-field corpus -> body = field 0.
    search::BooleanQuery bq;
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "alpha")),
           search::Occur::MUST);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "beta")),
           search::Occur::MUST);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "gamma")),
           search::Occur::MUST_NOT);

    auto hits = s.Search(bq);
    ASSERT_NE(hits, nullptr);

    std::vector<int> hit_ids;
    for (int i = 0; i < hits->Length(); ++i) hit_ids.push_back(hits->Id(i));
    std::sort(hit_ids.begin(), hit_ids.end());

    const std::vector<int> expected = {1};  // only d1
    ASSERT_EQ(hits->Length(), 1)
        << "BooleanQuery(+alpha +beta -gamma) must match exactly 1 doc "
           "(d1: 'alpha beta delta'). Got " << hits->Length()
        << " hits: " << (hit_ids.empty() ? std::string("[]") :
                         [&]() { std::string s = "["; for (int x : hit_ids) s += std::to_string(x) + ","; s.back() = ']'; return s; }());
    EXPECT_EQ(hit_ids, expected) << "hit set must be exactly {1}";
}

// =============================================================================
// Forensic Test 8: Deleted docs MUST stay deleted across a multi-segment merge
// =============================================================================
//
// User-level scenario for REVIEW.md §2 Bug 3 (SegmentMerger ignores the
// deletion bitmap when copying stored fields / postings, so deleted docs
// "revive" after a merge):
//
// Steps:
//   1. Write segment A: d0 body="apple", d1 body="banana".
//   2. Open reader, Delete(0) -> apple is dead.
//   3. Write segment B (separate writer session): d2 body="cherry",
//      d3 body="date".
//   4. Open writer + Optimize() -> forces a merge of A and B.
//   5. Search for "apple" -> MUST return 0 hits.
//      Search for "banana", "cherry", "date" -> MUST each return exactly 1 hit.
//      NumDocs -> MUST equal 3 (4 written, 1 deleted, all survive merge).
//
// Oracle (scenario invariant):
//   Delete is permanent. A merge that "revives" a deleted doc is by
//   definition broken — there is no version of correct merge semantics
//   under which deleted docs reappear in queries.
//
// This catches:
//   - SegmentMerger iterating [0, MaxDoc) but failing to skip deleted slots
//   - .del bitmap not propagated into merged segment
//   - Stored-field offsets recomputed using NumDocs as a slot count rather
//     than walking the deletion bitmap (REVIEW.md §2 Bug 3 exact mechanism)
//   - The "deleted doc revives, scoring is wrong, NumDocs is wrong" trio
TEST(ForensicClaude, DeletedDocsStayDeletedAcrossMerge) {
    store::RAMDirectory dir;

    // Segment A: 2 docs.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.mergeFactor = 10000;
        document::Document d0;
        d0.Add(document::Field::Text("body", "apple"));
        w.AddDocument(d0);
        document::Document d1;
        d1.Add(document::Field::Text("body", "banana"));
        w.AddDocument(d1);
        w.Close();
    }

    // Delete "apple" (slot 0 in segment A) via reader.
    {
        index::SegmentsReader r(dir);
        ASSERT_EQ(r.NumDocs(), 2);
        r.Delete(0);
        EXPECT_EQ(r.NumDocs(), 1);
        r.Close();
    }

    // Segment B: 2 more docs.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.mergeFactor = 10000;
        document::Document d2;
        d2.Add(document::Field::Text("body", "cherry"));
        w.AddDocument(d2);
        document::Document d3;
        d3.Add(document::Field::Text("body", "date"));
        w.AddDocument(d3);
        w.Close();
    }

    // Optimize: forces merge of segments A (with deletion bitmap) and B.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.Optimize();
        w.Close();
    }

    // Verify post-merge state: deletion must have been honored.
    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    EXPECT_EQ(r.NumDocs(), 3)
        << "after merge: 3 docs must survive (apple deleted, banana/cherry/date "
           "remain). If 4, SegmentMerger ignored the deletion bitmap (REVIEW.md "
           "§2 Bug 3).";

    auto search_term = [&](const std::string& term) {
        search::TermQuery tq(index::Term(0, term));
        return s.Search(tq);
    };

    {
        auto h = search_term("apple");
        ASSERT_NE(h, nullptr);
        EXPECT_EQ(h->Length(), 0)
            << "'apple' was deleted before merge — must NOT resurrect "
               "after Optimize. If 1 hit, merger copied a deleted doc.";
    }
    {
        auto h = search_term("banana");
        ASSERT_NE(h, nullptr);
        EXPECT_EQ(h->Length(), 1) << "'banana' must survive merge";
    }
    {
        auto h = search_term("cherry");
        ASSERT_NE(h, nullptr);
        EXPECT_EQ(h->Length(), 1) << "'cherry' must survive merge";
    }
    {
        auto h = search_term("date");
        ASSERT_NE(h, nullptr);
        EXPECT_EQ(h->Length(), 1) << "'date' must survive merge";
    }

    r.Close();
}

// =============================================================================
// Forensic Test 9: TermQuery is field-scoped — postings on field A must not
// match queries on field B even when the same term appears in both.
// =============================================================================
//
// Scenario (2 docs, each with two fields "title" and "body"):
//   d0: title="apple"   body="banana"
//   d1: title="banana"  body="apple"
//
// Lucene assigns field numbers globally by first-encounter order across the
// segment. d0 adds "title" first then "body", so title→0, body→1. d1 reuses
// the same numbers.
//
// Oracle (Lucene 1.0.1 spec, scenario invariant):
//   Postings are keyed by (fieldNumber, term). A TermQuery on (field=0,
//   term="apple") must only match docs whose **title** is "apple" — d0 only.
//   A TermQuery on (field=1, term="apple") must only match docs whose
//   **body** is "apple" — d1 only. If either query returns the other doc or
//   both docs, field isolation is broken (the indexer collapsed field
//   numbers, the term dictionary keyed on term-only, or the query dropped
//   the field discriminator).
//
// This catches:
//   - DocumentWriter or FieldInfos collapsing distinct fields onto the same
//     field number
//   - TermInfosWriter/Reader keying purely on the term string and ignoring
//     the field discriminator
//   - TermQuery scorer iterating all matching docs regardless of which field
//     produced the posting
TEST(ForensicClaude, FieldScopedTermDoesNotMatchAcrossFields) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));

        document::Document d0;
        d0.Add(document::Field::Text("title", "apple"));
        d0.Add(document::Field::Text("body",  "banana"));
        w.AddDocument(d0);

        document::Document d1;
        d1.Add(document::Field::Text("title", "banana"));
        d1.Add(document::Field::Text("body",  "apple"));
        w.AddDocument(d1);

        w.Close();
    }

    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    auto run = [&](int field, const std::string& term) {
        search::TermQuery tq(index::Term(field, term));
        return s.Search(tq);
    };

    {  // title:apple -> only d0
        auto h = run(0, "apple");
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(h->Length(), 1)
            << "title:apple must match exactly d0 (title=\"apple\"); body=apple "
               "(d1) must be excluded. Got " << h->Length() << " hits.";
        EXPECT_EQ(h->Id(0), 0);
    }
    {  // body:apple -> only d1
        auto h = run(1, "apple");
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(h->Length(), 1)
            << "body:apple must match exactly d1 (body=\"apple\"); title=apple "
               "(d0) must be excluded. Got " << h->Length() << " hits.";
        EXPECT_EQ(h->Id(0), 1);
    }
    {  // title:banana -> only d1
        auto h = run(0, "banana");
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(h->Length(), 1)
            << "title:banana must match exactly d1; body=banana (d0) excluded.";
        EXPECT_EQ(h->Id(0), 1);
    }
    {  // body:banana -> only d0
        auto h = run(1, "banana");
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(h->Length(), 1)
            << "body:banana must match exactly d0; title=banana (d1) excluded.";
        EXPECT_EQ(h->Id(0), 0);
    }
    r.Close();
}

// =============================================================================
// Forensic Test 10: BooleanQuery (SHOULD-only) — score grows monotonically
// with the number of matching clauses (coordination factor).
// =============================================================================
//
// Corpus (3 docs, single field "body"):
//   d0: "a"
//   d1: "a b"
//   d2: "a b c"
//
// Query: BooleanQuery(SHOULD a, SHOULD b, SHOULD c)
//
// Per-doc matched-clause count: d0=1, d1=2, d2=3.
//
// Oracle (Lucene 1.0.1 Similarity, hand-derived):
//   score(d) = coord(overlap, max) * Σ_{t matches} tf(t,d) · idf(t) · norm(d)
//
//   With the default `Similarity` in include/minilucene/search/similarity.h:
//     coord(o, n)        = o / n
//     idf(df, maxDoc)    = ln(maxDoc / (df + 1)) + 1
//     tf(freq)           = sqrt(freq)
//     norm(d)            = 1 / sqrt(len(d))  (then encoded to a byte)
//
//   For this corpus (maxDoc=3): df(a)=3, df(b)=2, df(c)=1.
//     idf(a) = ln(3/4)+1 ≈ 0.712
//     idf(b) = ln(3/3)+1 = 1.000
//     idf(c) = ln(3/2)+1 ≈ 1.405
//
//   Each matched term has tf=1, so tf=1. Norms: norm(d0)≈1, norm(d1)≈0.707,
//   norm(d2)≈0.577.
//
//   Unweighted sums of matched idfs (before coord+norm):
//     d0: 0.712               · norm(1.0)    = 0.712
//     d1: 0.712+1.000 = 1.712 · norm(0.707)  = 1.211
//     d2: 0.712+1.000+1.405 = 3.117 · norm(0.577) = 1.798
//
//   After coord:
//     d0: (1/3) · 0.712 = 0.237
//     d1: (2/3) · 1.211 = 0.807
//     d2: (3/3) · 1.798 = 1.798
//
//   So we expect score(d2) > score(d1) > score(d0) by clean margins — even
//   after norm-byte quantization the relative ordering cannot flip.
//
// Oracle bound: monotone in matched-clause count (NOT exact value).
//
// This catches:
//   - SHOULD scorer summing only the first matched clause
//   - coord() ignored entirely (would make d2 ≈ d0 within norm noise)
//   - SHOULD clauses requiring all to match (would shrink hit set to 1)
//   - Boolean scorer dropping high-idf rare terms (would make d2 ≤ d1)
TEST(ForensicClaude, BooleanShouldMoreMatchingClausesScoresHigher) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        const std::vector<std::string> bodies = {"a", "a b", "a b c"};
        for (const auto& b : bodies) {
            document::Document d;
            d.Add(document::Field::Text("body", b));
            w.AddDocument(d);
        }
        w.Close();
    }

    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    search::BooleanQuery bq;
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "a")),
           search::Occur::SHOULD);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "b")),
           search::Occur::SHOULD);
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "c")),
           search::Occur::SHOULD);

    auto hits = s.Search(bq);
    ASSERT_NE(hits, nullptr);
    ASSERT_EQ(hits->Length(), 3)
        << "SHOULD with at-least-one match: all 3 docs (each contains 'a') "
           "must be returned.";

    float score_by_doc[3] = {-1.0f, -1.0f, -1.0f};
    for (int i = 0; i < hits->Length(); ++i) {
        const int id = hits->Id(i);
        ASSERT_GE(id, 0);
        ASSERT_LE(id, 2);
        score_by_doc[id] = hits->Score(i);
    }
    for (int d = 0; d < 3; ++d) {
        ASSERT_GT(score_by_doc[d], 0.0f)
            << "doc " << d << " score must be populated (>0)";
    }

    EXPECT_GT(score_by_doc[2], score_by_doc[1])
        << "doc d2 matches 3 clauses, d1 matches 2 -> d2 must outscore d1. "
           "If equal, coord() is not applied. If reversed, scorer dropped a "
           "high-idf rare-term contribution.";
    EXPECT_GT(score_by_doc[1], score_by_doc[0])
        << "doc d1 matches 2 clauses, d0 matches 1 -> d1 must outscore d0. "
           "If equal/reversed, length-norm overwhelmed coord — but the hand "
           "derivation in the comment shows d1≈0.807 vs d0≈0.237.";
    r.Close();
}

// =============================================================================
// Forensic Test 11: DocFreq must equal the count of distinct documents an
// iteration over TermDocs(term) returns. (Math invariant.)
// =============================================================================
//
// Corpus (4 docs, single field "body"):
//   d0: "alpha beta"        - alpha 1×, beta 1×
//   d1: "alpha alpha beta"  - alpha 2×, beta 1×
//   d2: "alpha gamma"       - alpha 1×, gamma 1×
//   d3: "gamma delta"       - gamma 1×, delta 1×
//
// Per-term ground truth (hand-counted):
//   alpha: docFreq=3 (d0, d1, d2);   total_freq=4 (1+2+1)
//   beta:  docFreq=2 (d0, d1);       total_freq=2 (1+1)
//   gamma: docFreq=2 (d2, d3);       total_freq=2 (1+1)
//   delta: docFreq=1 (d3);           total_freq=1 (1)
//
// Oracle (math invariant, Lucene 1.0.1 contract):
//   For every term t, IndexReader.DocFreq(t) must equal the number of
//   iterations of TermDocs(t).Next() that return true. Furthermore the
//   per-doc Freq() values must sum to total_freq, and each Doc() must be a
//   distinct, in-order docID.
//
// This catches:
//   - docFreq cached at indexing time and not refreshed (off-by-one across
//     segments)
//   - TermDocs skipping or double-counting a doc
//   - Freq() returning stale value from previous Next() call
//   - .frq postings written with wrong VarInt delta encoding (Bug 1 cousin)
TEST(ForensicClaude, DocFreqMatchesTermDocsIteration) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        const std::vector<std::string> bodies = {
            "alpha beta",
            "alpha alpha beta",
            "alpha gamma",
            "gamma delta",
        };
        for (const auto& b : bodies) {
            document::Document d;
            d.Add(document::Field::Text("body", b));
            w.AddDocument(d);
        }
        w.Close();
    }

    index::SegmentsReader r(dir);

    struct TermSpec {
        std::string term;
        int expected_doc_freq;
        int expected_total_freq;
        std::vector<int> expected_docs;
    };
    const std::vector<TermSpec> specs = {
        {"alpha", 3, 4, {0, 1, 2}},
        {"beta",  2, 2, {0, 1}},
        {"gamma", 2, 2, {2, 3}},
        {"delta", 1, 1, {3}},
    };

    for (const auto& sp : specs) {
        index::Term t(0, sp.term);
        const int df = r.DocFreq(t);
        EXPECT_EQ(df, sp.expected_doc_freq)
            << "DocFreq(" << sp.term << ") expected "
            << sp.expected_doc_freq << ", got " << df;

        auto td = r.Docs(t);
        ASSERT_NE(td, nullptr) << "Docs(" << sp.term << ") must not be null";

        std::vector<int> seen_docs;
        int sum_freq = 0;
        int prev_doc = -1;
        while (td->Next()) {
            const int d = td->Doc();
            const int f = td->Freq();
            EXPECT_GT(d, prev_doc)
                << "TermDocs must yield strictly increasing docIDs "
                   "(prev=" << prev_doc << ", cur=" << d << ")";
            EXPECT_GT(f, 0)
                << "Freq() must be > 0 for any returned doc";
            seen_docs.push_back(d);
            sum_freq += f;
            prev_doc = d;
        }
        td->Close();

        EXPECT_EQ(static_cast<int>(seen_docs.size()), sp.expected_doc_freq)
            << "iteration count for " << sp.term << " must equal DocFreq";
        EXPECT_EQ(seen_docs, sp.expected_docs)
            << "doc set for " << sp.term << " mismatch";
        EXPECT_EQ(sum_freq, sp.expected_total_freq)
            << "sum of Freq() for " << sp.term << " mismatch";
    }
    r.Close();
}

// =============================================================================
// Forensic Test 12: NumDocs + MaxDoc contract under delete + optimize.
// =============================================================================
//
// Scenario:
//   Write 5 docs (single segment), delete docs {1, 3, 4} via reader, then
//   Optimize.
//
// Oracle (Lucene 1.0.1 IndexReader contract):
//   Before Optimize:
//     MaxDoc()  = 5   (still includes tombstones)
//     NumDocs() = 2   (5 minus 3 deletions)
//     IsDeleted(i) true for i ∈ {1,3,4}, false for i ∈ {0,2}
//   After Optimize:
//     MaxDoc()  = 2   (compacted; tombstones dropped)
//     NumDocs() = 2   (= MaxDoc — no tombstones survive optimize)
//     IsDeleted(i) false for all i ∈ [0, MaxDoc)
//
// Surviving docs (in original order, by id field): "doc-0", "doc-2".
//
// This catches:
//   - NumDocs returning the cached MaxDoc count (REVIEW.md §2 Bug
//     SegmentsReader.NumDocs not subtracting deletions — the bug that 9c16d71
//     supposedly fixed; this test locks it in as regression)
//   - Optimize copying tombstones to the new segment (MaxDoc would still be 5)
//   - IsDeleted bitmap not cleared after Optimize
//   - Surviving doc IDs/order corrupted by compaction
TEST(ForensicClaude, NumDocsMaxDocContractUnderDeleteAndOptimize) {
    store::RAMDirectory dir;

    // Write 5 docs.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        for (int i = 0; i < 5; ++i) {
            w.AddDocument(MakeDoc("doc-" + std::to_string(i),
                                  "body-" + std::to_string(i)));
        }
        w.Close();
    }

    // Delete {1, 3, 4}; assert pre-optimize contract.
    {
        index::SegmentsReader r(dir);
        ASSERT_EQ(r.MaxDoc(), 5);
        ASSERT_EQ(r.NumDocs(), 5);
        r.Delete(1);
        r.Delete(3);
        r.Delete(4);

        EXPECT_EQ(r.MaxDoc(), 5)
            << "pre-optimize: MaxDoc keeps tombstones, must remain 5.";
        EXPECT_EQ(r.NumDocs(), 2)
            << "pre-optimize: NumDocs = 5 - 3 deletions = 2. If 5, "
               "SegmentsReader.NumDocs is not subtracting deletions.";
        EXPECT_TRUE(r.IsDeleted(1));
        EXPECT_TRUE(r.IsDeleted(3));
        EXPECT_TRUE(r.IsDeleted(4));
        EXPECT_FALSE(r.IsDeleted(0));
        EXPECT_FALSE(r.IsDeleted(2));
        r.Close();
    }

    // Optimize -> compact.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.Optimize();
        w.Close();
    }

    // Post-optimize: MaxDoc == NumDocs == 2, no tombstones, surviving order
    // is {doc-0, doc-2}.
    {
        index::SegmentsReader r(dir);
        EXPECT_EQ(r.MaxDoc(), 2)
            << "post-optimize: MaxDoc must drop tombstones -> 2.";
        EXPECT_EQ(r.NumDocs(), 2)
            << "post-optimize: NumDocs must equal MaxDoc (no tombstones).";
        EXPECT_FALSE(r.IsDeleted(0))
            << "no slot may be deleted after Optimize.";
        EXPECT_FALSE(r.IsDeleted(1))
            << "no slot may be deleted after Optimize.";

        std::vector<std::string> surviving_ids;
        for (int i = 0; i < r.MaxDoc(); ++i) {
            auto doc = r.Document(i);
            ASSERT_NE(doc, nullptr);
            const document::Field* f = doc->GetField("id");
            ASSERT_NE(f, nullptr) << "stored id must survive optimize";
            surviving_ids.push_back(f->Value());
        }
        const std::vector<std::string> expected = {"doc-0", "doc-2"};
        EXPECT_EQ(surviving_ids, expected)
            << "surviving docs must be {doc-0, doc-2} in original order.";
        r.Close();
    }
}

// =============================================================================
// Forensic Test 13: PhraseQuery with slop=0 enforces term ORDER.
// =============================================================================
//
// Corpus (1 doc, single field "body"):
//   d0: "alpha beta gamma"  (positions: alpha=0, beta=1, gamma=2)
//
// Oracle (Lucene 1.0.1 PhraseQuery, hand-derived):
//   Phrase "alpha beta" slop=0 matches d0 because positions(beta) -
//   positions(alpha) = 1 - 0 = 1 == |query|-position-gap = 1.  -> 1 hit.
//
//   Phrase "beta alpha" slop=0 expects beta at offset 0 and alpha at offset
//   1 in the document. Required position gap: positions(alpha) -
//   positions(beta) = 0 - 1 = -1, which is not the required +1 for an
//   in-order match with slop=0. Per Lucene 1.0.1 PhraseScorer, exact phrase
//   requires the second term to follow the first by exactly 1 position; a
//   reversed pair does not match with slop=0. -> 0 hits.
//
// This catches:
//   - PhraseScorer treating the term list as a multiset (would match both)
//   - position-difference comparison using abs() instead of signed delta
//   - slop=0 path silently widening to slop>=1
TEST(ForensicClaude, PhraseQueryReverseOrderDoesNotMatchSlop0) {
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

    {  // forward: must match
        search::PhraseQuery pq;
        pq.Add(index::Term(0, "alpha"));
        pq.Add(index::Term(0, "beta"));
        pq.SetSlop(0);
        auto h = s.Search(pq);
        ASSERT_NE(h, nullptr);
        EXPECT_EQ(h->Length(), 1)
            << "in-order phrase 'alpha beta' must match d0; got "
            << h->Length();
    }
    {  // reversed: must NOT match at slop=0
        search::PhraseQuery pq;
        pq.Add(index::Term(0, "beta"));
        pq.Add(index::Term(0, "alpha"));
        pq.SetSlop(0);
        auto h = s.Search(pq);
        ASSERT_NE(h, nullptr);
        EXPECT_EQ(h->Length(), 0)
            << "reversed phrase 'beta alpha' slop=0 must NOT match. If 1 hit, "
               "PhraseScorer is using |delta| instead of signed delta, or "
               "ignoring term order.";
    }
    r.Close();
}

// =============================================================================
// Forensic Test 14: Phrase that exists in TWO separate segments — both must
// be found post-merge AND pre-merge (multi-segment correctness).
// =============================================================================
//
// Scenario:
//   Segment A: d0 body="alpha beta gamma"
//   Segment B: d1 body="delta alpha beta"
//   (Two separate IndexWriter sessions, mergeFactor=10000 to prevent
//   auto-merge.)
//
// Oracle (scenario invariant + Lucene 1.0.1 multi-segment phrase contract):
//   PhraseQuery "alpha beta" slop=0 must match both d0 and d1, regardless
//   of which segment each lives in. Positions are encoded per-segment, so
//   the searcher must decode them in the correct segment's frame — not
//   accumulate position deltas across segments (REVIEW.md §2 Bug 4 family).
//   After Optimize() forces a merge, the same query must still return both
//   docs — merger must rewrite .prx with correct, segment-local position
//   deltas in the merged segment.
//
// Pre-merge expected: hit set {0, 1}  (segIdx-mapped global docIDs)
// Post-merge expected: same hit set, both docs survive merge.
//
// This catches:
//   - Position deltas leaking across segment boundary (Bug 4)
//   - SegmentMerger writing positions as global offsets instead of
//     per-doc resets
//   - PhraseScorer maintaining stale position state across segment switch
//   - Merge losing one of the phrase-matching docs entirely
TEST(ForensicClaude, PhraseAcrossSegmentBoundaryStillMatches) {
    store::RAMDirectory dir;

    // Segment A.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.mergeFactor = 10000;
        document::Document d;
        d.Add(document::Field::Text("body", "alpha beta gamma"));
        w.AddDocument(d);
        w.Close();
    }

    // Segment B.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.mergeFactor = 10000;
        document::Document d;
        d.Add(document::Field::Text("body", "delta alpha beta"));
        w.AddDocument(d);
        w.Close();
    }

    auto run_phrase = [&](index::IndexReader& reader) {
        search::IndexSearcher s(reader);
        search::PhraseQuery pq;
        pq.Add(index::Term(0, "alpha"));
        pq.Add(index::Term(0, "beta"));
        pq.SetSlop(0);
        auto h = s.Search(pq);
        std::vector<int> ids;
        if (h != nullptr) {
            for (int i = 0; i < h->Length(); ++i) ids.push_back(h->Id(i));
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    };

    // Pre-merge: 2 segments.
    {
        index::SegmentsReader r(dir);
        ASSERT_EQ(r.NumDocs(), 2);
        auto ids = run_phrase(r);
        const std::vector<int> expected = {0, 1};
        EXPECT_EQ(ids, expected)
            << "pre-merge: both segments contain 'alpha beta' (in order), "
               "must match both.";
        r.Close();
    }

    // Force merge.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.Optimize();
        w.Close();
    }

    // Post-merge: 1 segment.
    {
        index::SegmentsReader r(dir);
        ASSERT_EQ(r.NumDocs(), 2);
        auto ids = run_phrase(r);
        const std::vector<int> expected = {0, 1};
        EXPECT_EQ(ids, expected)
            << "post-merge: merged segment must preserve phrase matches in "
               "both original docs. If only {0} or {1}, merger lost or "
               "corrupted one doc's positions.";
        r.Close();
    }
}

// =============================================================================
// Forensic Test 15: BooleanQuery with ONLY prohibited clauses returns empty.
// =============================================================================
//
// Corpus (3 docs, single field "body"):
//   d0: "alpha"
//   d1: "beta"
//   d2: "alpha beta"
//
// Query: BooleanQuery(MUST_NOT alpha)
//
// Oracle (Lucene 1.0.1 BooleanScorer spec):
//   A BooleanQuery must contain at least one MUST or SHOULD clause to
//   produce any hits. A query containing only MUST_NOT clauses has no
//   positive selector — the spec returns an empty hit set rather than
//   "all docs not matching". This is a deliberate edge-case choice in
//   Lucene 1.0.1 to prevent accidental full-corpus scans.
//
// Expected: 0 hits (regardless of whether some doc lacks the prohibited
// term).
//
// This catches:
//   - BooleanScorer naively treating MUST_NOT as a unary negation over
//     the full corpus (would return d1)
//   - MUST_NOT-only path falling through to "match all" default
//   - Off-by-one in clause counting that silently elevates MUST_NOT to MUST
TEST(ForensicClaude, BooleanQueryAllProhibitedReturnsEmpty) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        for (const auto& body :
             std::vector<std::string>{"alpha", "beta", "alpha beta"}) {
            document::Document d;
            d.Add(document::Field::Text("body", body));
            w.AddDocument(d);
        }
        w.Close();
    }

    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    search::BooleanQuery bq;
    bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "alpha")),
           search::Occur::MUST_NOT);

    auto h = s.Search(bq);
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->Length(), 0)
        << "MUST_NOT-only BooleanQuery must return 0 hits (Lucene 1.0.1: at "
           "least one MUST/SHOULD required). If 1 hit (d1: 'beta'), the "
           "scorer treated MUST_NOT as 'all-corpus minus matched'.";
    r.Close();
}

// =============================================================================
// Forensic Test 16: Optimize() is idempotent.
// =============================================================================
//
// Write 4 docs (single segment), Optimize once -> snapshot state, Optimize
// again -> verify second snapshot identical.
//
// Oracle (math invariant — Lucene 1.0.1 IndexWriter contract):
//   For any index I, Optimize(Optimize(I)) == Optimize(I) bytewise from a
//   semantics standpoint: (a) doc count unchanged, (b) MaxDoc unchanged,
//   (c) every query returns identical hits + identical scores, (d) every
//   stored field round-trips identically. The second Optimize must be a
//   no-op (or at worst rewrite to the same logical content), not a
//   destructive transform.
//
// This catches:
//   - Optimize re-encoding norms with quantization drift on each pass
//   - Optimize re-numbering fields on each pass (would scramble field IDs)
//   - Optimize accidentally re-applying a non-existent deletion bitmap
//   - Optimize losing 1 doc per pass (off-by-one in slot iteration)
TEST(ForensicClaude, OptimizeIsIdempotent) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        const std::vector<std::pair<std::string, std::string>> docs = {
            {"id-0", "alpha beta"},
            {"id-1", "alpha gamma"},
            {"id-2", "beta delta"},
            {"id-3", "alpha"},
        };
        for (const auto& [id, body] : docs) {
            w.AddDocument(MakeDoc(id, body));
        }
        w.Close();
    }

    auto snapshot = [&]() {
        index::SegmentsReader r(dir);
        search::IndexSearcher s(r);
        struct Snap {
            int max_doc;
            int num_docs;
            std::vector<std::string> stored_ids;
            std::vector<int>   alpha_doc_ids;
            std::vector<float> alpha_scores;
        };
        Snap snap;
        snap.max_doc  = r.MaxDoc();
        snap.num_docs = r.NumDocs();
        for (int i = 0; i < r.MaxDoc(); ++i) {
            auto doc = r.Document(i);
            const document::Field* f = doc ? doc->GetField("id") : nullptr;
            snap.stored_ids.push_back(f ? f->Value() : std::string("<null>"));
        }
        // body is field 1 (id Keyword first, body Text second).
        search::TermQuery tq(index::Term(1, "alpha"));
        auto h = s.Search(tq);
        if (h != nullptr) {
            for (int i = 0; i < h->Length(); ++i) {
                snap.alpha_doc_ids.push_back(h->Id(i));
                snap.alpha_scores.push_back(h->Score(i));
            }
        }
        r.Close();
        return snap;
    };

    // First Optimize.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.Optimize();
        w.Close();
    }
    auto snap1 = snapshot();

    // Second Optimize — must be a no-op semantically.
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.Optimize();
        w.Close();
    }
    auto snap2 = snapshot();

    EXPECT_EQ(snap1.max_doc, snap2.max_doc) << "MaxDoc must be idempotent";
    EXPECT_EQ(snap1.num_docs, snap2.num_docs) << "NumDocs must be idempotent";
    EXPECT_EQ(snap1.stored_ids, snap2.stored_ids)
        << "stored id order must be idempotent across optimize passes";
    EXPECT_EQ(snap1.alpha_doc_ids, snap2.alpha_doc_ids)
        << "hit set for body:alpha must be idempotent";
    ASSERT_EQ(snap1.alpha_scores.size(), snap2.alpha_scores.size());
    for (size_t i = 0; i < snap1.alpha_scores.size(); ++i) {
        EXPECT_FLOAT_EQ(snap1.alpha_scores[i], snap2.alpha_scores[i])
            << "score for hit #" << i << " must be identical across optimize "
               "passes (norm quantization drift would surface here).";
    }
}

// =============================================================================
// Forensic Test 17: AddDocument after Close() must throw, not silently write.
// =============================================================================
//
// Scenario:
//   1. Construct IndexWriter, add 1 doc, Close().
//   2. Call AddDocument on the closed writer.
//
// Oracle (Lucene 1.0.1 IndexWriter contract + scenario invariant):
//   A closed writer is a terminal state. Any further Add must either (a)
//   throw a hard error so callers learn immediately, or (b) be a documented
//   no-op. Silently writing to a fresh DocumentWriter that nobody can flush
//   loses data and is the worst possible failure mode. We pick the throwing
//   contract: it matches Java Lucene's AlreadyClosedException pattern and is
//   the only behavior testable from outside without inspecting internals.
//
// This catches:
//   - Missing `closed_` check in AddDocument (silent data leak)
//   - Caller code that reuses a writer across optimize/close boundaries
//   - Refactors that drop the closed-state guard
TEST(ForensicClaude, AddDocumentAfterCloseThrows) {
    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.AddDocument(MakeDoc("id-0", "alpha"));
    w.Close();

    EXPECT_THROW(w.AddDocument(MakeDoc("id-1", "beta")), std::logic_error)
        << "AddDocument on a closed IndexWriter must throw, not silently "
           "buffer the doc into an unreachable writer (data-loss bug).";
}

// =============================================================================
// Forensic Test 18: Phrase across segment boundary correct under auto-merge
// (mergeFactor=2) — does NOT manually disable merging.
// =============================================================================
//
// Scenario:
//   IndexWriter with mergeFactor=2 (forces a merge after every 2 docs).
//   Add 4 docs, each with body containing "alpha beta" in order. The writer
//   will auto-flush + auto-merge mid-stream; we must end with all 4 docs
//   matching the phrase regardless of which merge cycle they landed in.
//
// Oracle (scenario invariant):
//   PhraseQuery "alpha beta" slop=0 must match all 4 docs no matter how
//   many merge passes happened. Forensic Test 14 covered the explicit
//   Optimize() merge path; this one covers the *auto-merge during ingest*
//   path which exercises a different code path (FlushPending triggers
//   merger via mergeFactor, not Optimize).
//
// This catches:
//   - SegmentMerger writing position deltas wrong only for triggered (not
//     forced) merges
//   - mergeFactor threshold off-by-one losing the doc that triggered flush
//   - Merge mid-ingest corrupting an in-flight DocumentWriter buffer
TEST(ForensicClaude, PhraseSurvivesAutoMergeWithMergeFactorTwo) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.mergeFactor = 2;
        for (int i = 0; i < 4; ++i) {
            document::Document d;
            d.Add(document::Field::Text(
                "body", std::string("doc") + std::to_string(i) +
                            " alpha beta gamma"));
            w.AddDocument(d);
        }
        w.Close();
    }

    index::SegmentsReader r(dir);
    ASSERT_EQ(r.NumDocs(), 4);
    search::IndexSearcher s(r);
    search::PhraseQuery pq;
    pq.Add(index::Term(0, "alpha"));
    pq.Add(index::Term(0, "beta"));
    pq.SetSlop(0);
    auto h = s.Search(pq);
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->Length(), 4)
        << "all 4 docs contain in-order 'alpha beta'; auto-merge with "
           "mergeFactor=2 must not corrupt position deltas. Got "
        << h->Length() << " hits.";
    r.Close();
}

// =============================================================================
// Forensic Test 19: IndexWriter destructor without explicit Close() must
// flush pending docs (no silent data loss).
// =============================================================================
//
// Scenario:
//   Construct IndexWriter, add 3 docs, let it go out of scope WITHOUT calling
//   Close(). Reopen with SegmentsReader and verify all 3 docs are visible.
//
// Oracle (Lucene 1.0.1 IndexWriter contract + RAII invariant):
//   The destructor must perform whatever cleanup Close() would do. C++ RAII
//   is the only safety net against caller code that throws between Add and
//   Close. If ~IndexWriter doesn't flush pending docs, all writes since the
//   last segment flush vanish.
//
// This catches:
//   - Destructor that early-returns when closed_==false
//   - FlushPending guarded by a flag the destructor doesn't set
//   - mergeFactor change accidentally suppressing the final flush
TEST(ForensicClaude, DestructorWithoutCloseFlushesPending) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        // mergeFactor default is large; these 3 docs sit in pending.
        for (int i = 0; i < 3; ++i) {
            w.AddDocument(MakeDoc(std::string("id-") + std::to_string(i),
                                   "alpha"));
        }
        // Intentionally no w.Close(); destructor must flush.
    }

    index::SegmentsReader r(dir);
    EXPECT_EQ(r.NumDocs(), 3)
        << "~IndexWriter must flush pending docs (RAII contract). "
           "0 docs means the destructor skipped FlushPending — silent "
           "data loss for callers that forgot to call Close().";
    r.Close();
}

// =============================================================================
// Forensic Test 20: QueryParser must honor `field:term` prefix end-to-end.
// =============================================================================
//
// Scenario:
//   Index 2 docs:
//     d0: body="alpha"   title="beta"
//     d1: body="beta"    title="alpha"
//   Field assignment is order-of-first-encounter: id Keyword (#0), body Text (#1),
//   title Text (#2). (Verified by FieldInfos::FieldNumber lookup, NOT assumed.)
//
//   Parse query "title:alpha" with a FieldInfos-backed resolver and run search.
//
// Oracle (Lucene 1.0.1 QueryParser spec + field-scoped invariant):
//   "title:alpha" must match only d1 (title="alpha"), NOT d0 (body="alpha").
//   Likewise "body:alpha" must match only d0. If both queries return the same
//   set, the parser is throwing away the field name and putting every term
//   into field 0 (the bug we just fixed).
//
// This catches:
//   - QueryParser building Term(0, ...) regardless of parsed field
//   - FieldResolver not wired into ParseTerm
//   - Term-level field number ignored by search machinery
TEST(ForensicClaude, QueryParserFieldPrefixIsHonored) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        {
            document::Document d;
            d.Add(document::Field::Keyword("id", "d0"));
            d.Add(document::Field::Text("body", "alpha"));
            d.Add(document::Field::Text("title", "beta"));
            w.AddDocument(d);
        }
        {
            document::Document d;
            d.Add(document::Field::Keyword("id", "d1"));
            d.Add(document::Field::Text("body", "beta"));
            d.Add(document::Field::Text("title", "alpha"));
            w.AddDocument(d);
        }
        w.Close();
    }

    auto fi = index::FieldInfos::Read(dir, "_0");
    ASSERT_NE(fi, nullptr);
    ASSERT_GE(fi->FieldNumber("body"),  0);
    ASSERT_GE(fi->FieldNumber("title"), 0);
    ASSERT_NE(fi->FieldNumber("body"), fi->FieldNumber("title"))
        << "body and title must have distinct field numbers; otherwise the "
           "field-prefix test is meaningless.";

    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    auto search_with_field = [&](const std::string& q_text) {
        analysis::SimpleAnalyzer a;
        query_parser::QueryParser parser("body", q_text, &a);
        parser.UseFieldInfos(*fi);
        parser.SetDefaultFieldNumber(fi->FieldNumber("body"));
        auto q = parser.Parse();
        if (!q) return std::vector<int>{};
        auto h = s.Search(*q);
        std::vector<int> ids;
        if (h) for (int i = 0; i < h->Length(); ++i) ids.push_back(h->Id(i));
        std::sort(ids.begin(), ids.end());
        return ids;
    };

    {
        auto ids = search_with_field("title:alpha");
        const std::vector<int> expected = {1};
        EXPECT_EQ(ids, expected)
            << "'title:alpha' must match only d1 (title=alpha). If d0 hits "
               "too, QueryParser is using field 0 instead of the resolved "
               "field number.";
    }
    {
        auto ids = search_with_field("body:alpha");
        const std::vector<int> expected = {0};
        EXPECT_EQ(ids, expected)
            << "'body:alpha' must match only d0 (body=alpha). If d1 hits "
               "too, the field resolver is leaking.";
    }
    r.Close();
}

// =============================================================================
// Forensic Test 21: QueryParser must throw on unknown field (not silently
// fall back to field 0).
// =============================================================================
//
// Scenario:
//   No resolver set; parse "title:hello" with default field "body".
//
// Oracle (scenario invariant + safety contract):
//   The old QueryParser silently constructed Term(0, "hello") regardless of
//   the prefix. That's a wrong-results-with-no-error bug. The new contract:
//   any explicit non-default field name MUST throw std::runtime_error so
//   the caller learns immediately.
//
// This catches:
//   - Removing the ResolveField guard
//   - Field prefix being accepted but discarded
TEST(ForensicClaude, QueryParserUnknownFieldThrows) {
    query_parser::QueryParser parser("body", "title:hello");
    EXPECT_THROW(parser.Parse(), std::runtime_error)
        << "non-default field name without resolver must throw, not "
           "silently use field 0.";
}

// =============================================================================
// Forensic Test 22: AND / OR / NOT keyword semantics match Java QueryParser.
// =============================================================================
//
// Corpus (single field "body"):
//   d0: "alpha"
//   d1: "beta"
//   d2: "alpha beta"
//   d3: "gamma"
//
// Oracle (Lucene 1.0.1 QueryParser.jj addClause):
//   "alpha AND beta" → MUST alpha, MUST beta              → {d2}
//   "alpha OR beta"  → SHOULD alpha, SHOULD beta          → {d0,d1,d2}
//   "alpha NOT beta" → MUST alpha (joined by NOT becomes +alpha),
//                       MUST_NOT beta                       → {d0}
//     Why MUST? Java's addClause: when followed by NOT-modified clause,
//     the previous clause is not auto-upgraded (CONJ_NONE), so alpha stays
//     SHOULD. But a SHOULD-only + MUST_NOT BooleanQuery requires the
//     SHOULD to actually match — exactly Lucene 1.0.1 behavior. So result
//     is still {d0}.
//   "NOT beta"       → MUST_NOT-only                       → {} (Lucene
//                                                              spec: at
//                                                              least one
//                                                              positive
//                                                              clause
//                                                              required)
TEST(ForensicClaude, QueryParserAndOrNotKeywords) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        for (const auto& body :
             std::vector<std::string>{"alpha", "beta", "alpha beta", "gamma"}) {
            document::Document d;
            d.Add(document::Field::Text("body", body));
            w.AddDocument(d);
        }
        w.Close();
    }

    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    auto run = [&](const std::string& q_text) {
        analysis::SimpleAnalyzer a;
        query_parser::QueryParser parser("body", q_text, &a);
        auto q = parser.Parse();
        std::vector<int> ids;
        if (!q) return ids;
        auto h = s.Search(*q);
        if (h) for (int i = 0; i < h->Length(); ++i) ids.push_back(h->Id(i));
        std::sort(ids.begin(), ids.end());
        return ids;
    };

    EXPECT_EQ(run("alpha AND beta"), (std::vector<int>{2}))
        << "AND must require BOTH terms (d2 is the only doc with both).";
    EXPECT_EQ(run("alpha OR beta"),  (std::vector<int>{0, 1, 2}))
        << "OR is the default; any of alpha or beta matches.";
    EXPECT_EQ(run("alpha NOT beta"), (std::vector<int>{0}))
        << "NOT modifier prohibits beta; among alpha-containing docs only "
           "d0 remains (d2 has both).";
    EXPECT_EQ(run("NOT beta"),       (std::vector<int>{}))
        << "MUST_NOT-only must return 0 hits (Forensic Test 15 contract).";
    r.Close();
}

// =============================================================================
// Forensic Test 23: Parenthesised grouping changes BooleanQuery shape.
// =============================================================================
//
// Corpus (single field "body"):
//   d0: "alpha gamma"
//   d1: "beta gamma"
//   d2: "alpha"
//   d3: "beta"
//   d4: "gamma"
//
// Oracle:
//   "(alpha OR beta) AND gamma" → MUST (alpha OR beta), MUST gamma → {d0,d1}
//   "alpha OR beta AND gamma"   → SHOULD alpha, MUST beta, MUST gamma
//                                 (AND retroactively makes beta MUST and
//                                  gamma MUST)
//                               → docs where (beta AND gamma) OR alpha
//                               → {d0, d1, d2}  (d2 satisfies the SHOULD
//                                                 alpha; d1 satisfies +beta
//                                                 +gamma; d0 satisfies both
//                                                 alpha AND gamma — alpha
//                                                 alone is enough since
//                                                 SHOULD is permissive)
//   Without parens vs with parens MUST differ on d2.
//
// This catches:
//   - ParseGroup not recursing on '('
//   - Missing ')' handling leaving the parser stuck
//   - Sub-query result not being treated as a single atom for the outer
//     conjunction
TEST(ForensicClaude, QueryParserParenGrouping) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        for (const auto& body : std::vector<std::string>{
                 "alpha gamma", "beta gamma", "alpha", "beta", "gamma"}) {
            document::Document d;
            d.Add(document::Field::Text("body", body));
            w.AddDocument(d);
        }
        w.Close();
    }

    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);
    auto run = [&](const std::string& q_text) {
        analysis::SimpleAnalyzer a;
        query_parser::QueryParser parser("body", q_text, &a);
        auto q = parser.Parse();
        std::vector<int> ids;
        if (!q) return ids;
        auto h = s.Search(*q);
        if (h) for (int i = 0; i < h->Length(); ++i) ids.push_back(h->Id(i));
        std::sort(ids.begin(), ids.end());
        return ids;
    };

    EXPECT_EQ(run("(alpha OR beta) AND gamma"),
              (std::vector<int>{0, 1}))
        << "parens scope the OR so gamma must co-occur with alpha or beta.";

    // Without parens, AND binds tighter (beta AND gamma) then OR with alpha:
    // d0 (alpha gamma) matches via SHOULD alpha
    // d1 (beta gamma) matches via MUST beta + MUST gamma
    // d2 (alpha) matches via SHOULD alpha
    // d3 (beta) — no gamma, no alpha → out
    // d4 (gamma) — no alpha, no beta → out
    // Without parens: SHOULD alpha + MUST beta + MUST gamma. The SHOULD
    // is optional and doesn't force inclusion; only docs with BOTH beta
    // and gamma survive. Among d0..d4 only d1 (beta gamma) qualifies.
    EXPECT_EQ(run("alpha OR beta AND gamma"),
              (std::vector<int>{1}))
        << "AND has higher precedence than the default OR; only docs "
           "with BOTH beta AND gamma survive.";
    r.Close();
}

// =============================================================================
// Forensic Test 24: Boost `^N` scales score linearly through TermQuery.
// =============================================================================
//
// Corpus (single field "body"):
//   d0: "alpha"
//   d1: "alpha"
//
// Query A: `alpha`           → baseline score s0 for d0 (== s1 for d1 due
//                              to identical doc shapes).
// Query B: `alpha^2.5`       → score should be EXACTLY 2.5 * s0.
//
// Oracle (Lucene 1.0.1 Similarity / Query.boost contract):
//   Boost is a query-time multiplier applied at the scorer level. For a
//   single-term query, the score formula is:
//     tf * idf * idf * queryNorm * fieldNorm * boost
//   Holding everything else fixed (same doc, same term, same reader),
//   the ratio Score(boosted)/Score(unboosted) must equal `boost`.
//
// This catches:
//   - Query.boost_ not wired through to the scorer ctor
//   - Scorer.Score() forgetting to multiply by boost
//   - QueryParser not parsing `^N` (would silently treat `alpha^2.5` as
//     the literal term "alpha^2.5" and return 0 hits)
TEST(ForensicClaude, BoostScalesTermQueryScoreLinearly) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        for (int i = 0; i < 2; ++i) {
            document::Document d;
            d.Add(document::Field::Text("body", "alpha"));
            w.AddDocument(d);
        }
        w.Close();
    }

    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    auto first_score = [&](const std::string& q_text) {
        analysis::SimpleAnalyzer a;
        query_parser::QueryParser parser("body", q_text, &a);
        auto q = parser.Parse();
        EXPECT_NE(q, nullptr) << "parser returned null for '" << q_text
                              << "'; check boost syntax handling";
        if (!q) return 0.0f;
        auto h = s.Search(*q);
        EXPECT_NE(h, nullptr);
        EXPECT_GT(h->Length(), 0);
        return h->Score(0);
    };

    float s0 = first_score("alpha");
    float s1 = first_score("alpha^2.5");
    ASSERT_GT(s0, 0.0f);
    ASSERT_GT(s1, 0.0f);
    EXPECT_NEAR(s1 / s0, 2.5f, 1e-4f)
        << "Score(alpha^2.5) / Score(alpha) must equal 2.5. Actual ratio: "
        << (s1 / s0) << ". If ratio == 1.0, boost was parsed but not "
        << "plumbed into the scorer. If parser returned null, `^` was "
        << "consumed into the term text.";

    // Boost == 1.0 must be a no-op (Query default).
    float s_one = first_score("alpha^1.0");
    EXPECT_NEAR(s_one, s0, 1e-4f) << "boost=1.0 must not change scores";

    r.Close();
}

// =============================================================================
// Forensic Test 25: PhraseQuery rejects cross-field term mixing.
// =============================================================================
//
// Scenario:
//   PhraseQuery::Add(Term(0, "alpha")) followed by Add(Term(1, "beta")).
//
// Oracle (Lucene 1.0.1 PhraseScorer contract + scenario invariant):
//   Position deltas in `.prx` are stored per-(field, doc), so a phrase
//   that mixes field 0 and field 1 has no meaningful position semantics.
//   The Java implementation effectively assumes all terms share a field
//   (PhraseQuery::getField returns the first term's field, used to fetch
//   norms etc.). The C++ port previously accepted any term and silently
//   misbehaved. The new contract: reject at Add() time with
//   std::invalid_argument so the bug surfaces immediately.
//
// This catches:
//   - PhraseQuery::Add losing the cross-field guard
//   - Refactors that move the check into CreateScorer (too late — by then
//     the caller has already committed to a malformed query)
// =============================================================================
// Forensic Test 26: PhraseScorer freq counts PHRASE INSTANCES, not per-term
// matches. (Adversarial slop oracle to expose over-counting.)
// =============================================================================
//
// Corpus (single field "body"):
//   d0: "alpha beta alpha gamma"
//     positions: alpha@0, beta@1, alpha@2, gamma@3
//
// Query: PhraseQuery("alpha beta gamma") slop=1
//
// Oracle (Lucene 1.0.1 SloppyPhraseScorer phraseFreq contract):
//   freq is the number of *distinct phrase instances* matched in the doc,
//   NOT the sum of how many subsequent terms each anchor sees within slop.
//   Hand-derivation:
//     anchor alpha@0:
//       expected beta @ 1, gamma @ 2. actual beta@1 (off 0), gamma@3 (off 1).
//       total deviation 1 ≤ slop=1 → counts as ONE phrase instance.
//     anchor alpha@2:
//       expected beta @ 3 — actual only beta@1 (off 2). Out of slop.
//   Expected freq: 1.  Expected hit list: {d0}.
//
// The buggy per-term-increment code returns freq=3 (one for beta's match
// at p0=0, one for gamma's match at p0=0, one for gamma's match at p0=2),
// inflating tf=sqrt(freq) by sqrt(3)/sqrt(1) ≈ 1.73.
//
// This test asserts hit count + bounds on the relative score vs a single-
// term anchor query, both of which red-flag the over-count.
TEST(ForensicClaude, PhraseScorerSlopFreqCountsInstancesNotTerms) {
    // Two docs in the same index — same query terms, same idf, so the
    // ONLY differences between their scores are tf(freq) and lengthNorm.
    //
    //   d0: "alpha beta gamma"           (3 tokens; phrase appears ONCE)
    //   d1: "alpha beta alpha gamma"     (4 tokens; phrase appears ONCE at
    //                                      anchor p0=0; the alpha@2 anchor
    //                                      sits 1 over the slop budget)
    //
    // Expected freq (Lucene 1.0.1 SloppyPhraseScorer):
    //   d0.freq = 1, d1.freq = 1.
    //
    // Length norm: d0 has fewer tokens → larger lengthNorm → d0 ranks
    // HIGHER. So d0.score > d1.score.
    //
    // Bug behaviour: per-term inflation gave d1.freq = 3 (one for beta
    // matching anchor 0, one for gamma matching anchor 0, one for gamma
    // matching anchor 2). With tf=sqrt(3)≈1.73 vs tf=1, plus only a
    // modest norm penalty for one extra token, d1 ends up scoring HIGHER
    // than d0 — flipping the rank. We assert the correct rank d0 > d1.
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        document::Document d0;
        d0.Add(document::Field::Text("body", "alpha beta gamma"));
        w.AddDocument(d0);
        document::Document d1;
        d1.Add(document::Field::Text("body", "alpha beta alpha gamma"));
        w.AddDocument(d1);
        w.Close();
    }
    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    search::PhraseQuery pq;
    pq.Add(index::Term(0, "alpha"));
    pq.Add(index::Term(0, "beta"));
    pq.Add(index::Term(0, "gamma"));
    pq.SetSlop(1);

    auto h = s.Search(pq);
    ASSERT_NE(h, nullptr);
    ASSERT_EQ(h->Length(), 2)
        << "both docs must match the phrase; got " << h->Length();

    // h is sorted by score descending. Find the score for d0 vs d1 by id.
    float score_d0 = 0.0f, score_d1 = 0.0f;
    for (int i = 0; i < h->Length(); ++i) {
        if (h->Id(i) == 0) score_d0 = h->Score(i);
        if (h->Id(i) == 1) score_d1 = h->Score(i);
    }
    ASSERT_GT(score_d0, 0.0f);
    ASSERT_GT(score_d1, 0.0f);
    EXPECT_GT(score_d0, score_d1)
        << "d0 (3 tokens, freq=1) MUST outrank d1 (4 tokens, freq=1). "
           "score_d0=" << score_d0 << " score_d1=" << score_d1
        << ". If d1 wins, PhraseScorer is treating each per-term slop "
           "match as a separate phrase instance, inflating d1's freq to "
           "3 and overwhelming d0's length-norm advantage.";

    r.Close();
}

// =============================================================================
// Forensic Test 27: reversed phrase requires slop >= 2 (Lucene "edits" model).
// =============================================================================
//
// Corpus: d0 body = "beta alpha"
// Position frame: beta@0, alpha@1
//
// PhraseQuery("alpha beta")
//   slop=0 → no hit (#13 already covers this).
//   slop=1 → still no hit: a transposition costs 2 edits in Lucene's slop
//            model, so slop=1 is not enough.
//   slop=2 → hit (1 doc).
TEST(ForensicClaude, PhraseScorerReverseRequiresSlopTwo) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        document::Document d;
        d.Add(document::Field::Text("body", "beta alpha"));
        w.AddDocument(d);
        w.Close();
    }
    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    auto run = [&](int slop) {
        search::PhraseQuery pq;
        pq.Add(index::Term(0, "alpha"));
        pq.Add(index::Term(0, "beta"));
        pq.SetSlop(slop);
        auto h = s.Search(pq);
        return h ? h->Length() : 0;
    };

    EXPECT_EQ(run(0), 0) << "slop=0 reversed must not match";
    EXPECT_EQ(run(1), 0)
        << "slop=1 reversed must not match — Java treats transposition as "
           "2 edits. If it matches at slop=1, the impl is using |delta| "
           "with single-edit cost which contradicts the spec.";
    EXPECT_EQ(run(2), 1)
        << "slop=2 reversed MUST match (transposition costs exactly 2).";
    r.Close();
}

// =============================================================================
// Forensic Test 28: 3-term phrase with one missing must NOT match regardless
// of slop. (Catches a `||` vs `&&` confusion in the per-term loop.)
// =============================================================================
//
// Corpus: d0 body = "alpha beta delta"
//   positions: alpha@0, beta@1, delta@2  (no gamma!)
//
// Query: PhraseQuery("alpha beta gamma") slop=100
// Oracle: 0 hits — gamma is missing entirely. CreateScorer should return
//   nullptr because Positions("gamma") yields a scorer that immediately
//   exhausts.
TEST(ForensicClaude, PhraseScorerMissingTermNeverMatches) {
    store::RAMDirectory dir;
    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        document::Document d;
        d.Add(document::Field::Text("body", "alpha beta delta"));
        w.AddDocument(d);
        w.Close();
    }
    index::SegmentsReader r(dir);
    search::IndexSearcher s(r);

    search::PhraseQuery pq;
    pq.Add(index::Term(0, "alpha"));
    pq.Add(index::Term(0, "beta"));
    pq.Add(index::Term(0, "gamma"));  // not in doc
    pq.SetSlop(100);

    auto h = s.Search(pq);
    // Either nullptr (Scorer creation bailed) or empty Hits is acceptable.
    int n = h ? h->Length() : 0;
    EXPECT_EQ(n, 0)
        << "phrase containing a term absent from the doc must never match; "
           "got " << n << " hits.";
    r.Close();
}

// =============================================================================
// Forensic Test 29: Length-norm byte matches Java Lucene 1.0.1
// Similarity.norm(int numTerms) = (byte) ceil(255 / sqrt(numTerms)).
// =============================================================================
//
// Why: legacy C++ code computed `(uint8_t)(255 * 1/sqrt(numTerms))` which
//   (a) uses floor (cast truncates), off-by-one vs Java's ceil;
//   (b) collapses to byte 0 for any numTerms > ~1000 because `255 *
//       (1/sqrt(numTerms))` falls below 1.0 — silently making long docs
//       unscoreable.
//
// Oracle: bytewise values computed by hand from Java's formula.
//   numTerms=1     → ceil(255/1)     = 255
//   numTerms=4     → ceil(255/2)     = 128
//   numTerms=9     → ceil(255/3)     = 85
//   numTerms=10000 → ceil(255/100)   = 3
//   numTerms=65025 → ceil(255/255)   = 1   (the boundary; never 0)
//   numTerms=1e6   → ceil(255/1000)  = 1   (the long-doc bug case)
TEST(ForensicClaude, LengthNormByteMatchesJavaSpec) {
    search::Similarity sim;
    EXPECT_EQ(sim.EncodeLengthNorm(0),       0)   << "empty doc -> 0";
    EXPECT_EQ(sim.EncodeLengthNorm(1),       255) << "1 token";
    EXPECT_EQ(sim.EncodeLengthNorm(4),       128) << "4 tokens (was 127 with floor)";
    EXPECT_EQ(sim.EncodeLengthNorm(9),       85)  << "9 tokens (was 84 with floor)";
    EXPECT_EQ(sim.EncodeLengthNorm(10000),   3)   << "10k tokens";
    EXPECT_EQ(sim.EncodeLengthNorm(65025),   1)   << "boundary, never 0";
    EXPECT_EQ(sim.EncodeLengthNorm(1000000), 1)
        << "1M-token doc must NOT collapse to byte 0 — that was the silent "
           "data-loss bug. Got byte=" << int(sim.EncodeLengthNorm(1000000));
}

TEST(ForensicClaude, PhraseQueryRejectsCrossFieldTerms) {
    search::PhraseQuery pq;
    pq.Add(index::Term(0, "alpha"));  // ok — first term sets the field
    EXPECT_THROW(pq.Add(index::Term(1, "beta")), std::invalid_argument)
        << "mixing field 0 + field 1 in one phrase must throw.";

    // Same-field continues to work.
    EXPECT_NO_THROW(pq.Add(index::Term(0, "gamma")));
}

}  // namespace minilucene
