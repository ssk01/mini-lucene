#pragma once

#include "minilucene/search/top_docs.h"
#include "minilucene/util/priority_queue.h"

namespace minilucene {
namespace search {

class HitQueue : public util::PriorityQueue<ScoreDoc> {
public:
    explicit HitQueue(int size) { Initialize(size); }
protected:
    bool LessThan(const ScoreDoc& a, const ScoreDoc& b) const override {
        if (a.score == b.score)
            return a.doc > b.doc;
        return a.score < b.score;
    }
};

}  // namespace search
}  // namespace minilucene
