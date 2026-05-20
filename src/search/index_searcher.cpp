#include "minilucene/search/index_searcher.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/search/scorer.h"
#include "minilucene/search/term_query.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/util/priority_queue.h"

#include <algorithm>

namespace minilucene {
namespace search {

namespace {

class HitQueue : public util::PriorityQueue<ScoreDoc> {
public:
    explicit HitQueue(int top_k) { Initialize(top_k); }
protected:
    bool LessThan(const ScoreDoc& a, const ScoreDoc& b) const override {
        return a.score < b.score;
    }
};

}  // namespace

IndexSearcher::IndexSearcher(index::IndexReader& reader)
    : reader_(&reader) {}

TopDocs IndexSearcher::Search(const TermQuery& query, int top_k) const {
    auto scorer = query.Scorer(*reader_);
    if (!scorer) return TopDocs{};

    HitQueue queue(top_k);
    int total_hits = 0;

    while (scorer->Next()) {
        ++total_hits;
        ScoreDoc sd;
        sd.doc = scorer->Doc();
        sd.score = scorer->Score();
        queue.Put(sd);
    }

    TopDocs result;
    result.total_hits = total_hits;
    while (queue.Size() > 0) {
        result.score_docs.push_back(queue.Top());
        queue.Pop();
    }
    std::reverse(result.score_docs.begin(), result.score_docs.end());
    return result;
}

}  // namespace search
}  // namespace minilucene
