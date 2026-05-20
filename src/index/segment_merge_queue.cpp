#include "minilucene/index/segment_merge_queue.h"
#include "minilucene/index/segment_merge_info.h"

namespace minilucene {
namespace index {

SegmentMergeQueue::SegmentMergeQueue(int size) { Initialize(size); }

bool SegmentMergeQueue::LessThan(SegmentMergeInfo* const& a, SegmentMergeInfo* const& b) const {
    int cmp = a->term_.Text().compare(b->term_.Text());
    if (cmp == 0) {
        int fc = a->term_.FieldNumber() - b->term_.FieldNumber();
        if (fc == 0) return a->base_ < b->base_;
        return fc < 0;
    }
    return cmp < 0;
}

void SegmentMergeQueue::Close() {
    while (Size() > 0) {
        auto* info = Top();
        Pop();
        info->Close();
    }
}

}  // namespace index
}  // namespace minilucene
